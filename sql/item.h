#ifndef ITEM_INCLUDED
#define ITEM_INCLUDED

/* Copyright (c) 2000, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <new>

#include "binary_log_types.h"
#include "enum_query_type.h"
#include "field.h"       // Derivation
#include "handler.h"
#include "m_ctype.h"
#include "m_string.h"
#include "my_bitmap.h"
#include "my_compiler.h"
#include "my_dbug.h"
#include "my_decimal.h"  // my_decimal
#include "my_double2ulonglong.h"
#include "my_inttypes.h"
#include "my_macros.h"
#include "my_sys.h"
#include "my_table_map.h"
#include "my_time.h"
#include "mysql_com.h"
#include "mysqld_error.h"
#include "parse_tree_node_base.h" // Parse_tree_node
#include "sql_alloc.h"
#include "sql_array.h"   // Bounds_checked_array
#include "sql_const.h"
#include "sql_plugin_ref.h"
#include "sql_string.h"
#include "system_variables.h"
#include "table.h"
#include "table_trigger_field_support.h" // Table_trigger_field_support
#include "template_utils.h"
#include "thr_malloc.h"
#include "trigger_def.h" // enum_trigger_variable_type
#include "typelib.h"

class Item;
class Item_field;
class Json_wrapper;
class Protocol;
class SELECT_LEX;
class Security_context;
class THD;
class user_var_entry;
template <class T> class List;
template <class T> class List_iterator;
template <typename T> class SQL_I_List;

typedef Bounds_checked_array<Item*> Ref_item_array;

void item_init(void);			/* Init item functions */

/**
  Default condition filtering (selectivity) values used by
  get_filtering_effect() and friends when better estimates
  (statistics) are not available for a predicate.
*/
/**
  For predicates that are always satisfied. Must be 1.0 or the filter
  calculation logic will break down.
*/
#define COND_FILTER_ALLPASS 1.0f
/// Filtering effect for equalities: col1 = col2
#define COND_FILTER_EQUALITY 0.1f
/// Filtering effect for inequalities: col1 > col2
#define COND_FILTER_INEQUALITY 0.3333f
/// Filtering effect for between: col1 BETWEEN a AND b
#define COND_FILTER_BETWEEN 0.1111f
/**
   Value is out-of-date, will need recalculation.
   This is used by post-greedy-search logic which changes the access method and thus
   makes obsolete the filtering value calculated by best_access_path(). For
  example, test_if_skip_sort_order().
*/
#define COND_FILTER_STALE -1.0f
/**
   A special subcase of the above:
   - if this is table/index/range scan, and
   - rows_fetched is how many rows we will examine, and
   - rows_fetched is less than the number of rows in the table (as determined
   by test_if_cheaper_ordering() and test_if_skip_sort_order()).
   Unlike the ordinary case where rows_fetched:
   - is set by calculate_scan_cost(), and
   - is how many rows pass the constant condition (so, less than we will
   examine), and
   - the actual rows_fetched to show in EXPLAIN is the number of rows in the
   table (== rows which we will examine), and
   - the constant condition's effect has to be moved to filter_effect for
   EXPLAIN.
*/
#define COND_FILTER_STALE_NO_CONST -2.0f

static inline uint32
char_to_byte_length_safe(uint32 char_length_arg, uint32 mbmaxlen_arg)
{
   ulonglong tmp= ((ulonglong) char_length_arg) * mbmaxlen_arg;
   return (tmp > UINT_MAX32) ? (uint32) UINT_MAX32 : (uint32) tmp;
}


/*
   "Declared Type Collation"
   A combination of collation and its derivation.

  Flags for collation aggregation modes:
  MY_COLL_ALLOW_SUPERSET_CONV  - allow conversion to a superset
  MY_COLL_ALLOW_COERCIBLE_CONV - allow conversion of a coercible value
                                 (i.e. constant).
  MY_COLL_ALLOW_CONV           - allow any kind of conversion
                                 (combination of the above two)
  MY_COLL_ALLOW_NUMERIC_CONV   - if all items were numbers, convert to
                                 @@character_set_connection
  MY_COLL_DISALLOW_NONE        - don't allow return DERIVATION_NONE
                                 (e.g. when aggregating for comparison)
  MY_COLL_CMP_CONV             - combination of MY_COLL_ALLOW_CONV
                                 and MY_COLL_DISALLOW_NONE
*/

#define MY_COLL_ALLOW_SUPERSET_CONV   1
#define MY_COLL_ALLOW_COERCIBLE_CONV  2
#define MY_COLL_DISALLOW_NONE         4
#define MY_COLL_ALLOW_NUMERIC_CONV    8

#define MY_COLL_ALLOW_CONV (MY_COLL_ALLOW_SUPERSET_CONV | MY_COLL_ALLOW_COERCIBLE_CONV)
#define MY_COLL_CMP_CONV   (MY_COLL_ALLOW_CONV | MY_COLL_DISALLOW_NONE)

class DTCollation {
public:
  const CHARSET_INFO *collation;
  enum Derivation derivation;
  uint repertoire;
  
  void set_repertoire_from_charset(const CHARSET_INFO *cs)
  {
    repertoire= cs->state & MY_CS_PUREASCII ?
                MY_REPERTOIRE_ASCII : MY_REPERTOIRE_UNICODE30;
  }
  DTCollation()
  {
    collation= &my_charset_bin;
    derivation= DERIVATION_NONE;
    repertoire= MY_REPERTOIRE_UNICODE30;
  }
  DTCollation(const CHARSET_INFO *collation_arg, Derivation derivation_arg)
  {
    collation= collation_arg;
    derivation= derivation_arg;
    set_repertoire_from_charset(collation_arg);
  }
  void set(const DTCollation &dt)
  { 
    collation= dt.collation;
    derivation= dt.derivation;
    repertoire= dt.repertoire;
  }
  void set(const CHARSET_INFO *collation_arg, Derivation derivation_arg)
  {
    collation= collation_arg;
    derivation= derivation_arg;
    set_repertoire_from_charset(collation_arg);
  }
  void set(const CHARSET_INFO *collation_arg,
           Derivation derivation_arg,
           uint repertoire_arg)
  {
    collation= collation_arg;
    derivation= derivation_arg;
    repertoire= repertoire_arg;
  }
  void set_numeric()
  {
    collation= &my_charset_numeric;
    derivation= DERIVATION_NUMERIC;
    repertoire= MY_REPERTOIRE_NUMERIC;
  }
  void set(const CHARSET_INFO *collation_arg)
  {
    collation= collation_arg;
    set_repertoire_from_charset(collation_arg);
  }
  void set(Derivation derivation_arg)
  { derivation= derivation_arg; }
  void set_repertoire(uint repertoire_arg)
  { repertoire= repertoire_arg; }
  bool aggregate(DTCollation &dt, uint flags= 0);
  bool set(DTCollation &dt1, DTCollation &dt2, uint flags= 0)
  { set(dt1); return aggregate(dt2, flags); }
  const char *derivation_name() const
  {
    switch(derivation)
    {
      case DERIVATION_NUMERIC:   return "NUMERIC";
      case DERIVATION_IGNORABLE: return "IGNORABLE";
      case DERIVATION_COERCIBLE: return "COERCIBLE";
      case DERIVATION_IMPLICIT:  return "IMPLICIT";
      case DERIVATION_SYSCONST:  return "SYSCONST";
      case DERIVATION_EXPLICIT:  return "EXPLICIT";
      case DERIVATION_NONE:      return "NONE";
      default: return "UNKNOWN";
    }
  }
};


/**
  Class used as argument to Item::walk() together with mark_field_in_map()
*/
class Mark_field
{
public:
  Mark_field(TABLE *table, enum_mark_columns mark) :
  table(table), mark(mark)
  {}
  Mark_field(enum_mark_columns mark) :
  table(NULL), mark(mark)
  {}

  /**
     If == NULL, update map of any table.
     If <> NULL, update map of only this table.
  */
  TABLE *const table;
  /// How to mark the map.
  const enum_mark_columns mark;
};


/**
  Class used as argument to Item::walk() together with used_tables_for_level()
*/
class Used_tables
{
public:
  explicit Used_tables(SELECT_LEX *select) :
  select(select), used_tables(0)
  {}

  SELECT_LEX *const select;           ///< Level for which data is accumulated
  table_map used_tables;              ///< Accumulated used tables data
};

/*************************************************************************/

/**
  Storage for name strings.
  Enpowers Simple_cstring with allocation routines from the sql_strmake family.

  This class must stay as small as possible as we often 
  pass it into functions using call-by-value evaluation.

  Don't add new members or virual methods into this class!
*/
class Name_string: public Simple_cstring
{
private:
  void set_or_copy(const char *str, size_t length, bool is_null_terminated)
  {
    if (is_null_terminated)
      set(str, length);
    else
      copy(str, length);  
  }
public:
  Name_string(): Simple_cstring() {}
  /*
    Please do NOT add constructor Name_string(const char *str) !
    It will involve hidden strlen() call, which can affect
    performance negatively. Use Name_string(str, len) instead.
  */
  Name_string(const char *str, size_t length):
    Simple_cstring(str, length) {}
  Name_string(const LEX_STRING str): Simple_cstring(str) {}
  Name_string(const char *str, size_t length, bool is_null_terminated):
    Simple_cstring()
  {
    set_or_copy(str, length, is_null_terminated);
  }
  Name_string(const LEX_STRING str, bool is_null_terminated):
    Simple_cstring()
  {
    set_or_copy(str.str, str.length, is_null_terminated);
  }
  /**
    Allocate space using sql_strmake() or sql_strmake_with_convert().
  */
  void copy(const char *str, size_t length, const CHARSET_INFO *cs);
  /**
    Variants for copy(), for various argument combinations.
  */
  void copy(const char *str, size_t length)
  {
    copy(str, length, system_charset_info);
  }
  void copy(const char *str)
  {
    copy(str, (str ? strlen(str) : 0), system_charset_info);
  }
  void copy(const LEX_STRING lex)
  {
    copy(lex.str, lex.length);
  }
  void copy(const LEX_STRING *lex)
  {
    copy(lex->str, lex->length);
  }
  void copy(const Name_string str)
  {
    copy(str.ptr(), str.length());
  }
  /**
    Compare name to another name in C string, case insensitively.
  */
  bool eq(const char *str) const
  {
    DBUG_ASSERT(str && ptr());
    return my_strcasecmp(system_charset_info, ptr(), str) == 0;
  }
  bool eq_safe(const char *str) const
  {
    return is_set() && str && eq(str);
  }
  /**
    Compare name to another name in Name_string, case insensitively.
  */
  bool eq(const Name_string name) const
  {
    return eq(name.ptr());
  }
  bool eq_safe(const Name_string name) const
  {
    return is_set() && name.is_set() && eq(name);
  }
};


#define NAME_STRING(x)  Name_string(C_STRING_WITH_LEN(x))


extern const Name_string null_name_string;


/**
  Storage for Item names.
  Adds "autogenerated" flag and warning functionality to Name_string.
*/
class Item_name_string: public Name_string
{
private:
  bool m_is_autogenerated; /* indicates if name of this Item
                              was autogenerated or set by user */
public:
  Item_name_string(): Name_string(), m_is_autogenerated(true)
  { }
  Item_name_string(const Name_string name)
    :Name_string(name), m_is_autogenerated(true)
  { }
  /**
    Set m_is_autogenerated flag to the given value.
  */
  void set_autogenerated(bool is_autogenerated)
  {
    m_is_autogenerated= is_autogenerated;
  }
  /**
    Return the auto-generated flag.
  */
  bool is_autogenerated() const { return m_is_autogenerated; }
  using Name_string::copy;
  /**
    Copy name together with autogenerated flag.
    Produce a warning if name was cut.
  */
  void copy(const char *str_arg, size_t length_arg, const CHARSET_INFO *cs_arg,
           bool is_autogenerated_arg);
};


/*
  Instances of Name_resolution_context store the information necesary for
  name resolution of Items and other context analysis of a query made in
  fix_fields().

  This structure is a part of SELECT_LEX, a pointer to this structure is
  assigned when an item is created (which happens mostly during  parsing
  (sql_yacc.yy)), but the structure itself will be initialized after parsing
  is complete

  TODO: move subquery of INSERT ... SELECT and CREATE ... SELECT to
  separate SELECT_LEX which allow to remove tricks of changing this
  structure before and after INSERT/CREATE and its SELECT to make correct
  field name resolution.
*/
struct Name_resolution_context: Sql_alloc
{
  /*
    The name resolution context to search in when an Item cannot be
    resolved in this context (the context of an outer select)
  */
  Name_resolution_context *outer_context;
  /// Link to next name res context with the same query block as the base
  Name_resolution_context *next_context;

  /*
    List of tables used to resolve the items of this context.  Usually these
    are tables from the FROM clause of SELECT statement.  The exceptions are
    INSERT ... SELECT and CREATE ... SELECT statements, where SELECT
    subquery is not moved to a separate SELECT_LEX.  For these types of
    statements we have to change this member dynamically to ensure correct
    name resolution of different parts of the statement.
  */
  TABLE_LIST *table_list;
  /*
    In most cases the two table references below replace 'table_list' above
    for the purpose of name resolution. The first and last name resolution
    table references allow us to search only in a sub-tree of the nested
    join tree in a FROM clause. This is needed for NATURAL JOIN, JOIN ... USING
    and JOIN ... ON. 
  */
  TABLE_LIST *first_name_resolution_table;
  /*
    Last table to search in the list of leaf table references that begins
    with first_name_resolution_table.
  */
  TABLE_LIST *last_name_resolution_table;

  /*
    SELECT_LEX item belong to, in case of merged VIEW it can differ from
    SELECT_LEX where item was created, so we can't use table_list/field_list
    from there
  */
  SELECT_LEX *select_lex;

  /*
    Processor of errors caused during Item name resolving, now used only to
    hide underlying tables in errors about views (i.e. it substitute some
    errors for views)
  */
  bool view_error_handler;
  TABLE_LIST *view_error_handler_arg;

  /**
    When TRUE, items are resolved in this context against
    SELECT_LEX::item_list, SELECT_lex::group_list and
    this->table_list. If FALSE, items are resolved only against
    this->table_list.

    @see SELECT_LEX::item_list, SELECT_LEX::group_list
  */
  bool resolve_in_select_list;

  /*
    Security context of this name resolution context. It's used for views
    and is non-zero only if the view is defined with SQL SECURITY DEFINER.
  */
  Security_context *security_ctx;

  Name_resolution_context()
    :outer_context(NULL), next_context(NULL),
    table_list(NULL), select_lex(NULL),
    view_error_handler_arg(NULL), security_ctx(NULL)
    {}

  void init()
  {
    resolve_in_select_list= FALSE;
    view_error_handler= false;
    first_name_resolution_table= NULL;
    last_name_resolution_table= NULL;
  }

  void resolve_in_table_list_only(TABLE_LIST *tables)
  {
    table_list= first_name_resolution_table= tables;
    resolve_in_select_list= FALSE;
  }
};


/*
  Store and restore the current state of a name resolution context.
*/

class Name_resolution_context_state
{
private:
  TABLE_LIST *save_table_list;
  TABLE_LIST *save_first_name_resolution_table;
  TABLE_LIST *save_next_name_resolution_table;
  bool        save_resolve_in_select_list;
  TABLE_LIST *save_next_local;

public:
  Name_resolution_context_state() {}          /* Remove gcc warning */

public:
  /* Save the state of a name resolution context. */
  void save_state(Name_resolution_context *context, TABLE_LIST *table_list)
  {
    save_table_list=                  context->table_list;
    save_first_name_resolution_table= context->first_name_resolution_table;
    save_resolve_in_select_list=      context->resolve_in_select_list;
    save_next_local=                  table_list->next_local;
    save_next_name_resolution_table=  table_list->next_name_resolution_table;
  }

  /* Restore a name resolution context from saved state. */
  void restore_state(Name_resolution_context *context, TABLE_LIST *table_list)
  {
    table_list->next_local=                save_next_local;
    table_list->next_name_resolution_table= save_next_name_resolution_table;
    context->table_list=                   save_table_list;
    context->first_name_resolution_table=  save_first_name_resolution_table;
    context->resolve_in_select_list=       save_resolve_in_select_list;
  }

  TABLE_LIST *get_first_name_resolution_table()
  {
    return save_first_name_resolution_table;
  }
};


/*
  This enum is used to report information about monotonicity of function
  represented by Item* tree.
  Monotonicity is defined only for Item* trees that represent table
  partitioning expressions (i.e. have no subselects/user vars/PS parameters
  etc etc). An Item* tree is assumed to have the same monotonicity properties
  as its correspoinding function F:

  [signed] longlong F(field1, field2, ...) {
    put values of field_i into table record buffer;
    return item->val_int(); 
  }

  NOTE
  At the moment function monotonicity is not well defined (and so may be
  incorrect) for Item trees with parameters/return types that are different
  from INT_RESULT, may be NULL, or are unsigned.
  It will be possible to address this issue once the related partitioning bugs
  (BUG#16002, BUG#15447, BUG#13436) are fixed.

  The NOT_NULL enums are used in TO_DAYS, since TO_DAYS('2001-00-00') returns
  NULL which puts those rows into the NULL partition, but
  '2000-12-31' < '2001-00-00' < '2001-01-01'. So special handling is needed
  for this (see Bug#20577).
*/

typedef enum monotonicity_info 
{
   NON_MONOTONIC,              /* none of the below holds */
   MONOTONIC_INCREASING,       /* F() is unary and (x < y) => (F(x) <= F(y)) */
   MONOTONIC_INCREASING_NOT_NULL,  /* But only for valid/real x and y */
   MONOTONIC_STRICT_INCREASING,/* F() is unary and (x < y) => (F(x) <  F(y)) */
   MONOTONIC_STRICT_INCREASING_NOT_NULL  /* But only for valid/real x and y */
} enum_monotonicity_info;


/**
   A type for SQL-like 3-valued Booleans: true/false/unknown.
*/
class Bool3
{
public:
  /// @returns an instance set to "FALSE"
  static const Bool3 false3() { return Bool3(v_FALSE); }
  /// @returns an instance set to "UNKNOWN"
  static const Bool3 unknown3() { return Bool3(v_UNKNOWN); }
  /// @returns an instance set to "TRUE"
  static const Bool3 true3() { return Bool3(v_TRUE); }

  bool is_true() const { return m_val == v_TRUE; }
  bool is_unknown() const { return m_val == v_UNKNOWN; }
  bool is_false() const { return m_val == v_FALSE; }

private:
  enum value { v_FALSE, v_UNKNOWN, v_TRUE };
  /// This is private; instead, use false3()/etc.
  Bool3(value v) : m_val(v) {}

  value m_val;
  /*
    No operator to convert Bool3 to bool (or int) - intentionally: how
    would you map UNKNOWN3 to true/false?
    It is because we want to block such conversions that Bool3 is a class
    instead of a plain enum.
  */
};

/*************************************************************************/

class sp_rcontext;


class Settable_routine_parameter
{
public:
  /*
    Set required privileges for accessing the parameter.

    SYNOPSIS
      set_required_privilege()
        rw        if 'rw' is true then we are going to read and set the
                  parameter, so SELECT and UPDATE privileges might be
                  required, otherwise we only reading it and SELECT
                  privilege might be required.
  */
  Settable_routine_parameter() {}
  virtual ~Settable_routine_parameter() {}
  virtual void set_required_privilege(bool rw MY_ATTRIBUTE((unused))) {};

  /*
    Set parameter value.

    SYNOPSIS
      set_value()
        thd       thread handle
        ctx       context to which parameter belongs (if it is local
                  variable).
        it        item which represents new value

    RETURN
      FALSE if parameter value has been set,
      TRUE if error has occured.
  */
  virtual bool set_value(THD *thd, sp_rcontext *ctx, Item **it)= 0;

  virtual void set_out_param_info(Send_field *info MY_ATTRIBUTE((unused))) {}

  virtual const Send_field *get_out_param_info() const
  { return NULL; }
};


typedef bool (Item::*Item_processor) (uchar *arg);
/*
  Analyzer function
    SYNOPSIS
      argp   in/out IN:  Analysis parameter
                    OUT: Parameter to be passed to the transformer

    RETURN 
      TRUE   Invoke the transformer
      FALSE  Don't do it

*/
typedef bool (Item::*Item_analyzer) (uchar **argp);
typedef Item* (Item::*Item_transformer) (uchar *arg);
typedef void (*Cond_traverser) (const Item *item, void *arg);


class Item : public Parse_tree_node
{
  typedef Parse_tree_node super;

  virtual bool is_expensive_processor(uchar *) { return false; }

protected:
  /**
     Sets the result value of the function an empty string, using the current
     character set. No memory is allocated.
     @retval A pointer to the str_value member.
   */
  String *make_empty_result() {
    str_value.set("", 0, collation.collation);
    return &str_value; 
  }

public:
  Item(const Item &)= delete;
  void operator=(Item &)= delete;
  static void *operator new(size_t size) throw ()
  { return sql_alloc(size); }
  static void *operator new(size_t size, MEM_ROOT *mem_root,
                            const std::nothrow_t &arg MY_ATTRIBUTE((unused))
                            = std::nothrow) throw ()
  { return alloc_root(mem_root, size); }

  static void operator delete(void *ptr MY_ATTRIBUTE((unused)),
                              size_t size MY_ATTRIBUTE((unused)))
  { TRASH(ptr, size); }
  static void operator delete(void*, MEM_ROOT*,
                              const std::nothrow_t&) throw ()
  {}

  enum Type {INVALID_ITEM= 0,
             FIELD_ITEM, FUNC_ITEM, SUM_FUNC_ITEM, STRING_ITEM,
	     INT_ITEM, REAL_ITEM, NULL_ITEM, VARBIN_ITEM,
	     COPY_STR_ITEM, FIELD_AVG_ITEM, DEFAULT_VALUE_ITEM,
	     PROC_ITEM,COND_ITEM, REF_ITEM, FIELD_STD_ITEM,
	     FIELD_VARIANCE_ITEM, INSERT_VALUE_ITEM,
             SUBSELECT_ITEM, ROW_ITEM, CACHE_ITEM, TYPE_HOLDER,
             PARAM_ITEM, TRIGGER_FIELD_ITEM, DECIMAL_ITEM,
             XPATH_NODESET, XPATH_NODESET_CMP,
             VIEW_FIXER_ITEM, FIELD_BIT_ITEM, NULL_RESULT_ITEM };

  enum cond_result { COND_UNDEF,COND_OK,COND_TRUE,COND_FALSE };

  enum traverse_order { POSTFIX, PREFIX };

  enum enum_walk
  {
    WALK_PREFIX=   0x01,
    WALK_POSTFIX=  0x02,
    WALK_SUBQUERY= 0x04,
    WALK_SUBQUERY_PREFIX= 0x05,   // Combine prefix and subquery traversal
    WALK_SUBQUERY_POSTFIX= 0x06   // Combine postfix and subquery traversal
  };

  /**
    Provide data type for a user or system variable, based on the type of
    the item that is assigned to the variable.

    @param src_type  Source type that variable's type is derived from
    @param max_bytes Maximum string size in bytes, used for string types
  */
  static enum_field_types type_for_variable(enum_field_types src_type,
                                            uint32 max_bytes)
  {
    switch (src_type)
    {
      case MYSQL_TYPE_TINY:
      case MYSQL_TYPE_SHORT:
      case MYSQL_TYPE_INT24:
      case MYSQL_TYPE_LONG:
      case MYSQL_TYPE_LONGLONG:
        return MYSQL_TYPE_LONGLONG;
      case MYSQL_TYPE_DECIMAL:
      case MYSQL_TYPE_NEWDECIMAL:
        return MYSQL_TYPE_NEWDECIMAL;
      case MYSQL_TYPE_FLOAT:
      case MYSQL_TYPE_DOUBLE:
        return MYSQL_TYPE_DOUBLE;
      case MYSQL_TYPE_VARCHAR:
      case MYSQL_TYPE_VAR_STRING:
      case MYSQL_TYPE_STRING:
        return MYSQL_TYPE_VARCHAR;
      case MYSQL_TYPE_YEAR:
        return MYSQL_TYPE_LONGLONG;
      case MYSQL_TYPE_TIMESTAMP:
      case MYSQL_TYPE_DATE:
      case MYSQL_TYPE_TIME:
      case MYSQL_TYPE_DATETIME:
      case MYSQL_TYPE_NEWDATE:
      case MYSQL_TYPE_BIT:
      case MYSQL_TYPE_TIMESTAMP2:
      case MYSQL_TYPE_DATETIME2:
      case MYSQL_TYPE_TIME2:
      case MYSQL_TYPE_JSON:
      case MYSQL_TYPE_ENUM:
      case MYSQL_TYPE_SET:
      case MYSQL_TYPE_GEOMETRY:
      case MYSQL_TYPE_NULL:
        return MYSQL_TYPE_VARCHAR;
      case MYSQL_TYPE_TINY_BLOB:
      case MYSQL_TYPE_BLOB:
      case MYSQL_TYPE_MEDIUM_BLOB:
      case MYSQL_TYPE_LONG_BLOB:
        return string_field_type(max_bytes);
      default:
        DBUG_ASSERT(false);
        return MYSQL_TYPE_NULL;
    }
  }


  /// Item constructor for general use.
  Item();

  /**
    Constructor used by Item_field, Item_ref & aggregate functions.
    Used for duplicating lists in processing queries with temporary tables.

    Also used for Item_cond_and/Item_cond_or for creating top AND/OR structure
    of WHERE clause to protect it of optimisation changes in prepared statements
  */
  Item(THD *thd, Item *item);

  /**
    Parse-time context-independent constructor.

    This constructor and caller constructors of child classes must not
    access/change thd->lex (including thd->lex->current_select(),
    thd->m_parser_state etc structures).

    If we need to finalize the construction of the object, then we move
    all context-sensitive code to the itemize() virtual function.

    The POS parameter marks this constructor and other context-independent
    constructors of child classes for easy recognition/separation from other
    (context-dependent) constructors.
  */
  explicit Item(const POS &);

  virtual ~Item()
  {
#ifdef EXTRA_DEBUG
    item_name.set(0);
#endif
  }		/*lint -e1509 */

private:
  /*
    Hide the contextualize*() functions: call/override the itemize()
    in Item class tree instead.

    Note: contextualize_() is an intermediate function. Remove it together
    with Parse_tree_node::contextualize_().
  */
  bool contextualize(Parse_context *) override { DBUG_ASSERT(0); return true; }
  bool contextualize_(Parse_context *) override{ DBUG_ASSERT(0); return true; }

protected:
  /**
    Helper function to skip itemize() for grammar-allocated items

    @param [out] res    pointer to "this"

    @retval true        can skip itemize()
    @retval false       can't skip: the item is allocated directly by the parser
  */
  bool skip_itemize(Item **res)
  {
    *res= this;
    return !is_parser_item;
  }

  /*
    Checks if the function should return binary result based on the items
    provided as parameter.
    Function should only be used by Item_bit_func*

    @param      a item to check
    @param      b item to check, may be nullptr

    @returns true if binary result.
   */
  static bool bit_func_returns_binary(const Item *a, const Item *b);
public:
  /**
    The same as contextualize()/contextualize_() but with additional parameter
    
    This function finalize the construction of Item objects (see the Item(POS)
    constructor): we can access/change parser contexts from the itemize()
    function.

    @param        pc    current parse context
    @param  [out] res   pointer to "this" or to a newly allocated
                        replacement object to use in the Item tree instead

    @retval false       success
    @retval true        syntax/OOM/etc error
  */
  virtual bool itemize(Parse_context *pc, Item **res);

  void rename(char *new_name);
  void init_make_field(Send_field *tmp_field,enum enum_field_types type);
  virtual void cleanup();
  virtual void make_field(Send_field *field);
  virtual Field *make_string_field(TABLE *table);
  virtual bool fix_fields(THD *, Item **);
  /**
    Fix after tables have been moved from one select_lex level to the parent
    level, e.g by semijoin conversion.
    Basically re-calculate all attributes dependent on the tables.

    @param parent_select  select_lex that tables are moved to.
    @param removed_select select_lex that tables are moved away from,
                          child of parent_select.
  */
  virtual void
  fix_after_pullout(SELECT_LEX *parent_select MY_ATTRIBUTE((unused)),
                    SELECT_LEX *removed_select MY_ATTRIBUTE((unused)))
  {};
  /*
    should be used in case where we are sure that we do not need
    complete fix_fields() procedure.
  */
  inline void quick_fix_field() { fixed= 1; }

protected:
  /**
    Helper function which does all of the work for
    save_in_field(Field*, bool), except some error checking common to
    all subclasses, which is performed by save_in_field() itself.

    Subclasses that need to specialize the behaviour of
    save_in_field(), should override this function instead of
    save_in_field().

    @param[in,out] field  the field to save the item into
    @param no_conversions whether or not to allow conversions of the value

    @return the status from saving into the field
      @retval TYPE_OK    item saved without any errors or warnings
      @retval != TYPE_OK there were errors or warnings when saving the item
  */
  virtual type_conversion_status save_in_field_inner(Field *field,
                                                     bool no_conversions);
public:
  /**
    Save the item into a field but do not emit any warnings.

    @param field         field to save the item into
    @param no_conversions whether or not to allow conversions of the value

    @return the status from saving into the field
      @retval TYPE_OK    item saved without any issues
      @retval != TYPE_OK there were issues saving the item
  */
  type_conversion_status save_in_field_no_warnings(Field *field,
                                                   bool no_conversions);
  /**
    Save a temporal value in packed longlong format into a Field.
    Used in optimizer.

    Subclasses that need to specialize this function, should override
    save_in_field_inner().

    @param[in,out] field  the field to save the item into
    @param no_conversions whether or not to allow conversions of the value

    @return the status from saving into the field
      @retval TYPE_OK    item saved without any errors or warnings
      @retval != TYPE_OK there were errors or warnings when saving the item
  */
  type_conversion_status save_in_field(Field *field, bool no_conversions);

  virtual void save_org_in_field(Field *field)
  { save_in_field(field, true); }

  virtual bool send(Protocol *protocol, String *str);
  bool evaluate(THD *thd, String *str);
  virtual bool eq(const Item *, bool binary_cmp) const;
  virtual Item_result result_type() const { return REAL_RESULT; }
  /**
    Result type when an item appear in a numeric context.
    See Field::numeric_context_result_type() for more comments.
  */
  virtual enum Item_result numeric_context_result_type() const
  {
    if (is_temporal())
      return decimals ? DECIMAL_RESULT : INT_RESULT;
    if (result_type() == STRING_RESULT)
      return REAL_RESULT; 
    return result_type();
  }
  /**
    Similar to result_type() but makes DATE, DATETIME, TIMESTAMP
    pretend to be numbers rather than strings.
  */
  inline enum Item_result temporal_with_date_as_number_result_type() const
  {
    return is_temporal_with_date() ?
           (decimals ? DECIMAL_RESULT : INT_RESULT) : result_type();
  }

  /// Retrieve the derived data type of the Item.
  inline enum_field_types data_type() const
  {
    return static_cast<enum_field_types>(m_data_type);
  }

  /**
    Set the data type of the current Item. It is however recommended to
    use one of the type-specific setters if possible.

    @param data_type The data type of this Item.
  */
  inline void set_data_type(enum_field_types data_type)
  {
    m_data_type= static_cast<uint8>(data_type);
  }

  /**
    Set the data type of the Item to be longlong.
    Maximum display width is set to be the maximum of a 64-bit integer,
    but it may be adjusted later. The unsigned property is not affected.
  */
  inline void set_data_type_longlong()
  {
    set_data_type(MYSQL_TYPE_LONGLONG);
    collation.set_numeric();
    fix_char_length(21);
  }

  /**
    Set the data type of the Item to be decimal.
    The unsigned property must have been set before calling this function.

    @param precision Number of digits of precision
    @param dec       Number of digits after decimal point.
  */
  inline void set_data_type_decimal(uint8 precision, uint8 dec)
  {
    set_data_type(MYSQL_TYPE_NEWDECIMAL);
    collation.set_numeric();
    decimals= dec;
    fix_char_length(
      my_decimal_precision_to_length_no_truncation(precision, dec,
                                                   unsigned_flag));
  }

  /// Set the data type of the Item to be double precision floating point.
  inline void set_data_type_double()
  {
    set_data_type(MYSQL_TYPE_DOUBLE);
    decimals= NOT_FIXED_DEC;
    max_length= float_length(decimals);
    collation.set_numeric();
  }

  /// Initialize an Item to be of VARCHAR type, other properties undetermined.
  inline void set_data_type_string_init()
  {
    set_data_type(MYSQL_TYPE_VARCHAR);
    decimals= NOT_FIXED_DEC;
  }

  /**
    Set the Item to be variable length string. Actual type is determined from
    maximum string size. Collation must have been set before calling function.

    @param max_l  Maximum number of characters in string
  */
  inline void set_data_type_string(uint32 max_l)
  {
    max_length= max_l * collation.collation->mbmaxlen;
    if (max_length < 65536)
      set_data_type(MYSQL_TYPE_VARCHAR);
    else if (max_length < 16777216)
      set_data_type(MYSQL_TYPE_MEDIUM_BLOB);
    else
      set_data_type(MYSQL_TYPE_LONG_BLOB);
  }

  /**
    Set the Item to be variable length string. Like function above, but with
    larger string length precision.

    @param max_char_length_arg  Maximum number of characters in string
  */
  inline void set_data_type_string(ulonglong max_char_length_arg)
  {
    ulonglong max_result_length= max_char_length_arg *
                                 collation.collation->mbmaxlen;
    if (max_result_length >= MAX_BLOB_WIDTH)
    {
      max_result_length= MAX_BLOB_WIDTH;
      maybe_null= true;
    }
    set_data_type_string(uint32(max_result_length/collation.collation->mbmaxlen));
  }

  /**
    Set the Item to be variable length string. Like function above, but will
    also set character set and collation.

    @param max_l  Maximum number of characters in string
    @param cs     Pointer to character set and collation struct
  */
  inline void set_data_type_string(uint32 max_l, const CHARSET_INFO *cs)
  {
    collation.collation= cs;
    set_data_type_string(max_l);
  }

  /**
    Set the Item to be variable length string. Like function above, but will
    also set full collation information.

    @param max_l  Maximum number of characters in string
    @param coll   Ref to collation data, including derivation and repertoire
  */
  inline void set_data_type_string(uint32 max_l, const DTCollation &coll)
  {
    collation.set(coll);
    set_data_type_string(max_l);
  }

  /**
    Set the Item to be fixed length string. Collation must have been set
    before calling function.

    @param max_l Number of characters in string
  */
  inline void set_data_type_char(uint32 max_l)
  {
    max_length= max_l * collation.collation->mbmaxlen;
    DBUG_ASSERT(max_length < 65536);
    set_data_type(MYSQL_TYPE_STRING);
  }

  /**
    Set the Item to be fixed length string. Like function above, but will
    also set character set and collation.

    @param max_l  Maximum number of characters in string
    @param cs     Pointer to character set and collation struct
  */
  inline void set_data_type_char(uint32 max_l, const CHARSET_INFO *cs)
  {
    collation.collation= cs;
    set_data_type_char(max_l);
  }

  /**
    Set the Item to be of BLOB type.

    @param max_l Maximum number of bytes in data type
  */
  inline void set_data_type_blob(uint32 max_l)
  {
    set_data_type(MYSQL_TYPE_LONG_BLOB);
    max_length= max_l;
  }

  /// Set all type properties for Item of DATE type.
  inline void set_data_type_date()
  {
    set_data_type(MYSQL_TYPE_DATE);
    collation.set_numeric();
    decimals= 0;
    max_length= MAX_DATE_WIDTH;
  }

  /**
    Set all type properties for Item of TIME type.

    @param fsp Fractional seconds precision
  */
  inline void set_data_type_time(uint8 fsp)
  {
    set_data_type(MYSQL_TYPE_TIME);
    collation.set_numeric();
    decimals= fsp;
    max_length= MAX_TIME_WIDTH + fsp + (fsp > 0 ? 1 : 0);
  }

  /**
    Set all properties for Item of DATETIME type.

    @param fsp Fractional seconds precision
  */
  inline void set_data_type_datetime(uint8 fsp)
  {
    set_data_type(MYSQL_TYPE_DATETIME);
    collation.set_numeric();
    decimals= fsp;
    max_length= MAX_DATETIME_WIDTH + fsp + (fsp > 0 ? 1 : 0);
  }

  /**
    Set all properties for Item of TIMESTAMP type.

    @param fsp Fractional seconds precision
  */
  inline void set_data_type_timestamp(uint8 fsp)
  {
    set_data_type(MYSQL_TYPE_TIMESTAMP);
    collation.set_numeric();
    decimals= fsp;
    max_length= MAX_DATE_WIDTH + fsp + (fsp > 0 ? 1 : 0);
  }

  /**
    Set type information of Item from "result" information.
    For String types, type is set based on maximum string size.
    For other types, the associated type with the largest precision is set.

    @param result Either Integer, Decimal, Double or String
    @param length Maximum string size, used only for String result.
  */
  void set_data_type_from_result(Item_result result, uint32 length)
  {
    switch (result)
    {
    case INT_RESULT:
      set_data_type(MYSQL_TYPE_LONGLONG);
      break;
    case DECIMAL_RESULT:
      set_data_type(MYSQL_TYPE_NEWDECIMAL);
      break;
    case REAL_RESULT:
      set_data_type(MYSQL_TYPE_DOUBLE);
      break;
    case STRING_RESULT:
      set_data_type_string(length);
      break;
    case ROW_RESULT:
    case INVALID_RESULT:
    default:
      DBUG_ASSERT(false);
      break;
    }
  }

  /**
    Set data type properties of the item from the properties of another item.

    @param item Item to set data type properties from.
  */
  inline void set_data_type_from_item(Item *item)
  {
    set_data_type(item->data_type());
    collation= item->collation;
    max_length= item->max_length;
    decimals= item->decimals;
    unsigned_flag= item->unsigned_flag;
  }


  /**
    Determine correct string field type, based on string length

    @param max_bytes Maximum string size, in number of bytes @todo
  */
  static enum_field_types string_field_type(uint32 max_bytes)
  {
    if (max_bytes >= 16777216)
      return MYSQL_TYPE_LONG_BLOB;
    else if (max_bytes >= 65536)
      return MYSQL_TYPE_MEDIUM_BLOB;
    else
      return MYSQL_TYPE_VARCHAR;
  }

  virtual Item_result cast_to_int_type() const { return result_type(); }
  virtual enum Type type() const =0;

  void aggregate_type(Bounds_checked_array<Item *>items);

  /*
    Return information about function monotonicity. See comment for
    enum_monotonicity_info for details. This function can only be called
    after fix_fields() call.
  */
  virtual enum_monotonicity_info get_monotonicity_info() const
  { return NON_MONOTONIC; }

  /*
    Convert "func_arg $CMP$ const" half-interval into "FUNC(func_arg) $CMP2$ const2"

    SYNOPSIS
      val_int_endpoint()
        left_endp  FALSE  <=> The interval is "x < const" or "x <= const"
                   TRUE   <=> The interval is "x > const" or "x >= const"

        incl_endp  IN   FALSE <=> the comparison is '<' or '>'
                        TRUE  <=> the comparison is '<=' or '>='
                   OUT  The same but for the "F(x) $CMP$ F(const)" comparison

    DESCRIPTION
      This function is defined only for unary monotonic functions. The caller
      supplies the source half-interval

         x $CMP$ const

      The value of const is supplied implicitly as the value this item's
      argument, the form of $CMP$ comparison is specified through the
      function's arguments. The calle returns the result interval
         
         F(x) $CMP2$ F(const)
      
      passing back F(const) as the return value, and the form of $CMP2$ 
      through the out parameter. NULL values are assumed to be comparable and
      be less than any non-NULL values.

    RETURN
      The output range bound, which equal to the value of val_int()
        - If the value of the function is NULL then the bound is the 
          smallest possible value of LLONG_MIN 
  */
  virtual longlong val_int_endpoint(bool left_endp MY_ATTRIBUTE((unused)),
                                    bool *incl_endp MY_ATTRIBUTE((unused)))
  { DBUG_ASSERT(0); return 0; }


  /* valXXX methods must return NULL or 0 or 0.0 if null_value is set. */
  /*
    Return double precision floating point representation of item.

    SYNOPSIS
      val_real()

    RETURN
      In case of NULL value return 0.0 and set null_value flag to TRUE.
      If value is not null null_value flag will be reset to FALSE.
  */
  virtual double val_real()=0;
  /*
    Return integer representation of item.

    SYNOPSIS
      val_int()

    RETURN
      In case of NULL value return 0 and set null_value flag to TRUE.
      If value is not null null_value flag will be reset to FALSE.
  */
  virtual longlong val_int()=0;
  /**
    Return date value of item in packed longlong format.
  */
  virtual longlong val_date_temporal();
  /**
    Return time value of item in packed longlong format.
  */
  virtual longlong val_time_temporal();
  /**
    Return date or time value of item in packed longlong format,
    depending on item field type.
  */
  longlong val_temporal_by_field_type()
  {
    if (data_type() == MYSQL_TYPE_TIME)
      return val_time_temporal();
    DBUG_ASSERT(is_temporal_with_date());
    return val_date_temporal();
  }
  /**
    Get date or time value in packed longlong format.
    Before conversion from MYSQL_TIME to packed format,
    the MYSQL_TIME value is rounded to "dec" fractional digits.
  */
  longlong val_temporal_with_round(enum_field_types type, uint8 dec);

  /*
    This is just a shortcut to avoid the cast. You should still use
    unsigned_flag to check the sign of the item.
  */
  inline ulonglong val_uint() { return (ulonglong) val_int(); }
  /*
    Return string representation of this item object.

    SYNOPSIS
      val_str()
      str   an allocated buffer this or any nested Item object can use to
            store return value of this method.

    NOTE
      Buffer passed via argument  should only be used if the item itself
      doesn't have an own String buffer. In case when the item maintains
      it's own string buffer, it's preferable to return it instead to
      minimize number of mallocs/memcpys.
      The caller of this method can modify returned string, but only in case
      when it was allocated on heap, (is_alloced() is true).  This allows
      the caller to efficiently use a buffer allocated by a child without
      having to allocate a buffer of it's own. The buffer, given to
      val_str() as argument, belongs to the caller and is later used by the
      caller at it's own choosing.
      A few implications from the above:
      - unless you return a string object which only points to your buffer
        but doesn't manages it you should be ready that it will be
        modified.
      - even for not allocated strings (is_alloced() == false) the caller
        can change charset (see Item_func_{typecast/binary}. XXX: is this
        a bug?
      - still you should try to minimize data copying and return internal
        object whenever possible.

    RETURN
      In case of NULL value or error, return error_str() as this function will
      check if the return value may be null, and it will either set null_value
      to true and return nullptr or to false and it will return empty string.
      If value is not null set null_value flag to false before returning it.
  */
  virtual String *val_str(String *str)=0;

  /*
    Returns string representation of this item in ASCII format.

    SYNOPSIS
      val_str_ascii()
      str - similar to val_str();

    NOTE
      This method is introduced for performance optimization purposes.

      1. val_str() result of some Items in string context
      depends on @@character_set_results.
      @@character_set_results can be set to a "real multibyte" character
      set like UCS2, UTF16, UTF32. (We'll use only UTF32 in the examples
      below for convenience.)

      So the default string result of such functions
      in these circumstances is real multi-byte character set, like UTF32.

      For example, all numbers in string context
      return result in @@character_set_results:

      SELECT CONCAT(20010101); -> UTF32

      We do sprintf() first (to get ASCII representation)
      and then convert to UTF32;
      
      So these kind "data sources" can use ASCII representation
      internally, but return multi-byte data only because
      @@character_set_results wants so.
      Therefore, conversion from ASCII to UTF32 is applied internally.


      2. Some other functions need in fact ASCII input.

      For example,
        inet_aton(), GeometryFromText(), Convert_TZ(), GET_FORMAT().

      Similar, fields of certain type, like DATE, TIME,
      when you insert string data into them, expect in fact ASCII input.
      If they get non-ASCII input, for example UTF32, they
      convert input from UTF32 to ASCII, and then use ASCII
      representation to do further processing.


      3. Now imagine we pass result of a data source of the first type
         to a data destination of the second type.

      What happens:
        a. data source converts data from ASCII to UTF32, because
           @@character_set_results wants so and passes the result to
           data destination.
        b. data destination gets UTF32 string.
        c. data destination converts UTF32 string to ASCII,
           because it needs ASCII representation to be able to handle data
           correctly.

      As a result we get two steps of unnecessary conversion:
      From ASCII to UTF32, then from UTF32 to ASCII.

      A better way to handle these situations is to pass ASCII
      representation directly from the source to the destination.

      This is why val_str_ascii() introduced.

    RETURN
      Similar to val_str()
  */
  virtual String *val_str_ascii(String *str);
  
  /*
    Return decimal representation of item with fixed point.

    SYNOPSIS
      val_decimal()
      decimal_buffer  buffer which can be used by Item for returning value
                      (but can be not)

    NOTE
      Returned value should not be changed if it is not the same which was
      passed via argument.

    RETURN
      Return pointer on my_decimal (it can be other then passed via argument)
        if value is not NULL (null_value flag will be reset to FALSE).
      In case of NULL value it return 0 pointer and set null_value flag
        to TRUE.
  */
  virtual my_decimal *val_decimal(my_decimal *decimal_buffer)= 0;
  /*
    Return boolean value of item.

    RETURN
      FALSE value is false or NULL
      TRUE value is true (not equal to 0)
  */
  virtual bool val_bool();
  /**
    Evaluate an XPath function.

    @details
    The SQL interface consists of extractvalue() and updatexml().
    Both of them ensure that none of the arguments are NULL, before
    invoking parse_xml(). So, we should never see any NULL SQL input
    and never return NULL from the overridden member functions in
    item_xmlfunc.cc.

    @see Item_xml_str_func::parse_xpath()
    @see Item_func_xml_update::val_str()

    @param nodeset   the nodeset
    @return the extracted nodeset (never NULL in overridden member functions)
      @retval NULL   always in the base member function
  */
  virtual String *val_nodeset(String *nodeset MY_ATTRIBUTE((unused)))
  {
    /*
      If the first argument of updatexml() is not an XML document,
      this base member function may be called. Because we will return
      nullptr here, updatexml() will evaluate to NULL.

      Outside Item_func_xml_update::val_str(), only the overridden
      member functions defined in item_xmlfunc.cc should be invoked,
      by Item_xml_str_func::parse_xpath().
    */
    return nullptr;
  }

  /**
    Get a JSON value from an Item.

    All subclasses that can return a JSON value, should override this
    function. The function in the base class is not expected to be
    called. If it is called, it most likely means that some subclass
    is missing an override of val_json().

    @param[in,out] result The resulting Json_wrapper.

    @return false if successful, true on failure
  */
  /* purecov: begin deadcode */
  virtual bool val_json(Json_wrapper *result MY_ATTRIBUTE((unused)))
  {
    DBUG_ABORT();
    my_error(ER_NOT_SUPPORTED_YET, MYF(0), "item type for JSON");
    return error_json();
  }
  /* purecov: end */

  /**
    Calculate the filter contribution that is relevant for table
    'filter_for_table' for this item.

    @param filter_for_table  The table we are calculating filter effect for
    @param read_tables       Tables earlier in the join sequence.
                             Predicates for table 'filter_for_table' that
                             rely on values from these tables can be part of
                             the filter effect.
    @param fields_to_ignore  Fields in 'filter_for_table' that should not
                             be part of the filter calculation. The filtering
                             effect of these fields is already part of the
                             calculation somehow (e.g. because there is a
                             predicate "col = <const>", and the optimizer
                             has decided to do ref access on 'col').
    @param rows_in_table     The number of rows in table 'filter_for_table'

    @return                  the filtering effect (between 0 and 1) this
                             Item contributes with.
  */
  virtual float
  get_filtering_effect(table_map filter_for_table MY_ATTRIBUTE((unused)),
                       table_map read_tables MY_ATTRIBUTE((unused)),
                       const MY_BITMAP
                       *fields_to_ignore MY_ATTRIBUTE((unused)),
                       double rows_in_table MY_ATTRIBUTE((unused)))
  {
    // Filtering effect cannot be calculated for a table already read.
    DBUG_ASSERT((read_tables & filter_for_table) == 0);
    return COND_FILTER_ALLPASS;
  }


  /**
    Get the value to return from val_json() in case of errors.

    @see Item::error_bool

    @return The value val_json() should return, which is true.
  */
  bool error_json()
  {
    null_value= maybe_null;
    return true;
  }


protected:
  /* Helper functions, see item_sum.cc */
  String *val_string_from_real(String *str);
  String *val_string_from_int(String *str);
  String *val_string_from_decimal(String *str);
  String *val_string_from_date(String *str);
  String *val_string_from_datetime(String *str);
  String *val_string_from_time(String *str);
  my_decimal *val_decimal_from_real(my_decimal *decimal_value);
  my_decimal *val_decimal_from_int(my_decimal *decimal_value);
  my_decimal *val_decimal_from_string(my_decimal *decimal_value);
  my_decimal *val_decimal_from_date(my_decimal *decimal_value);
  my_decimal *val_decimal_from_time(my_decimal *decimal_value);
  longlong val_int_from_decimal();
  longlong val_int_from_date();
  longlong val_int_from_time();
  longlong val_int_from_datetime();
  double val_real_from_decimal();


  /**
    Get the value to return from val_bool() in case of errors.

    This function is called from val_bool() when an error has occurred
    and we need to return something to abort evaluation of the
    item. The expected pattern in val_bool() is

      if (@<error condition@>)
      {
        my_error(...)
        return error_bool();
      }

    @return The value val_bool() should return.
  */
  bool error_bool()
  {
    null_value= maybe_null;
    return false;
  }


  /**
    Get the value to return from val_int() in case of errors.

    @see Item::error_bool

    @return The value val_int() should return.
  */
  int error_int()
  {
    null_value= maybe_null;
    return 0;
  }


  /**
    Get the value to return from val_real() in case of errors.

    @see Item::error_bool

    @return The value val_real() should return.
  */
  double error_real()
  {
    null_value= maybe_null;
    return 0.0;
  }


  /**
    Get the value to return from val_str() in case of errors.

    @see Item::error_bool

    @return The value val_str() should return.
  */
  String *error_str()
  {
    null_value= maybe_null;
    return null_value ? NULL : make_empty_result();
  }


  /**
    Convert val_str() to date in MYSQL_TIME
  */
  bool get_date_from_string(MYSQL_TIME *ltime, my_time_flags_t flags);
  /**
    Convert val_real() to date in MYSQL_TIME
  */
  bool get_date_from_real(MYSQL_TIME *ltime, my_time_flags_t flags);
  /**
    Convert val_decimal() to date in MYSQL_TIME
  */
  bool get_date_from_decimal(MYSQL_TIME *ltime, my_time_flags_t flags);
  /**
    Convert val_int() to date in MYSQL_TIME
  */
  bool get_date_from_int(MYSQL_TIME *ltime, my_time_flags_t flags);
  /**
    Convert get_time() from time to date in MYSQL_TIME
  */
  bool get_date_from_time(MYSQL_TIME *ltime);

  /**
    Convert a numeric type to date
  */
  bool get_date_from_numeric(MYSQL_TIME *ltime, my_time_flags_t fuzzydate);

  /**
    Convert a non-temporal type to date
  */
  bool get_date_from_non_temporal(MYSQL_TIME *ltime, my_time_flags_t fuzzydate);

  /**
    Convert val_str() to time in MYSQL_TIME
  */
  bool get_time_from_string(MYSQL_TIME *ltime);
  /**
    Convert val_real() to time in MYSQL_TIME
  */
  bool get_time_from_real(MYSQL_TIME *ltime);
  /**
    Convert val_decimal() to time in MYSQL_TIME
  */
  bool get_time_from_decimal(MYSQL_TIME *ltime);
  /**
    Convert val_int() to time in MYSQL_TIME
  */
  bool get_time_from_int(MYSQL_TIME *ltime);
  /**
    Convert date to time
  */
  bool get_time_from_date(MYSQL_TIME *ltime);
  /**
    Convert datetime to time
  */
  bool get_time_from_datetime(MYSQL_TIME *ltime);

  /**
    Convert a numeric type to time
  */
  bool get_time_from_numeric(MYSQL_TIME *ltime);

  /**
    Convert a non-temporal type to time
  */
  bool get_time_from_non_temporal(MYSQL_TIME *ltime);

public:

  type_conversion_status save_time_in_field(Field *field);
  type_conversion_status save_date_in_field(Field *field);
  type_conversion_status save_str_value_in_field(Field *field, String *result);

  virtual Field *get_tmp_table_field() { return 0; }
  /* This is also used to create fields in CREATE ... SELECT: */
  virtual Field *tmp_table_field(TABLE*) { return 0; }
  virtual const char *full_name() const
  {
    return item_name.is_set() ? item_name.ptr() : "???";
  }

  /*
    *result* family of methods is analog of *val* family (see above) but
    return value of result_field of item if it is present. If Item have not
    result field, it return val(). This methods set null_value flag in same
    way as *val* methods do it.
  */
  virtual double  val_result() { return val_real(); }
  virtual longlong val_int_result() { return val_int(); }
  /**
    Get time value in packed longlong format. NULL is converted to 0.
  */
  virtual longlong val_time_temporal_result() { return val_time_temporal(); }
  /**
    Get date value in packed longlong format. NULL is converted to 0.
  */
  virtual longlong val_date_temporal_result() { return val_date_temporal(); }
  virtual String *str_result(String* tmp) { return val_str(tmp); }
  virtual my_decimal *val_decimal_result(my_decimal *val)
  { return val_decimal(val); }
  virtual bool val_bool_result() { return val_bool(); }
  virtual bool is_null_result() { return is_null(); }

  /* bit map of tables used by item */
  virtual table_map used_tables() const { return (table_map) 0L; }
  /*
    Return table map of tables that can't be NULL tables (tables that are
    used in a context where if they would contain a NULL row generated
    by a LEFT or RIGHT join, the item would not be true).
    This expression is used on WHERE item to determinate if a LEFT JOIN can be
    converted to a normal join.
    Generally this function should return used_tables() if the function
    would return null if any of the arguments are null
    As this is only used in the beginning of optimization, the value don't
    have to be updated in update_used_tables()
  */
  virtual table_map not_null_tables() const { return used_tables(); }
  /*
    Returns true if this is a simple constant item like an integer, not
    a constant expression. Used in the optimizer to propagate basic constants.
  */
  virtual bool basic_const_item() const { return 0; }
  /**
    @return cloned item if it is constant
      @retval nullptr  if this is not const
  */
  virtual Item *clone_item() const { return nullptr; }
  virtual cond_result eq_cmp_result() const { return COND_OK; }
  inline uint float_length(uint decimals_par) const
  { return decimals != NOT_FIXED_DEC ? (DBL_DIG+2+decimals_par) : DBL_DIG+8;}
  virtual uint decimal_precision() const;
  inline int decimal_int_part() const
  { return my_decimal_int_part(decimal_precision(), decimals); }
  /**
    TIME precision of the item: 0..6
  */
  virtual uint time_precision();
  /**
    DATETIME precision of the item: 0..6
  */
  virtual uint datetime_precision();
  /* 
    Returns true if this is constant (during query execution, i.e. its value
    will not change until next fix_fields) and its value is known.
    When the default implementation of used_tables() is effective, this
    function will always return true (because used_tables() is empty).
  */
  virtual bool const_item() const
  {
    if (used_tables() == 0)
      return can_be_evaluated_now();
    return false;
  }
  /* 
    Returns true if this is constant but its value may be not known yet.
    (Can be used for parameters of prep. stmts or of stored procedures.)
  */
  virtual bool const_during_execution() const 
  { return (used_tables() & ~PARAM_TABLE_BIT) == 0; }

  /**
    This method is used for to:
      - to generate a view definition query (SELECT-statement);
      - to generate a SQL-query for EXPLAIN EXTENDED;
      - to generate a SQL-query to be shown in INFORMATION_SCHEMA;
      - to generate a SQL-query that looks like a prepared statement for query_rewrite
      - debug.

    For more information about view definition query, INFORMATION_SCHEMA
    query and why they should be generated from the Item-tree, @see
    mysql_register_view().
  */
  virtual void print(String *str, enum_query_type)
  {
    str->append(full_name());
  }

  void print_item_w_name(String *, enum_query_type query_type);
  /**
     Prints the item when it's part of ORDER BY and GROUP BY.
     @param  str            String to print to
     @param  query_type     How to format the item
     @param  used_alias     Whether item was referenced with alias.
  */
  void print_for_order(String *str, enum_query_type query_type,
                       bool used_alias);

  virtual void update_used_tables() {}

  virtual void split_sum_func(THD*, Ref_item_array,
                              List<Item>&) {}
  /* Called for items that really have to be split */
  void split_sum_func2(THD *thd, Ref_item_array ref_item_array,
                       List<Item> &fields,
                       Item **ref, bool skip_registered);
  virtual bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate)= 0;
  virtual bool get_time(MYSQL_TIME *ltime)= 0;
  /**
    Get timestamp in "struct timeval" format.
    @retval  false on success
    @retval  true  on error
  */
  virtual bool get_timeval(struct timeval *tm, int *warnings);
  virtual bool get_date_result(MYSQL_TIME *ltime, my_time_flags_t fuzzydate)
  { return get_date(ltime,fuzzydate); }
  /*
    The method allows to determine nullness of a complex expression 
    without fully evaluating it, instead of calling val/result*() then 
    checking null_value. Used in Item_func_isnull/Item_func_isnotnull
    and Item_sum_count/Item_sum_count_distinct.
    Any new item which can be NULL must implement this method.
  */
  virtual bool is_null() { return 0; }

  /// Make sure the null_value member has a correct value.
  void update_null_value();

  /*
    Inform the item that there will be no distinction between its result
    being FALSE or NULL.

    NOTE
      This function will be called for eg. Items that are top-level AND-parts
      of the WHERE clause. Items implementing this function (currently
      Item_cond_and and subquery-related item) enable special optimizations
      when they are "top level".
  */
  virtual void top_level_item() {}
  /*
    set field of temporary table for Item which can be switched on temporary
    table during query processing (grouping and so on)
  */
  virtual void set_result_field(Field*) {}
  virtual bool is_result_field() const { return false; }
  virtual bool is_bool_func() const { return false; }
  virtual void save_in_result_field(bool no_conversions MY_ATTRIBUTE((unused)))
  {}
  /*
    Set value of aggregate function in case of no rows for grouping were found.
    Also used for subqueries with outer references in SELECT list.
  */
  virtual void no_rows_in_result() {}
  virtual Item *copy_or_same(THD*) { return this; }
  virtual Item *copy_andor_structure(THD*) { return this; }
  virtual Item *real_item() { return this; }
  virtual Item *substitutional_item()
  {
    return  runtime_item ? real_item() : this;
  }
  virtual void set_runtime_created() { runtime_item= true; }
  virtual Item *get_tmp_table_item(THD *thd) { return copy_or_same(thd); }

  static const CHARSET_INFO *default_charset();
  virtual const CHARSET_INFO *compare_collation() const { return nullptr; }

  /*
    For backward compatibility, to make numeric
    data types return "binary" charset in client-side metadata.
  */
  virtual const CHARSET_INFO *charset_for_protocol() const
  {
    return result_type() == STRING_RESULT ? collation.collation :
                                            &my_charset_bin;
  };

  /**
    Traverses a tree of Items in prefix and/or postfix order.
    Optionally walks into subqueries.

    @param processor   processor function to be invoked per item
                       returns true to abort traversal, false to continue
    @param walk        controls how to traverse the item tree
                       WALK_PREFIX:  call processor before invoking children
                       WALK_POSTFIX: call processor after invoking children
                       WALK_SUBQUERY go down into subqueries
                       walk values are bit-coded and may be combined.
                       Omitting both WALK_PREFIX and WALK_POSTFIX is undefined
                       behaviour.
    @param arg         Optional pointer to a walk-specific object

    @retval      false walk succeeded
    @retval      true  walk aborted
                       by agreement, an error may have been reported
  */

  virtual bool walk(Item_processor processor,
                    enum_walk walk MY_ATTRIBUTE((unused)),
                    uchar *arg)
  {
    return (this->*processor)(arg);
  }

  /**
    Perform a generic transformation of the Item tree, by adding zero or
    more additional Item objects to it.

    @param transformer  Transformer function
    @param[in,out] arg  Pointer to struct used by transformer function

    @returns Returned item tree after transformation, NULL if error

    @details

    Transformation is performed as follows:

    transform()
    {
      transform children if any;
      return this->*some_transformer(...);
    }

    Note that unlike Item::compile(), transform() does not support an analyzer
    function, ie. all children are unconditionally invoked.

    @todo Let compile() handle all transformations during optimization, and
          let transform() handle transformations during preparation only.
          Then there would be no need to call change_item_tree() during
          transformation.
  */
  virtual Item* transform(Item_transformer transformer, uchar *arg);

  /**
    Perform a generic "compilation" of the Item tree, ie transform the Item tree
    by adding zero or more Item objects to it.

    @param analyzer      Analyzer function, see details section
    @param[in,out] arg_p Pointer to struct used by analyzer function
    @param transformer   Transformer function, see details section
    @param[in,out] arg_t Pointer to struct used by transformer function

    @returns Returned item tree after transformation, NULL if error

    @details

    The process of this transformation is assumed to be as follows:

    compile()
    { 
      if (this->*some_analyzer(...))
      {
        compile children if any;
        return this->*some_transformer(...);
      }
      else
        return this;
    }

    i.e. analysis is performed top-down while transformation is done
    bottom-up. If no transformation is applied, the item is returned unchanged.
    A transformation error is indicated by returning a NULL pointer. Notice
    that the analyzer function should never cause an error.

    The function is supposed to be used during the optimization stage of
    query execution. All new allocations are recorded using
    THD::change_item_tree() so that they can be rolled back after execution.

    @todo Pass THD to compile() function, thus no need to use current_thd.
  */
  virtual Item* compile(Item_analyzer analyzer, uchar **arg_p,
                        Item_transformer transformer, uchar *arg_t)
  {
    if ((this->*analyzer) (arg_p))
      return ((this->*transformer) (arg_t));
    return this;
  }

   virtual void traverse_cond(Cond_traverser traverser,
                              void *arg, traverse_order)
   {
     (*traverser)(this, arg);
   }

  /*
    This is used to get the most recent version of any function in
    an item tree. The version is the version where a MySQL function
    was introduced in. So any function which is added should use
    this function and set the int_arg to maximum of the input data
    and their own version info.
  */
  virtual bool intro_version(uchar *) { return false; }

  /// cleanup() item if it is resolved ('fixed').
  bool cleanup_processor(uchar *)
  {
    if (fixed)
      cleanup();
    return false;
  }

  virtual bool collect_item_field_processor(uchar *) { return false; }

  /**
    Item::walk function. Set bit in table->tmp_set for all fields in
    table 'arg' that are referred to by the Item.
  */
  virtual bool add_field_to_set_processor(uchar*) { return false; }

  /// A processor to handle the select lex visitor framework.
  virtual bool visitor_processor(uchar *arg);

  /**
    Item::walk function. Set bit in table->cond_set for all fields of
    all tables that are referred to by the Item.
  */
  virtual bool add_field_to_cond_set_processor(uchar *) {return false; }

  /**
     Visitor interface for removing all column expressions (Item_field) in
     this expression tree from a bitmap. @see walk()

     @param arg  A MY_BITMAP* cast to unsigned char*, where the bits represent
                 Field::field_index values.
   */
  virtual bool remove_column_from_bitmap(uchar *arg MY_ATTRIBUTE((unused)))
  { return false; }
  virtual bool find_item_in_field_list_processor(uchar *) { return false; }
  virtual bool change_context_processor(uchar *) { return false; }
  virtual bool find_item_processor(uchar *arg) { return this == (void *) arg; }
  /**
    Mark underlying field in read or write map of a table.

    @param arg        Mark_field object
  */
  virtual bool mark_field_in_map(uchar *arg MY_ATTRIBUTE((unused)))
  { return false; }
protected:
  /**
    Helper function for mark_field_in_map(uchar *arg).

    @param mark_field Mark_field object
    @param field      Field to be marked for read/write
  */
  static inline bool mark_field_in_map(Mark_field *mark_field, Field* field)
  {
    TABLE *table= mark_field->table;
    if (table != NULL && table != field->table)
      return false;

    table= field->table;
    table->mark_column_used(table->in_use, field, mark_field->mark);

    return false;
  }
public:
  /**
    Return used table information for the specified query block (level).
    For a field that is resolved from this query block, return the table number.
    For a field that is resolved from a query block outer to the specified one,
    return OUTER_REF_TABLE_BIT

    @param[in,out] arg pointer to an instance of class Used_tables, which is
                       constructed with the query block as argument.
                       The used tables information is accumulated in the field
                       used_tables in this class. 

    @note This function is used to update used tables information after
          merging a query block (a subquery) with its parent.
  */
  virtual bool used_tables_for_level(uchar *arg MY_ATTRIBUTE((unused)))
  { return false; }
  /**
    Check privileges.

    @param thd   thread handle
  */
  virtual bool check_column_privileges(uchar *thd MY_ATTRIBUTE((unused)))
  { return false; }
  virtual bool inform_item_in_cond_of_tab(uchar *) { return false; }
  /**
     Clean up after removing the item from the item tree.

     @param arg Pointer to the SELECT_LEX from which the walk started, i.e.,
                the SELECT_LEX that contained the clause that was removed.
  */
  virtual bool clean_up_after_removal(uchar *arg MY_ATTRIBUTE((unused)))
  { return false; }

  /**
    Propagate components that use referenced columns from derived tables.
    Some columns from derived tables may be determined to be unused, but
    may actually reference other columns that are used. This function will
    return true for such columns when called with Item::walk(), which then
    means that this column can also be marked as used.
    @see also SELECT_LEX::delete_unused_merged_columns().
  */
  bool propagate_derived_used(uchar *) { return is_derived_used(); }

  /**
    Called by Item::walk() to set all the referenced items' derived_used flag.
  */
  bool propagate_set_derived_used(uchar *)
  {
    set_derived_used();
    return false;
  }

  /// @see Distinct_check::check_query()
  virtual bool aggregate_check_distinct(uchar *) { return false; }
  /// @see Group_check::check_query()
  virtual bool aggregate_check_group(uchar *) { return false; }
  /// @see Group_check::analyze_conjunct()
  virtual bool is_strong_side_column_not_in_fd(uchar *) { return false; }
  /// @see Group_check::is_in_fd_of_underlying()
  virtual bool is_column_not_in_fd(uchar *) { return false; }
  virtual Bool3 local_column(const SELECT_LEX*) const
  { return Bool3::false3(); }

  virtual bool cache_const_expr_analyzer(uchar **cache_item);
  Item *cache_const_expr_transformer(uchar *item);

  virtual bool equality_substitution_analyzer(uchar **) { return false; }

  virtual Item *equality_substitution_transformer(uchar *) { return this; }

  /**
    Check if a partition function is allowed.

    @return whether a partition function is not accepted

    @details
    check_partition_func_processor is used to check if a partition function
    uses an allowed function. An allowed function will always ensure that
    X=Y guarantees that also part_function(X)=part_function(Y) where X is
    a set of partition fields and so is Y. The problems comes mainly from
    character sets where two equal strings can be quite unequal. E.g. the
    german character for double s is equal to 2 s.

    The default is that an item is not allowed
    in a partition function. Allowed functions
    can never depend on server version, they cannot depend on anything
    related to the environment. They can also only depend on a set of
    fields in the table itself. They cannot depend on other tables and
    cannot contain any queries and cannot contain udf's or similar.
    If a new Item class is defined and it inherits from a class that is
    allowed in a partition function then it is very important to consider
    whether this should be inherited to the new class. If not the function
    below should be defined in the new Item class.

    The general behaviour is that most integer functions are allowed.
    If the partition function contains any multi-byte collations then
    the function check_part_func_fields will report an error on the
    partition function independent of what functions are used. So the
    only character sets allowed are single character collation and
    even for those only a limited set of functions are allowed. The
    problem with multi-byte collations is that almost every string
    function has the ability to change things such that two strings
    that are equal will not be equal after manipulated by a string
    function. E.g. two strings one contains a double s, there is a
    special german character that is equal to two s. Now assume a
    string function removes one character at this place, then in
    one the double s will be removed and in the other there will
    still be one s remaining and the strings are no longer equal
    and thus the partition function will not sort equal strings into
    the same partitions.

    So the check if a partition function is valid is two steps. First
    check that the field types are valid, next check that the partition
    function is valid. The current set of partition functions valid
    assumes that there are no multi-byte collations amongst the partition
    fields.
  */
  virtual bool check_partition_func_processor(uchar *) { return true;}
  virtual bool subst_argument_checker(uchar **arg)
  {
    if (*arg)
      *arg= NULL;
    return true;
  }
  virtual bool explain_subquery_checker(uchar**) { return true; }
  virtual Item *explain_subquery_propagator(uchar*) { return this; }

  virtual Item *equal_fields_propagator(uchar*) { return this; }
  virtual bool set_no_const_sub(uchar*) { return false; }
  virtual Item *replace_equal_field(uchar*) { return this; }
  /*
    Check if an expression value has allowed arguments, like DATE/DATETIME
    for date functions. Also used by partitioning code to reject
    timezone-dependent expressions in a (sub)partitioning function.
  */
  virtual bool check_valid_arguments_processor(uchar*) { return false; }

 /**
   Check if an expression/function is allowed for a virtual column

   @param[in,out] int_arg  An array of two integers. Used only for
   Item_field. The first cell passes the field's number in the table. The
   second cell is an out parameter containing the error code.

   @returns true if function is not accepted
  */
  virtual bool check_gcol_func_processor(uchar *int_arg MY_ATTRIBUTE((unused)))
  { return true; }

  /**
    Check if a generated expression depends on DEFAULT function.

    @returns false if the function is not DEFAULT(), otherwise true.
  */
  virtual bool check_gcol_depend_default_processor(uchar *) { return false; }

  /*
    For SP local variable returns pointer to Item representing its
    current value and pointer to current Item otherwise.
  */
  virtual Item *this_item() { return this; }
  virtual const Item *this_item() const { return this; }

  /*
    For SP local variable returns address of pointer to Item representing its
    current value and pointer passed via parameter otherwise.
  */
  virtual Item **this_item_addr(THD*, Item **addr_arg) { return addr_arg; }

  // Row emulation
  virtual uint cols() const { return 1; }
  virtual Item* element_index(uint) { return this; }
  virtual Item** addr(uint) { return 0; }
  virtual bool check_cols(uint c);
  // It is not row => null inside is impossible
  virtual bool null_inside() { return 0; }
  // used in row subselects to get value of elements
  virtual void bring_value() {}

  Field *tmp_table_field_from_field_type(TABLE *table, bool fixed_length);
  virtual Item_field *field_for_view_update() { return 0; }

  virtual Item *neg_transformer(THD*) { return NULL; }
  virtual Item *update_value_transformer(uchar*) { return this; }
  virtual Item *safe_charset_converter(const CHARSET_INFO *tocs);
  void delete_self()
  {
    cleanup();
    delete this;
  }

  /** @return whether the item is local to a stored procedure */
  virtual bool is_splocal() const { return false; }

  /*
    Return Settable_routine_parameter interface of the Item.  Return 0
    if this Item is not Settable_routine_parameter.
  */
  virtual Settable_routine_parameter *get_settable_routine_parameter()
  {
    return 0;
  }
  inline bool is_temporal_with_date() const
  {
    return is_temporal_type_with_date(data_type());
  }
  inline bool is_temporal_with_date_and_time() const
  {
    return is_temporal_type_with_date_and_time(data_type());
  }
  inline bool is_temporal_with_time() const
  {
    return is_temporal_type_with_time(data_type());
  }
  inline bool is_temporal() const
  {
    return is_temporal_type(data_type());
  }
  /**
    Check whether this and the given item has compatible comparison context.
    Used by the equality propagation. See Item_field::equal_fields_propagator.

    @return
      TRUE  if the context is the same or if fields could be
            compared as DATETIME values by the Arg_comparator.
      FALSE otherwise.
  */
  inline bool has_compatible_context(Item *item) const
  {
    /* Same context. */
    if (cmp_context == INVALID_RESULT || item->cmp_context == cmp_context)
      return TRUE;
    /* DATETIME comparison context. */
    if (is_temporal_with_date())
      return item->is_temporal_with_date() ||
             item->cmp_context == STRING_RESULT;
    if (item->is_temporal_with_date())
      return is_temporal_with_date() || cmp_context == STRING_RESULT;
    return FALSE;
  }
  virtual Field::geometry_type get_geometry_type() const
    { return Field::GEOM_GEOMETRY; };
  String *check_well_formed_result(String *str,
                                   bool send_error,
                                   bool truncate);
  bool eq_by_collation(Item *item, bool binary_cmp, const CHARSET_INFO *cs); 

  /*
    Test whether an expression is expensive to compute. Used during
    optimization to avoid computing expensive expressions during this
    phase. Also used to force temp tables when sorting on expensive
    functions.
    TODO:
    Normally we should have a method:
      cost Item::execution_cost(),
    where 'cost' is either 'double' or some structure of various cost
    parameters.
  */
  virtual bool is_expensive()
  {
    if (is_expensive_cache < 0)
      is_expensive_cache= walk(&Item::is_expensive_processor, WALK_POSTFIX,
                               NULL);
    return MY_TEST(is_expensive_cache);
  }
  virtual bool can_be_evaluated_now() const;

  /**
    @return maximum number of characters that this Item can store
    If Item is of string or blob type, return max string length in bytes
    divided by bytes per character, otherwise return max_length.
    @todo - check if collation for other types should have mbmaxlen = 1
  */
  uint32 max_char_length() const
  {
    if (result_type() == STRING_RESULT)
      return max_length / collation.collation->mbmaxlen;
    else
      return max_length;
  }

  inline void fix_char_length(uint32 max_char_length_arg)
  {
    max_length= char_to_byte_length_safe(max_char_length_arg,
                                         collation.collation->mbmaxlen);
  }

  /*
    Return TRUE if the item points to a column of an outer-joined table.
  */
  virtual bool is_outer_field() const { DBUG_ASSERT(fixed); return FALSE; }

  /**
     Check if an item either is a blob field, or will be represented as a BLOB
     field if a field is created based on this item.

     @retval TRUE  If a field based on this item will be a BLOB field,
     @retval FALSE Otherwise.
  */
  bool is_blob_field() const;

  /**
    Checks if this item or any of its decendents contains a subquery.
  */
  virtual bool has_subquery() const { return with_subselect; }
  virtual bool has_stored_program() const { return with_stored_program; }
  /// Whether this Item was created by the IN->EXISTS subquery transformation
  virtual bool created_by_in2exists() const { return false; }

  // @return true if an expression in select list of derived table is used
  bool is_derived_used() const { return derived_used; }

  void mark_subqueries_optimized_away()
  {
    if (has_subquery())
      walk(&Item::subq_opt_away_processor, WALK_POSTFIX, NULL);
  }

  /**
    Analyzer function for GC substitution. @see substitute_gc()
  */
  virtual bool gc_subst_analyzer(uchar**) { return false; }
  /**
    Transformer function for GC substitution. @see substitute_gc()
  */
  virtual Item *gc_subst_transformer(uchar*) { return this; }
  /**
    Check if this item is of a type that is eligible for GC
    substitution. All items that belong to subclasses of Item_func are
    eligible for substitution. @see substitute_gc()
  */
  bool can_be_substituted_for_gc() const
  {
    const Type t= type();
    return t == FUNC_ITEM || t == COND_ITEM;
  }

  /**
    This function applies only to Item_field objects referred to by an Item_ref
    object that has been marked as a const_item.

    @param arg  Keep track of whether an Item_ref refers to an Item_field.
  */
  virtual bool repoint_const_outer_ref(uchar *arg MY_ATTRIBUTE((unused)))
  { return false; }
private:
  virtual bool subq_opt_away_processor(uchar*) { return false; }

  // Set an expression from select list of derived table as used.
  void set_derived_used() { derived_used= true; }

public:                            // Start of data fields
  /**
    Intrusive list pointer for free list. If not null, points to the next
    Item on some Query_arena's free list. For instance, stored procedures
    have their own Query_arena's.

    @see Query_arena::free_list
  */
  Item *next;
  /// str_values's main purpose is to cache the value in save_in_field
  String str_value;

  /**
    Character set and collation properties assigned for this Item.
    Used if Item represents a character string expression.
  */
  DTCollation collation;
  Item_name_string item_name;      ///< Name from query
  Item_name_string orig_name;      ///< Original item name (if it was renamed)
  /**
    Maximum length of result of evaluating this item, in number of bytes.
    - For character or blob data types, max char length multiplied by max
      character size (collation.mbmaxlen).
    - For decimal type, it is the precision in digits plus sign (unless
      unsigned) plus decimal point (unless it has zero decimals).
    - For other numeric types, the default or specific display length.
    - For date/time types, the display length (10 for DATE, 10 + optional FSP
      for TIME, 19 + optional fsp for datetime/timestamp).
    - For bit, the number of bits.
    - For enum, the string length of the widest enum element.
    - For set, the sum of the string length of each set element plus separators.
    - For geometry, the maximum size of a BLOB (it's underlying storage type).
    - For json, the maximum size of a BLOB (it's underlying storage type).
  */
  uint32 max_length;               ///< Maximum length, in bytes
  /**
    This member has several successive meanings, depending on the phase we're
    in:
    - when attaching conditions to tables: it says whether some condition
      needs to be attached or can be omitted (for example because it is already
      implemented by 'ref' access).
    - when pushing index conditions: it says whether a condition uses only
      indexed columns.
    - when creating an internal temporary table: says how to store BIT fields.
    - when we change DISTINCT to GROUP BY: used for book-keeping of fields.
  */
  int32 marker;
  Item_result cmp_context;         ///< Comparison context
private:
  const bool is_parser_item;       ///< true if allocated directly by parser
  /*
    If this item was created in runtime memroot, it cannot be used for
    substitution in subquery transformation process
  */
  bool runtime_item;
  int8 is_expensive_cache;         ///< Cache of result of is_expensive()
  uint8 m_data_type;               ///< Data type assigned to Item
public:
  bool fixed;                      ///< True if item has been resolved
  /**
    Number of decimals in result when evaluating this item
    - For integer type, always zero.
    - For decimal type, number of decimals.
    - For float type, it may be DECIMAL_NOT_SPECIFIED
    - For time, datetime and timestamp, number of decimals in fractional second
    - For string types, may be decimals of cast source or DECIMAL_NOT_SPECIFIED
  */
  uint8 decimals;
  /**
    True if this item may be null.

    For items that represent rows, it is true if one of the columns
    may be null.

    For items that represent scalar or row subqueries, it is true if
    one of the returned columns could be null, or if the subquery
    could return zero rows.
  */
  bool maybe_null;
  bool null_value;              ///< True if item is null
  bool unsigned_flag;
  bool with_sum_func;              ///< True if item is aggregated

private:
  /**
    True if this is an expression from the select list of a derived table
    which is actually used by outer query.
  */
  bool derived_used;

protected:
  /**
    True if this item is a subquery or some of its arguments is or contains a
    subquery. Computed by fix_fields() and updated by update_used_tables().
  */
  bool with_subselect;
  /**
    True if this item is a stored program or some of its arguments is or
    contains a stored program. Computed by fix_fields() and updated
    by update_used_tables().
  */
  bool with_stored_program;

  /**
    This variable is a cache of 'Needed tables are locked'. True if either
    'No tables locks is needed' or 'Needed tables are locked'.
    If tables are used, then it will be set to
    current_thd->lex->is_query_tables_locked().

    It is used when checking const_item()/can_be_evaluated_now().
  */
  bool tables_locked_cache;
};


class sp_head;


class Item_basic_constant :public Item
{
  table_map used_table_map;
public:
  Item_basic_constant(): Item(), used_table_map(0) {};
  explicit Item_basic_constant(const POS &pos): Item(pos), used_table_map(0) {};

  void set_used_tables(table_map map) { used_table_map= map; }
  table_map used_tables() const override { return used_table_map; }
  bool check_gcol_func_processor(uchar *) override { return false; }
  /* to prevent drop fixed flag (no need parent cleanup call) */
  void cleanup() override
  {
    /*
      Restore the original field name as it might not have been allocated
      in the statement memory. If the name is auto generated, it must be
      done again between subsequent executions of a prepared statement.
    */
    if (orig_name.is_set())
      item_name= orig_name;
  }
};


/*****************************************************************************
  The class is a base class for representation of stored routine variables in
  the Item-hierarchy. There are the following kinds of SP-vars:
    - local variables (Item_splocal);
    - CASE expression (Item_case_expr);
*****************************************************************************/

class Item_sp_variable : public Item
{
protected:
  /*
    THD, which is stored in fix_fields() and is used in this_item() to avoid
    current_thd use.
  */
  THD *m_thd;

public:
  Name_string m_name;

public:
#ifndef DBUG_OFF
  /*
    Routine to which this Item_splocal belongs. Used for checking if correct
    runtime context is used for variable handling.
  */
  sp_head *m_sp;
#endif

public:
  Item_sp_variable(const Name_string sp_var_name);

public:
  bool fix_fields(THD *thd, Item **) override;

  double val_real() override;
  longlong val_int() override;
  String *val_str(String *sp) override;
  my_decimal *val_decimal(my_decimal *decimal_value) override;
  bool val_json(Json_wrapper *result) override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override;
  bool get_time(MYSQL_TIME *ltime) override;
  bool is_null() override;

public:
  inline void make_field(Send_field *field) override;
  inline bool send(Protocol *protocol, String *str) override;

protected:
  inline type_conversion_status save_in_field_inner(Field *field,
                                                    bool no_conversions)
    override;
};

/*****************************************************************************
  Item_sp_variable inline implementation.
*****************************************************************************/

inline void Item_sp_variable::make_field(Send_field *field)
{
  Item *it= this_item();
  it->item_name.copy(item_name.is_set() ? item_name : m_name);
  it->make_field(field);
}

inline type_conversion_status
Item_sp_variable::save_in_field_inner(Field *field, bool no_conversions)
{
  return this_item()->save_in_field(field, no_conversions);
}

inline bool Item_sp_variable::send(Protocol *protocol, String *str)
{
  return this_item()->send(protocol, str);
}


/*****************************************************************************
  A reference to local SP variable (incl. reference to SP parameter), used in
  runtime.
*****************************************************************************/

class Item_splocal final : public Item_sp_variable,
                           private Settable_routine_parameter
{
  uint m_var_idx;

  Type m_type;
  Item_result m_result_type;
public:
  /*
    If this variable is a parameter in LIMIT clause.
    Used only during NAME_CONST substitution, to not append
    NAME_CONST to the resulting query and thus not break
    the slave.
  */
  bool limit_clause_param;
  /* 
    Position of this reference to SP variable in the statement (the
    statement itself is in sp_instr_stmt::m_query).
    This is valid only for references to SP variables in statements,
    excluding DECLARE CURSOR statement. It is used to replace references to SP
    variables with NAME_CONST calls when putting statements into the binary
    log.
    Value of 0 means that this object doesn't corresponding to reference to
    SP variable in query text.
  */
  uint pos_in_query;
  /*
    Byte length of SP variable name in the statement (see pos_in_query).
    The value of this field may differ from the name_length value because
    name_length contains byte length of UTF8-encoded item name, but
    the query string (see sp_instr_stmt::m_query) is currently stored with
    a charset from the SET NAMES statement.
  */
  uint len_in_query;

  Item_splocal(const Name_string sp_var_name, uint sp_var_idx,
               enum_field_types sp_var_type,
               uint pos_in_q= 0, uint len_in_q= 0);

  bool is_splocal() const override { return true; }

  Item *this_item() override;
  const Item *this_item() const override;
  Item **this_item_addr(THD *thd, Item **) override;

  void print(String *str, enum_query_type query_type) override;

public:
  inline uint get_var_idx() const { return m_var_idx; }

  inline enum Type type() const override { return m_type; }
  inline Item_result result_type() const override { return m_result_type; }
  bool val_json(Json_wrapper *result) override;

private:
  bool set_value(THD *thd, sp_rcontext *ctx, Item **it) override;

public:
  Settable_routine_parameter *get_settable_routine_parameter() override
  {
    return this;
  }
};

/*****************************************************************************
  A reference to case expression in SP, used in runtime.
*****************************************************************************/

class Item_case_expr final :public Item_sp_variable
{
public:
  Item_case_expr(uint case_expr_id);

public:
  Item *this_item() override;
  const Item *this_item() const override;
  Item **this_item_addr(THD *thd, Item **) override;

  Type type() const override { return this_item()->type(); }
  Item_result result_type() const override { return this_item()->result_type();}

public:
  /*
    NOTE: print() is intended to be used from views and for debug.
    Item_case_expr can not occur in views, so here it is only for debug
    purposes.
  */
  void print(String *str, enum_query_type query_type) override;

private:
  uint m_case_expr_id;
};

/*
  NAME_CONST(given_name, const_value). 
  This 'function' has all properties of the supplied const_value (which is 
  assumed to be a literal constant), and the name given_name. 

  This is used to replace references to SP variables when we write PROCEDURE
  statements into the binary log.

  TODO
    Together with Item_splocal and Item::this_item() we can actually extract
    common a base of this class and Item_splocal. Maybe it is possible to
    extract a common base with class Item_ref, too.
*/

class Item_name_const final : public Item
{
  typedef Item super;

  Item *value_item;
  Item *name_item;
  bool valid_args;
public:
  Item_name_const(const POS &pos, Item *name_arg, Item *val);

  bool itemize(Parse_context *pc, Item **res) override;
  bool fix_fields(THD *, Item **) override;

  enum Type type() const override;
  double val_real() override;
  longlong val_int() override;
  String *val_str(String *sp) override;
  my_decimal *val_decimal(my_decimal *) override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override;
  bool get_time(MYSQL_TIME *ltime) override;
  bool is_null() override;
  void print(String *str, enum_query_type query_type) override;

  Item_result result_type() const override
  {
    return value_item->result_type();
  }

  bool send(Protocol *protocol, String *str) override
  {
    return value_item->send(protocol, str);
  }

  bool cache_const_expr_analyzer(uchar **) override
  {
    // Item_name_const always wraps a literal, so there is no need to cache it.
    return false;
  }

protected:
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override
  {
    return value_item->save_in_field(field, no_conversions);
  }
};

bool agg_item_collations_for_comparison(DTCollation &c, const char *name,
                                        Item **items, uint nitems, uint flags);
bool agg_item_set_converter(DTCollation &coll, const char *fname,
                            Item **args, uint nargs, uint flags, int item_sep);
bool agg_item_charsets(DTCollation &c, const char *name,
                       Item **items, uint nitems, uint flags, int item_sep);
inline bool
agg_item_charsets_for_string_result(DTCollation &c, const char *name,
                                    Item **items, uint nitems,
                                    int item_sep= 1)
{
  uint flags= MY_COLL_ALLOW_SUPERSET_CONV |
              MY_COLL_ALLOW_COERCIBLE_CONV |
              MY_COLL_ALLOW_NUMERIC_CONV;
  return agg_item_charsets(c, name, items, nitems, flags, item_sep);
}
inline bool
agg_item_charsets_for_comparison(DTCollation &c, const char *name,
                                 Item **items, uint nitems,
                                 int item_sep= 1)
{
  uint flags= MY_COLL_ALLOW_SUPERSET_CONV |
              MY_COLL_ALLOW_COERCIBLE_CONV |
              MY_COLL_DISALLOW_NONE;
  return agg_item_charsets(c, name, items, nitems, flags, item_sep);
}
inline bool
agg_item_charsets_for_string_result_with_comparison(DTCollation &c,
                                                    const char *name,
                                                    Item **items, uint nitems,
                                                    int item_sep= 1)
{
  uint flags= MY_COLL_ALLOW_SUPERSET_CONV |
              MY_COLL_ALLOW_COERCIBLE_CONV |
              MY_COLL_ALLOW_NUMERIC_CONV |
              MY_COLL_DISALLOW_NONE;
  return agg_item_charsets(c, name, items, nitems, flags, item_sep);
}


class Item_num: public Item_basic_constant
{
  typedef Item_basic_constant super;
public:
  Item_num() { collation.set_numeric(); } /* Remove gcc warning */
  explicit Item_num(const POS &pos) : super(pos)
  { collation.set_numeric(); }

  virtual Item_num *neg()= 0;
  Item *safe_charset_converter(const CHARSET_INFO *tocs) override;
  bool check_partition_func_processor(uchar *) override { return false; }
};

#define NO_CACHED_FIELD_INDEX ((uint)(-1))

class Item_ident : public Item
{
  typedef Item super;

protected:
  /* 
    We have to store initial values of db_name, table_name and field_name
    to be able to restore them during cleanup() because they can be 
    updated during fix_fields() to values from Field object and life-time 
    of those is shorter than life-time of Item_field.
  */
  const char *orig_db_name;
  const char *orig_table_name;
  const char *orig_field_name;
  bool m_alias_of_expr; ///< if this Item's name is alias of SELECT expression

public:
  Name_resolution_context *context;
  const char *db_name;
  const char *table_name;
  const char *field_name;

  /* 
    Cached value of index for this field in table->field array, used by prep. 
    stmts for speeding up their re-execution. Holds NO_CACHED_FIELD_INDEX 
    if index value is not known.
  */
  uint cached_field_index;
  /*
    Cached pointer to table which contains this field, used for the same reason
    by prep. stmt. too in case then we have not-fully qualified field.
    0 - means no cached value.
    @todo Notice that this is usually the same as Item_field::table_ref.
          cached_table should be replaced by table_ref ASAP.
  */
  TABLE_LIST *cached_table;
  SELECT_LEX *depended_from;

  Item_ident(Name_resolution_context *context_arg,
             const char *db_name_arg, const char *table_name_arg,
             const char *field_name_arg)
   :orig_db_name(db_name_arg), orig_table_name(table_name_arg),
    orig_field_name(field_name_arg), m_alias_of_expr(false),
    context(context_arg),
    db_name(db_name_arg), table_name(table_name_arg),
    field_name(field_name_arg),
    cached_field_index(NO_CACHED_FIELD_INDEX),
    cached_table(NULL), depended_from(NULL)
  {
    item_name.set(field_name_arg);
  }

  Item_ident(const POS &pos,
             const char *db_name_arg, const char *table_name_arg,
             const char *field_name_arg)
   :super(pos), orig_db_name(db_name_arg), orig_table_name(table_name_arg),
    orig_field_name(field_name_arg), m_alias_of_expr(false),
    db_name(db_name_arg), table_name(table_name_arg),
    field_name(field_name_arg),
    cached_field_index(NO_CACHED_FIELD_INDEX),
    cached_table(NULL), depended_from(NULL)
  {
    item_name.set(field_name_arg);
  }


  /// Constructor used by Item_field & Item_*_ref (see Item comment)

  Item_ident(THD *thd, Item_ident *item)
   :Item(thd, item),
    orig_db_name(item->orig_db_name),
    orig_table_name(item->orig_table_name),
    orig_field_name(item->orig_field_name),
    m_alias_of_expr(item->m_alias_of_expr),
    context(item->context),
    db_name(item->db_name),
    table_name(item->table_name),
    field_name(item->field_name),
    cached_field_index(item->cached_field_index),
    cached_table(item->cached_table),
    depended_from(item->depended_from)
  {}

  bool itemize(Parse_context *pc, Item **res) override;

  const char *full_name() const override;
  void fix_after_pullout(SELECT_LEX *parent_select, SELECT_LEX *removed_select)
    override;
  void cleanup() override;
  bool aggregate_check_distinct(uchar *arg) override;
  bool aggregate_check_group(uchar *arg) override;
  Bool3 local_column(const SELECT_LEX *sl) const override;

  void print(String *str, enum_query_type query_type) override
  {
    print(str, query_type, db_name, table_name);
  }
protected:
  /**
    Function to print column name for a table

    To print a column for a permanent table (picks up database and table from
    Item_ident object):

       item->print(str, qt)

    To print a column for a temporary table:

       item->print(str, qt, specific_db, specific_table)

    Items of temporary table fields have empty/NULL values of table_name and
    db_name. To print column names in a 3D form (`database`.`table`.`column`),
    this function prints db_name_arg and table_name_arg parameters instead of
    this->db_name and this->table_name respectively.

    @param [out] str            Output string buffer.
    @param       query_type     Bitmap to control printing details.
    @param       db_name_arg    String to output as a column database name.
    @param       table_name_arg String to output as a column table name.
  */
  void print(String *str, enum_query_type query_type,
             const char *db_name_arg,
             const char *table_name_arg) const;
public:
  bool change_context_processor(uchar *cntx) override
  {
    context= reinterpret_cast<Name_resolution_context *>(cntx);
    return false;
  }

  /// @returns true if this Item's name is alias of SELECT expression
  bool is_alias_of_expr() const { return m_alias_of_expr; }
  /// Marks that this Item's name is alias of SELECT expression
  void set_alias_of_expr() { m_alias_of_expr= true; }

  bool walk(Item_processor processor, enum_walk walk, uchar *arg) override
  {
    /*
      Item_ident processors like aggregate_check*() use
      WALK_PREFIX|WALK_POSTFIX and depend on the processor being called twice
      then.
    */
    return ((walk & WALK_PREFIX) && (this->*processor)(arg)) ||
           ((walk & WALK_POSTFIX) && (this->*processor)(arg));
  }

  /**
     @returns true if a part of this Item's full name (name or table name) is
     an alias.
  */
  virtual bool alias_name_used() const { return m_alias_of_expr; }
  friend bool insert_fields(THD *thd, Name_resolution_context *context,
                            const char *db_name,
                            const char *table_name, List_iterator<Item> *it,
                            bool any_privileges);
  bool is_strong_side_column_not_in_fd(uchar *arg) override;
  bool is_column_not_in_fd(uchar *arg) override;
};


class Item_ident_for_show final : public Item
{
public:
  Field *field;
  const char *db_name;
  const char *table_name;

  Item_ident_for_show(Field *par_field, const char *db_arg,
                      const char *table_name_arg)
    :field(par_field), db_name(db_arg), table_name(table_name_arg)
  {}

  enum Type type() const override { return FIELD_ITEM; }
  virtual bool fix_fields(THD *thd, Item **ref) override;
  double val_real() override { return field->val_real(); }
  longlong val_int() override { return field->val_int(); }
  String *val_str(String *str) override { return field->val_str(str); }
  my_decimal *val_decimal(my_decimal *dec) override
  { return field->val_decimal(dec); }
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override
  {
    return field->get_date(ltime, fuzzydate);
  }
  bool get_time(MYSQL_TIME *ltime) override
  {
    return field->get_time(ltime);
  }
  void make_field(Send_field *tmp_field) override;
  const CHARSET_INFO *charset_for_protocol() const override
  { return (CHARSET_INFO *)field->charset_for_protocol(); }
};


class COND_EQUAL;
class Item_equal;

class Item_field : public Item_ident
{
  typedef Item_ident super;

protected:
  void set_field(Field *field);
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;
public:
  /**
    Table containing this resolved field. This is required e.g for calculation
    of table map. Notice that for the following types of "tables",
    no TABLE_LIST object is assigned and hence table_ref is NULL:
     - Temporary tables assigned by join optimizer for sorting and aggregation.
     - Stored procedure dummy tables.
    For fields referencing such tables, table number is always 0, and other
    uses of table_ref is not needed.
  */
  TABLE_LIST *table_ref;
  Field *field;
  Field *result_field;
  Item_equal *item_equal;
  bool no_const_subst;
  /*
    if any_privileges set to TRUE then here real effective privileges will
    be stored
  */
  uint have_privileges;
  /* field need any privileges (for VIEW creation) */
  bool any_privileges;

  Item_field(Name_resolution_context *context_arg,
             const char *db_arg,const char *table_name_arg,
             const char *field_name_arg);
  Item_field(const POS &pos,
             const char *db_arg,const char *table_name_arg,
             const char *field_name_arg);

  /*
    Constructor needed to process subquery with temporary tables (see Item).
    Notice that it will have no name resolution context.
  */
  Item_field(THD *thd, Item_field *item);
  /*
    Ensures that field, table, and database names will live as long as
    Item_field (this is important in prepared statements).
  */
  Item_field(THD *thd, Name_resolution_context *context_arg, Field *field);
  /*
    If this constructor is used, fix_fields() won't work, because
    db_name, table_name and column_name are unknown. It's necessary to call
    reset_field() before fix_fields() for all fields created this way.
  */
  Item_field(Field *field);

  bool itemize(Parse_context *pc, Item **res) override;

  enum Type type() const override { return FIELD_ITEM; }
  bool eq(const Item *item, bool binary_cmp) const override;
  double val_real() override;
  longlong val_int() override;
  longlong val_time_temporal() override;
  longlong val_date_temporal() override;
  my_decimal *val_decimal(my_decimal *) override;
  String *val_str(String *) override;
  bool val_json(Json_wrapper *result) override;
  double val_result() override;
  longlong val_int_result() override;
  longlong val_time_temporal_result() override;
  longlong val_date_temporal_result() override;
  String *str_result(String *tmp) override;
  my_decimal *val_decimal_result(my_decimal *) override;
  bool val_bool_result() override;
  bool is_null_result() override;
  bool send(Protocol *protocol, String *str_arg) override;
  void reset_field(Field *f);
  bool fix_fields(THD *, Item **) override;
  void make_field(Send_field *tmp_field) override;
  void save_org_in_field(Field *field) override;
  table_map used_tables() const override;
  enum Item_result result_type() const override
  {
    return field->result_type();
  }
  enum Item_result numeric_context_result_type() const override
  {
    return field->numeric_context_result_type();
  }
  Item_result cast_to_int_type() const override
  {
    return field->cast_to_int_type();
  }
  enum_monotonicity_info get_monotonicity_info() const override
  {
    return MONOTONIC_STRICT_INCREASING;
  }
  longlong val_int_endpoint(bool left_endp, bool *incl_endp) override;
  Field *get_tmp_table_field() override { return result_field; }
  Field *tmp_table_field(TABLE *) override { return result_field; }
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override;
  bool get_date_result(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override;
  bool get_time(MYSQL_TIME *ltime) override;
  bool get_timeval(struct timeval *tm, int *warnings) override;
  bool is_null() override { return field->is_null(); }
  Item *get_tmp_table_item(THD *thd) override;
  bool collect_item_field_processor(uchar *arg) override;
  bool add_field_to_set_processor(uchar *arg) override;
  bool add_field_to_cond_set_processor(uchar *) override;
  bool remove_column_from_bitmap(uchar *arg) override;
  bool find_item_in_field_list_processor(uchar *arg) override;
  bool check_gcol_func_processor(uchar *int_arg) override;
  bool mark_field_in_map(uchar *arg) override
  {
    return Item::mark_field_in_map(pointer_cast<Mark_field *>(arg), field);
  }
  bool used_tables_for_level(uchar *arg) override;
  bool check_column_privileges(uchar *arg) override;
  bool check_partition_func_processor(uchar *) override { return false; }
  void cleanup() override;
  Item_equal *find_item_equal(COND_EQUAL *cond_equal);
  bool subst_argument_checker(uchar **arg) override;
  Item *equal_fields_propagator(uchar *arg) override;
  bool set_no_const_sub(uchar *) override;
  Item *replace_equal_field(uchar *) override;
  inline uint32 max_disp_length() { return field->max_display_length(); }
  Item_field *field_for_view_update() override { return this; }
  Item *safe_charset_converter(const CHARSET_INFO *tocs) override;
  int fix_outer_field(THD *thd, Field **field, Item **reference);
  Item *update_value_transformer(uchar *select_arg) override;
  void print(String *str, enum_query_type query_type) override;
  bool is_outer_field() const override
  {
    DBUG_ASSERT(fixed);
    return table_ref->outer_join || table_ref->outer_join_nest();
  }
  Field::geometry_type get_geometry_type() const override
  {
    DBUG_ASSERT(data_type() == MYSQL_TYPE_GEOMETRY);
    return field->get_geometry_type();
  }
  const CHARSET_INFO *charset_for_protocol(void) const override
  { return field->charset_for_protocol(); }

#ifndef DBUG_OFF
  void dbug_print() const
  {
    fprintf(DBUG_FILE, "<field ");
    if (field)
    {
      fprintf(DBUG_FILE, "'%s.%s': ", field->table->alias, field->field_name);
      field->dbug_print();
    }
    else
      fprintf(DBUG_FILE, "NULL");

    fprintf(DBUG_FILE, ", result_field: ");
    if (result_field)
    {
      fprintf(DBUG_FILE, "'%s.%s': ",
              result_field->table->alias, result_field->field_name);
      result_field->dbug_print();
    }
    else
      fprintf(DBUG_FILE, "NULL");
    fprintf(DBUG_FILE, ">\n");
  }
#endif

  float get_filtering_effect(table_map filter_for_table,
                             table_map read_tables,
                             const MY_BITMAP *fields_to_ignore,
                             double rows_in_table) override;

  /**
    Returns the probability for the predicate "col OP <val>" to be
    true for a row in the case where no index statistics or range
    estimates are available for 'col'.

    The probability depends on the number of rows in the table: it is by
    default 'default_filter', but never lower than 1/max_distinct_values
    (e.g. number of rows in the table, or the number of distinct values
    possible for the datatype if the field provides that kind of
    information).

    @param max_distinct_values The maximum number of distinct values,
                               typically the number of rows in the table
    @param default_filter      The default filter for the predicate

    @return the estimated filtering effect for this predicate
  */

  float get_cond_filter_default_probability(double max_distinct_values,
                                            float default_filter) const;

  friend class Item_default_value;
  friend class Item_insert_value;
  friend class SELECT_LEX_UNIT;

  /**
     @note that field->table->alias_name_used is reliable only if
     thd->lex->need_correct_ident() is true.
  */
  bool alias_name_used() const override
  { return m_alias_of_expr ||
      // maybe the qualifying table was given an alias ("t1 AS foo"):
      (field ? field->table->alias_name_used : false);
  }

  bool repoint_const_outer_ref(uchar *arg) override;
};

class Item_null : public Item_basic_constant
{
  typedef Item_basic_constant super;

  void init()
  {
    maybe_null= true;
    null_value= TRUE;
    set_data_type(MYSQL_TYPE_NULL);
    max_length= 0;
    fixed= 1;
    collation.set(&my_charset_bin, DERIVATION_IGNORABLE);
  }
protected:
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;
public:
  Item_null()
  {
    init();
    item_name= NAME_STRING("NULL");
  }
  explicit Item_null(const POS &pos) : super(pos)
  {
    init();
    item_name= NAME_STRING("NULL");
  }

  Item_null(const Name_string &name_par)
  {
    init();
    item_name= name_par;
  }

  enum Type type() const override { return NULL_ITEM; }
  bool eq(const Item *item, bool binary_cmp) const override;
  double val_real() override;
  longlong val_int() override;
  longlong val_time_temporal() override { return val_int(); }
  longlong val_date_temporal() override { return val_int(); }
  String *val_str(String *str) override;
  my_decimal *val_decimal(my_decimal *) override;
  bool get_date(MYSQL_TIME *, my_time_flags_t) override
  {
    return true;
  }
  bool get_time(MYSQL_TIME *) override
  {
    return true;
  }
  bool val_json(Json_wrapper *wr) override;
  bool send(Protocol *protocol, String *str) override;
  enum Item_result result_type() const override { return STRING_RESULT; }
  bool basic_const_item() const override { return true; }
  Item *clone_item() const override { return new Item_null(item_name); }
  bool is_null() override { return true; }

  void print(String *str, enum_query_type query_type) override
  {
    str->append(query_type == QT_NORMALIZED_FORMAT ? "?" : "NULL");
  }

  Item *safe_charset_converter(const CHARSET_INFO *tocs) override;
  bool check_partition_func_processor(uchar *) override { return false; }
};

/**
  An item representing NULL values for use with ROLLUP.

  When grouping WITH ROLLUP, Item_null_result items are created to
  represent NULL values in the grouping columns of the ROLLUP rows. To
  avoid type problems during execution, these objects are created with
  the same field and result types as the fields of the columns they
  belong to.
 */
class Item_null_result final : public Item_null
{
  /** Result type for this NULL value */
  Item_result res_type;

public:
  Field *result_field;
  Item_null_result(enum_field_types fld_type, Item_result res_type)
    : Item_null(), res_type(res_type), result_field(NULL)
  {
    set_data_type(fld_type);
  }
  bool is_result_field() const override { return result_field != nullptr; }
  void save_in_result_field(bool no_conversions) override
  {
    save_in_field(result_field, no_conversions);
  }
  bool check_partition_func_processor(uchar *) override { return true; }
  Item_result result_type() const override { return res_type; }
  bool check_gcol_func_processor(uchar *) override { return true; }
  enum Type type() const override { return NULL_RESULT_ITEM; }
};

/// Placeholder ('?') of prepared statement.
class Item_param final : public Item,
                         private Settable_routine_parameter
{
  typedef Item super;

protected:
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;

public:
  enum enum_item_param_state
  {
    NO_VALUE, NULL_VALUE, INT_VALUE, REAL_VALUE,
    STRING_VALUE, TIME_VALUE, LONG_DATA_VALUE,
    DECIMAL_VALUE
  } state;

  /*
    A buffer for string and long data values. Historically all allocated
    values returned from val_str() were treated as eligible to
    modification. I. e. in some cases Item_func_concat can append it's
    second argument to return value of the first one. Because of that we
    can't return the original buffer holding string data from val_str(),
    and have to have one buffer for data and another just pointing to
    the data. This is the latter one and it's returned from val_str().
    Can not be declared inside the union as it's not a POD type.
  */
  String str_value_ptr;
  my_decimal decimal_value;
  union
  {
    longlong integer;
    double   real;
    /*
      Character sets conversion info for string values.
      Character sets of client and connection defined at bind time are used
      for all conversions, even if one of them is later changed (i.e.
      between subsequent calls to mysql_stmt_execute).
    */
    struct CONVERSION_INFO
    {
      const CHARSET_INFO *character_set_client;
      const CHARSET_INFO *character_set_of_placeholder;
      /*
        This points at character set of connection if conversion
        to it is required (i. e. if placeholder typecode is not BLOB).
        Otherwise it's equal to character_set_client (to simplify
        check in convert_str_value()).
      */
      const CHARSET_INFO *final_character_set_of_str_value;
    } cs_info;
    MYSQL_TIME     time;
  } value;

  /* Cached values for virtual methods to save us one switch.  */
  enum Item_result item_result_type;
  enum Type item_type;

  /*
    data_type() is used when this item is used in a temporary table.
    This is NOT placeholder metadata sent to client, as this value
    is assigned after sending metadata (in setup_one_conversion_function).
    For example in case of 'SELECT ?' you'll get MYSQL_TYPE_STRING both
    in result set and placeholders metadata, no matter what type you will
    supply for this placeholder in mysql_stmt_execute.
  */

  /*
    Offset of placeholder inside statement text. Used to create
    no-placeholders version of this statement for the binary log.
  */
  uint pos_in_query;

  Item_param(const POS &pos, MEM_ROOT *root, uint pos_in_query_arg);

  bool itemize(Parse_context *pc, Item **item) override;

  enum Item_result result_type() const override { return item_result_type; }
  enum Type type() const override { return item_type; }

  double val_real() override;
  longlong val_int() override;
  my_decimal *val_decimal(my_decimal *) override;
  String *val_str(String *) override;
  bool get_time(MYSQL_TIME *tm) override;
  bool get_date(MYSQL_TIME *tm, my_time_flags_t fuzzydate) override;

  void set_null();
  void set_int(longlong i, uint32 max_length_arg);
  void set_double(double i);
  void set_decimal(const char *str, ulong length);
  void set_decimal(const my_decimal *dv);
  bool set_str(const char *str, size_t length);
  bool set_longdata(const char *str, ulong length);
  void set_time(MYSQL_TIME *tm, timestamp_type type, uint32 max_length_arg);
  bool set_from_user_var(THD *thd, const user_var_entry *entry);
  void reset();
  /*
    Assign placeholder value from bind data.
  */
  void (*set_param_func)(Item_param *param, uchar **pos, ulong len);

  const String *query_val_str(THD *thd, String *str) const;

  bool convert_str_value(THD *thd);

  /*
    If value for parameter was not set we treat it as non-const
    so noone will use parameters value in fix_fields still
    parameter is constant during execution.
  */
  table_map used_tables() const override
  { return state != NO_VALUE ? (table_map)0 : PARAM_TABLE_BIT; }
  void print(String *str, enum_query_type query_type) override;
  bool is_null() override
  { DBUG_ASSERT(state != NO_VALUE); return state == NULL_VALUE; }
  bool basic_const_item() const override;
  /*
    This method is used to make a copy of a basic constant item when
    propagating constants in the optimizer. The reason to create a new
    item and not use the existing one is not precisely known (2005/04/16).
    Probably we are trying to preserve tree structure of items, in other
    words, avoid pointing at one item from two different nodes of the tree.
    Return a new basic constant item if parameter value is a basic
    constant, assert otherwise. This method is called only if
    basic_const_item returned TRUE.
  */
  Item *safe_charset_converter(const CHARSET_INFO *tocs) override;
  Item *clone_item() const override;
  /*
    Implement by-value equality evaluation if parameter value
    is set and is a basic constant (integer, real or string).
    Otherwise return FALSE.
  */
  bool eq(const Item *item, bool binary_cmp) const override;
  /** Item is a argument to a limit clause. */
  bool limit_clause_param;
  void set_param_type_and_swap_value(Item_param *from);
  /**
    This should be called after any modification done to this Item, to
    propagate the said modification to all its clones.
  */
  void sync_clones();
  bool add_clone(Item_param *i) { return m_clones.push_back(i); }

private:
  Settable_routine_parameter *get_settable_routine_parameter() override
  {
    return this;
  }

  bool set_value(THD*, sp_rcontext*, Item **it) override;

  void set_out_param_info(Send_field *info) override;

public:
  const Send_field *get_out_param_info() const override;

  void make_field(Send_field *field) override;

private:
  Send_field *m_out_param_info;
  /**
    If a query expression's text QT, containing a parameter, is internally
    duplicated and parsed twice (@see reparse_common_table_expression), the
    first parsing will create an Item_param I, and the re-parsing, which
    parses a forged "(QT)" parse-this-CTE type of statement, will create an
    Item_param J. J should not exist:
    - from the point of view of logging: it is not in the original query so it
    should not be substituted in the query written to logs (in insert_params()
    if with_log is true).
    - from the POV of query cache matching (same).
    - from the POV of the user:
        * user provides one single value for I, not one for I and one for J.
        * user expects mysql_stmt_param_count() to return 1, not 2 (count is
        sent by the server in send_prep_stmt()).
    That is why J is part neither of LEX::param_list, nor of param_array; it
    is considered an inferior clone of I; I::m_clones contains J.
    The connection between I and J is made once, by comparing their
    byte position in the statement, in Item_param::itemize().
    J gets its value from I: @see Item_param::sync_clones.
  */
  Mem_root_array<Item_param *> m_clones;
};


class Item_int :public Item_num
{
  typedef Item_num super;
public:
  longlong value;
  Item_int(int32 i,uint length= MY_INT32_NUM_DECIMAL_DIGITS)
    :value((longlong) i)
  { set_data_type(MYSQL_TYPE_LONGLONG); max_length= length; fixed= true; }
  Item_int(const POS &pos, int32 i,uint length= MY_INT32_NUM_DECIMAL_DIGITS)
    :super(pos), value((longlong) i)
  { set_data_type(MYSQL_TYPE_LONGLONG); max_length= length; fixed= true; }
  Item_int(longlong i,uint length= MY_INT64_NUM_DECIMAL_DIGITS)
    :value(i)
  { set_data_type(MYSQL_TYPE_LONGLONG); max_length= length; fixed= true; }
  Item_int(ulonglong i, uint length= MY_INT64_NUM_DECIMAL_DIGITS)
    :value((longlong)i)
  {
    set_data_type(MYSQL_TYPE_LONGLONG);
    max_length= length;
    fixed= true;
    unsigned_flag= true;
  }
  Item_int(const Item_int *item_arg)
  {
    set_data_type(item_arg->data_type());
    value= item_arg->value;
    item_name= item_arg->item_name;
    max_length= item_arg->max_length;
    fixed= true;
  }
  Item_int(const Name_string &name_arg, longlong i, uint length) :value(i)
  {
    set_data_type(MYSQL_TYPE_LONGLONG);
    max_length= length;
    item_name= name_arg;
    fixed= true;
  }
  Item_int(const POS &pos, const Name_string &name_arg, longlong i, uint length)
    :super(pos), value(i)
  {
    set_data_type(MYSQL_TYPE_LONGLONG);
    max_length= length;
    item_name= name_arg;
    fixed= true;
  }
  Item_int(const char *str_arg, uint length)
  {
    set_data_type(MYSQL_TYPE_LONGLONG);
    init(str_arg, length);
  }
  Item_int(const POS &pos, const char *str_arg, uint length) : super(pos)
  {
    set_data_type(MYSQL_TYPE_LONGLONG);
    init(str_arg, length);
  }

  Item_int(const POS &pos, const LEX_STRING &num, int dummy_error= 0)
  : Item_int(pos, num, my_strtoll10(num.str, NULL, &dummy_error),
             static_cast<uint>(num.length))
  {}

private:
  void init(const char *str_arg, uint length);

protected:
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;

public:
  enum Type type() const override { return INT_ITEM; }
  enum Item_result result_type() const override { return INT_RESULT; }
  longlong val_int() override { DBUG_ASSERT(fixed); return value; }
  double val_real() override
  {
    DBUG_ASSERT(fixed);
    return static_cast<double>(value);
  }
  my_decimal *val_decimal(my_decimal *) override;
  String *val_str(String *) override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override
  {
    return get_date_from_int(ltime, fuzzydate);
  }
  bool get_time(MYSQL_TIME *ltime) override
  {
    return get_time_from_int(ltime);
  }
  bool basic_const_item() const override { return true; }
  Item *clone_item() const override { return new Item_int(this); }
  void print(String *str, enum_query_type query_type) override;
  Item_num *neg() override { value= -value; return this; }
  uint decimal_precision() const override
  { return (uint)(max_length - MY_TEST(value < 0)); }
  bool eq(const Item *, bool) const override;
  bool check_partition_func_processor(uchar *) override { return false; }
  bool check_gcol_func_processor(uchar *) override { return false; }
};


/**
  Item_int with value==0 and length==1
*/
class Item_int_0 final : public Item_int
{
public:
  Item_int_0() :Item_int(NAME_STRING("0"), 0, 1) {}
  explicit
  Item_int_0(const POS &pos) :Item_int(pos, NAME_STRING("0"), 0, 1) {}
};


/*
  Item_temporal is used to store numeric representation
  of time/date/datetime values for queries like:

     WHERE datetime_column NOT IN
     ('2006-04-25 10:00:00','2006-04-25 10:02:00', ...);

  and for SHOW/INFORMATION_SCHEMA purposes (see sql_show.cc)

  TS-TODO: Can't we use Item_time_literal, Item_date_literal,
  TS-TODO: and Item_datetime_literal for this purpose?
*/
class Item_temporal final : public Item_int
{
protected:
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;
public:
  Item_temporal(enum_field_types field_type_arg, longlong i): Item_int(i)
  {
    DBUG_ASSERT(is_temporal_type(field_type_arg));
    set_data_type(field_type_arg);
  }
  Item_temporal(enum_field_types field_type_arg, const Name_string &name_arg,
                longlong i, uint length): Item_int(i)
  {
    DBUG_ASSERT(is_temporal_type(field_type_arg));
    set_data_type(field_type_arg);
    max_length= length;
    item_name= name_arg;
    fixed= true;
  }
  Item *clone_item() const override
  { return new Item_temporal(data_type(), value); }
  longlong val_time_temporal() override { return val_int(); }
  longlong val_date_temporal() override { return val_int(); }
  bool get_date(MYSQL_TIME *, my_time_flags_t) override
  {
    DBUG_ASSERT(0);
    return false;
  }
  bool get_time(MYSQL_TIME *) override
  {
    DBUG_ASSERT(0);
    return false;
  }
};


class Item_uint :public Item_int
{
protected:
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;
public:
  Item_uint(const char *str_arg, uint length)
    :Item_int(str_arg, length) { unsigned_flag= 1; }
  Item_uint(const POS &pos, const char *str_arg, uint length)
    :Item_int(pos, str_arg, length) { unsigned_flag= 1; }

  Item_uint(ulonglong i) :Item_int(i, 10) {}
  Item_uint(const Name_string &name_arg, longlong i, uint length)
    :Item_int(name_arg, i, length) { unsigned_flag= true; }
  double val_real() override
  {
    DBUG_ASSERT(fixed);
    return ulonglong2double(static_cast<ulonglong>(value));
  }
  String *val_str(String*) override;

  Item *clone_item() const override
  { return new Item_uint(item_name, value, max_length); }
  void print(String *str, enum_query_type query_type) override;
  Item_num *neg() override;
  uint decimal_precision() const override { return max_length; }
};


/* decimal (fixed point) constant */
class Item_decimal :public Item_num
{
  typedef Item_num super;
protected:
  my_decimal decimal_value;
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;
public:
  Item_decimal(const POS &pos,
               const char *str_arg, uint length, const CHARSET_INFO *charset);
  Item_decimal(const Name_string &name_arg,
               const my_decimal *val_arg, uint decimal_par, uint length);
  Item_decimal(my_decimal *value_par);
  Item_decimal(longlong val, bool unsig);
  Item_decimal(double val);
  Item_decimal(const uchar *bin, int precision, int scale);

  enum Type type() const override { return DECIMAL_ITEM; }
  enum Item_result result_type() const override { return DECIMAL_RESULT; }
  longlong val_int() override;
  double val_real() override;
  String *val_str(String *) override;
  my_decimal *val_decimal(my_decimal *) override { return &decimal_value; }
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override
  {
    return get_date_from_decimal(ltime, fuzzydate);
  }
  bool get_time(MYSQL_TIME *ltime) override
  {
    return get_time_from_decimal(ltime);
  }
  bool basic_const_item() const override { return true; }
  Item *clone_item() const override
  {
    return new Item_decimal(item_name, &decimal_value, decimals, max_length);
  }
  void print(String *str, enum_query_type query_type) override;
  Item_num *neg() override
  {
    my_decimal_neg(&decimal_value);
    unsigned_flag= !decimal_value.sign();
    return this;
  }
  uint decimal_precision() const override { return decimal_value.precision(); }
  bool eq(const Item *, bool binary_cmp) const override;
  void set_decimal_value(my_decimal *value_par);
  bool check_partition_func_processor(uchar *) override { return false; }
};


class Item_float :public Item_num
{
  typedef Item_num super;

  Name_string presentation;
public:
  double value;
  // Item_real() :value(0) {}
  Item_float(const char *str_arg, uint length)
  { init(str_arg, length); }
  Item_float(const POS &pos, const char *str_arg, uint length) : super(pos)
  { init(str_arg, length); }

  Item_float(const Name_string name_arg,
             double val_arg, uint decimal_par, uint length)
    :value(val_arg)
  {
    presentation= name_arg;
    item_name= name_arg;
    set_data_type(MYSQL_TYPE_DOUBLE);
    decimals= (uint8) decimal_par;
    max_length= length;
    fixed= 1;
  }
  Item_float(const POS &pos, const Name_string name_arg,
             double val_arg, uint decimal_par, uint length)
    :super(pos), value(val_arg)
  {
    presentation= name_arg;
    item_name= name_arg;
    set_data_type(MYSQL_TYPE_DOUBLE);
    decimals= (uint8) decimal_par;
    max_length= length;
    fixed= 1;
  }

  Item_float(double value_par, uint decimal_par) :value(value_par)
  {
    set_data_type(MYSQL_TYPE_DOUBLE);
    decimals= (uint8) decimal_par;
    fixed= 1;
  }

private:
  void init(const char *str_arg, uint length);

protected:
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;

public:
  enum Type type() const override { return REAL_ITEM; }
  double val_real() override { DBUG_ASSERT(fixed); return value; }
  longlong val_int() override
  {
    DBUG_ASSERT(fixed == 1);
    if (value <= (double) LLONG_MIN)
    {
       return LLONG_MIN;
    }
    else if (value >= (double) (ulonglong) LLONG_MAX)
    {
      return LLONG_MAX;
    }
    return (longlong) rint(value);
  }
  String *val_str(String *) override;
  my_decimal *val_decimal(my_decimal *) override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override
  {
    return get_date_from_real(ltime, fuzzydate);
  }
  bool get_time(MYSQL_TIME *ltime) override
  {
    return get_time_from_real(ltime);
  }
  bool basic_const_item() const override { return true; }
  Item *clone_item() const override
  { return new Item_float(item_name, value, decimals, max_length); }
  Item_num *neg() override { value= -value; return this; }
  void print(String *str, enum_query_type query_type) override;
  bool eq(const Item *, bool binary_cmp) const override;
};


class Item_func_pi :public Item_float
{
  const Name_string func_name;
public:
  Item_func_pi(const POS &pos)
    : Item_float(pos, null_name_string, M_PI, 6, 8),
      func_name(NAME_STRING("pi()"))
  {}

  void print(String *str, enum_query_type) override
  {
    str->append(func_name);
  }

  Item *safe_charset_converter(const CHARSET_INFO *tocs) override;
};


class Item_string :public Item_basic_constant
{
  typedef Item_basic_constant super;

protected:
  explicit Item_string(const POS &pos) : super(pos), m_cs_specified(FALSE)
  {
    set_data_type(MYSQL_TYPE_VARCHAR);
  }

  void init(const char *str, size_t length,
            const CHARSET_INFO *cs, Derivation dv, uint repertoire)
  {
    set_data_type(MYSQL_TYPE_VARCHAR);
    str_value.set_or_copy_aligned(str, length, cs);
    collation.set(cs, dv, repertoire);
    /*
      We have to have a different max_length than 'length' here to
      ensure that we get the right length if we do use the item
      to create a new table. In this case max_length must be the maximum
      number of chars for a string of this type because we in Create_field::
      divide the max_length with mbmaxlen).
    */
    max_length= static_cast<uint32>(str_value.numchars() * cs->mbmaxlen);
    item_name.copy(str, length, cs);
    decimals=NOT_FIXED_DEC;
    // it is constant => can be used without fix_fields (and frequently used)
    fixed= 1;
    /*
      Check if the string has any character that can't be
      interpreted using the relevant charset.
    */
    check_well_formed_result(&str_value, false, false);
  }
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;
public:
  /* Create from a string, set name from the string itself. */
  Item_string(const char *str, size_t length,
              const CHARSET_INFO *cs, Derivation dv= DERIVATION_COERCIBLE,
              uint repertoire= MY_REPERTOIRE_UNICODE30)
    : m_cs_specified(FALSE)
  {
    init(str, length, cs, dv, repertoire);
  }
  Item_string(const POS &pos, const char *str, size_t length,
              const CHARSET_INFO *cs, Derivation dv= DERIVATION_COERCIBLE,
              uint repertoire= MY_REPERTOIRE_UNICODE30)
    : super(pos), m_cs_specified(FALSE)
  {
    init(str, length, cs, dv, repertoire);
  }

  /* Just create an item and do not fill string representation */
  Item_string(const CHARSET_INFO *cs, Derivation dv= DERIVATION_COERCIBLE)
    : m_cs_specified(FALSE)
  {
    collation.set(cs, dv);
    set_data_type(MYSQL_TYPE_VARCHAR);
    max_length= 0;
    decimals= NOT_FIXED_DEC;
    fixed= 1;
  }

  /* Create from the given name and string. */
  Item_string(const Name_string name_par, const char *str, size_t length,
              const CHARSET_INFO *cs, Derivation dv= DERIVATION_COERCIBLE,
              uint repertoire= MY_REPERTOIRE_UNICODE30)
    : m_cs_specified(FALSE)
  {
    str_value.set_or_copy_aligned(str, length, cs);
    collation.set(cs, dv, repertoire);
    set_data_type(MYSQL_TYPE_VARCHAR);
    max_length= static_cast<uint32>(str_value.numchars() * cs->mbmaxlen);
    item_name= name_par;
    decimals=NOT_FIXED_DEC;
    // it is constant => can be used without fix_fields (and frequently used)
    fixed= 1;
  }
  Item_string(const POS &pos, const Name_string name_par, const char *str, size_t length,
              const CHARSET_INFO *cs, Derivation dv= DERIVATION_COERCIBLE,
              uint repertoire= MY_REPERTOIRE_UNICODE30)
    : super(pos), m_cs_specified(FALSE)
  {
    str_value.set_or_copy_aligned(str, length, cs);
    collation.set(cs, dv, repertoire);
    set_data_type(MYSQL_TYPE_VARCHAR);
    max_length= static_cast<uint32>(str_value.numchars()*cs->mbmaxlen);
    item_name= name_par;
    decimals=NOT_FIXED_DEC;
    // it is constant => can be used without fix_fields (and frequently used)
    fixed= 1;
  }

  /* Create from the given name and string. */
  Item_string(const POS &pos,
              const Name_string name_par, const LEX_STRING &literal,
              const CHARSET_INFO *cs, Derivation dv= DERIVATION_COERCIBLE,
              uint repertoire= MY_REPERTOIRE_UNICODE30)
    : super(pos), m_cs_specified(FALSE)
  {
    str_value.set_or_copy_aligned(literal.str ? literal.str : "",
                                  literal.str ? literal.length : 0, cs);
    collation.set(cs, dv, repertoire);
    set_data_type(MYSQL_TYPE_VARCHAR);
    max_length= static_cast<uint32>(str_value.numchars()*cs->mbmaxlen);
    item_name= name_par;
    decimals=NOT_FIXED_DEC;
    // it is constant => can be used without fix_fields (and frequently used)
    fixed= 1;
  }

  /*
    This is used in stored procedures to avoid memory leaks and
    does a deep copy of its argument.
  */
  void set_str_with_copy(const char *str_arg, uint length_arg)
  {
    str_value.copy(str_arg, length_arg, collation.collation);
    max_length= static_cast<uint32>(str_value.numchars() *
                                    collation.collation->mbmaxlen);
  }
  void set_repertoire_from_value()
  {
    collation.repertoire= my_string_repertoire(str_value.charset(),
                                               str_value.ptr(),
                                               str_value.length());
  }
  enum Type type() const override { return STRING_ITEM; }
  double val_real() override;
  longlong val_int() override;
  String *val_str(String *) override
  {
    DBUG_ASSERT(fixed == 1);
    return &str_value;
  }
  my_decimal *val_decimal(my_decimal *) override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override
  {
    return get_date_from_string(ltime, fuzzydate);
  }
  bool get_time(MYSQL_TIME *ltime) override
  {
    return get_time_from_string(ltime);
  }
  enum Item_result result_type() const override { return STRING_RESULT; }
  bool basic_const_item() const override { return true; }
  bool eq(const Item *item, bool binary_cmp) const override;
  Item *clone_item() const override
  {
    return new Item_string(static_cast<Name_string>(item_name), str_value.ptr(),
    			   str_value.length(), collation.collation);
  }
  Item *safe_charset_converter(const CHARSET_INFO *tocs) override;
  Item *charset_converter(const CHARSET_INFO *tocs, bool lossless);
  inline void append(char *str, size_t length)
  {
    str_value.append(str, length);
    max_length= static_cast<uint32>(str_value.numchars() *
                                    collation.collation->mbmaxlen);
  }
  void print(String *str, enum_query_type query_type) override;
  bool check_partition_func_processor(uchar *) override { return false; }

  /**
    Return TRUE if character-set-introducer was explicitly specified in the
    original query for this item (text literal).

    This operation is to be called from Item_string::print(). The idea is
    that when a query is generated (re-constructed) from the Item-tree,
    character-set-introducers should appear only for those literals, where
    they were explicitly specified by the user. Otherwise, that may lead to
    loss collation information (character set introducers implies default
    collation for the literal).

    Basically, that makes sense only for views and hopefully will be gone
    one day when we start using original query as a view definition.

    @return This operation returns the value of m_cs_specified attribute.
      @retval TRUE if character set introducer was explicitly specified in
      the original query.
      @retval FALSE otherwise.
  */
  inline bool is_cs_specified() const
  {
    return m_cs_specified;
  }

  /**
    Set the value of m_cs_specified attribute.

    m_cs_specified attribute shows whether character-set-introducer was
    explicitly specified in the original query for this text literal or
    not. The attribute makes sense (is used) only for views.

    This operation is to be called from the parser during parsing an input
    query.
  */
  inline void set_cs_specified(bool cs_specified)
  {
    m_cs_specified= cs_specified;
  }

private:
  bool m_cs_specified;
};


longlong
longlong_from_string_with_check(const CHARSET_INFO *cs,
                                const char *cptr, const char *end);
double
double_from_string_with_check(const CHARSET_INFO *cs,
                              const char *cptr, const char *end);

class Item_static_string_func : public Item_string
{
  const Name_string func_name;
public:
  Item_static_string_func(const Name_string &name_par,
                          const char *str, size_t length, const CHARSET_INFO *cs,
                          Derivation dv= DERIVATION_COERCIBLE)
    :Item_string(null_name_string, str, length, cs, dv), func_name(name_par)
  {}
  Item_static_string_func(const POS &pos, const Name_string &name_par,
                          const char *str, size_t length, const CHARSET_INFO *cs,
                          Derivation dv= DERIVATION_COERCIBLE)
    :Item_string(pos, null_name_string, str, length, cs, dv),
     func_name(name_par)
  {}

  Item *safe_charset_converter(const CHARSET_INFO *tocs) override;

  inline void print(String *str, enum_query_type) override
  {
    str->append(func_name);
  }

  bool check_partition_func_processor(uchar *) override { return true; }
  bool check_gcol_func_processor(uchar *) override { return true; }
};


/* for show tables */
class Item_partition_func_safe_string : public Item_string
{
public:
  Item_partition_func_safe_string(const Name_string name, size_t length,
                                  const CHARSET_INFO *cs= NULL):
    Item_string(name, NullS, 0, cs)
  {
    max_length= static_cast<uint32>(length);
  }
};


class Item_blob final : public Item_partition_func_safe_string
{
public:
  Item_blob(const char *name, size_t length) :
    Item_partition_func_safe_string(Name_string(name, strlen(name)),
                                    length, &my_charset_bin)
  { set_data_type(MYSQL_TYPE_BLOB); }
  enum Type type() const override { return TYPE_HOLDER; }
};


/**
  Item_empty_string -- is a utility class to put an item into List<Item>
  which is then used in protocol.send_result_set_metadata() when sending SHOW output to
  the client.
*/

class Item_empty_string :public Item_partition_func_safe_string
{
public:
  Item_empty_string(const char *header, size_t length,
                    const CHARSET_INFO *cs= NULL) :
    Item_partition_func_safe_string(Name_string(header, strlen(header)),
                                    0, cs ? cs : &my_charset_utf8_general_ci)
    {
      max_length= static_cast<uint32>(length * collation.collation->mbmaxlen);
    }
  void make_field(Send_field *field) override;
};


class Item_return_int :public Item_int
{
public:
  Item_return_int(const char *name_arg, uint length,
		  enum_field_types field_type_arg, longlong value= 0)
    :Item_int(Name_string(name_arg, name_arg ? strlen(name_arg) : 0),
              value, length)
  {
    set_data_type(field_type_arg);
    unsigned_flag= true;
  }
};


class Item_hex_string : public Item_basic_constant
{
  typedef Item_basic_constant super;

protected:
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;

public:
  Item_hex_string();
  explicit Item_hex_string(const POS &pos) : super(pos)
  { set_data_type(MYSQL_TYPE_VARCHAR); }

  Item_hex_string(const char *str,uint str_length);
  Item_hex_string(const POS &pos, const LEX_STRING &literal);

  enum Type type() const override { return VARBIN_ITEM; }
  double val_real() override
  {
    DBUG_ASSERT(fixed);
    return (double) (ulonglong) Item_hex_string::val_int();
  }
  longlong val_int() override;
  bool basic_const_item() const override { return true; }
  Item *clone_item() const override
  {
    return new Item_hex_string(str_value.ptr(), max_length);
  }
  String *val_str(String *) override { DBUG_ASSERT(fixed); return &str_value; }
  my_decimal *val_decimal(my_decimal *) override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override
  {
    return get_date_from_string(ltime, fuzzydate);
  }
  bool get_time(MYSQL_TIME *ltime) override
  {
    return get_time_from_string(ltime);
  }
  Item_result result_type() const override { return STRING_RESULT; }
  Item_result numeric_context_result_type() const override {return INT_RESULT;}
  Item_result cast_to_int_type() const override { return INT_RESULT; }
  void print(String *str, enum_query_type query_type) override;
  bool eq(const Item *item, bool binary_cmp) const override;
  Item *safe_charset_converter(const CHARSET_INFO *tocs) override;
  bool check_partition_func_processor(uchar *) override { return false; }
  static LEX_STRING make_hex_str(const char *str, size_t str_length);
private:
  void hex_string_init(const char *str, uint str_length);
};


class Item_bin_string final : public Item_hex_string
{
  typedef Item_hex_string super;

public:
  Item_bin_string(const char *str, size_t str_length)
  { bin_string_init(str, str_length); }
  Item_bin_string(const POS &pos, const LEX_STRING &literal) : super(pos)
  { bin_string_init(literal.str, literal.length); }

  static LEX_STRING make_bin_str(const char *str, size_t str_length);

private:
  void bin_string_init(const char *str, size_t str_length);
};

class Item_result_field :public Item	/* Item with result field */
{
public:
  Field *result_field;				/* Save result here */
  Item_result_field() :result_field(0) {}
  explicit Item_result_field(const POS &pos) :Item(pos), result_field(0) {}

  // Constructor used for Item_sum/Item_cond_and/or (see Item comment)
  Item_result_field(THD *thd, Item_result_field *item):
    Item(thd, item), result_field(item->result_field)
  {}
  ~Item_result_field() {}			/* Required with gcc 2.95 */
  Field *get_tmp_table_field() override { return result_field; }
  Field *tmp_table_field(TABLE*) override { return result_field; }
  table_map used_tables() const override { return 1; }

  /**
    Resolve type-related information for this item, such as result field type,
    maximum size, precision, signedness, character set and collation.
    Also check compatibility of argument types and return error when applicable.
    Also adjust nullability when applicable.

    @param thd    thread handler
    @returns      false if success, true if error
  */
  virtual bool resolve_type(THD *thd)=0;

  void set_result_field(Field *field) override { result_field= field; }
  bool is_result_field() const override { return true; }
  void save_in_result_field(bool no_conversions) override
  {
    save_in_field(result_field, no_conversions);
  }
  void cleanup() override;
  /*
    This method is used for debug purposes to print the name of an
    item to the debug log. The second use of this method is as
    a helper function of print() and error messages, where it is
    applicable. To suit both goals it should return a meaningful,
    distinguishable and sintactically correct string. This method
    should not be used for runtime type identification, use enum
    {Sum}Functype and Item_func::functype()/Item_sum::sum_func()
    instead.
    Added here, to the parent class of both Item_func and Item_sum_func.

    NOTE: for Items inherited from Item_sum, func_name() return part of
    function name till first argument (including '(') to make difference in
    names for functions with 'distinct' clause and without 'distinct' and
    also to make printing of items inherited from Item_sum uniform.
  */
  virtual const char *func_name() const= 0;
  bool check_gcol_func_processor(uchar *) override { return false; }
};


class Item_ref :public Item_ident
{
protected:
  void set_properties();
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;
public:
  enum Ref_Type { REF, DIRECT_REF, VIEW_REF, OUTER_REF, AGGREGATE_REF };
  Field *result_field;			 /* Save result here */
  Item **ref;
private:
  /**
    'ref' can be set (to non-NULL) in the constructor or afterwards.
    The second case means that we are doing resolution, possibly pointing
    'ref' to a non-permanent Item. To not have 'ref' become dangling at the
    end of execution, and to start clean for the resolution of the next
    execution, 'ref' must be restored to NULL. rollback_item_tree_changes()
    does not handle restoration of Item** values, so we need this dedicated
    Boolean.
  */
  const bool chop_ref;
public:
  Item_ref(Name_resolution_context *context_arg,
           const char *db_arg, const char *table_name_arg,
           const char *field_name_arg)
    :Item_ident(context_arg, db_arg, table_name_arg, field_name_arg),
    result_field(0), ref(NULL), chop_ref(!ref) {}
  Item_ref(const POS &pos,
           const char *db_arg, const char *table_name_arg,
           const char *field_name_arg)
    :Item_ident(pos, db_arg, table_name_arg, field_name_arg),
     result_field(0), ref(NULL), chop_ref(!ref)
  {}

  /*
    This constructor is used in two scenarios:
    A) *item = NULL
      No initialization is performed, fix_fields() call will be necessary.
      
    B) *item points to an Item this Item_ref will refer to. This is 
      used for GROUP BY. fix_fields() will not be called in this case,
      so we call set_properties to make this item "fixed". set_properties
      performs a subset of action Item_ref::fix_fields does, and this subset
      is enough for Item_ref's used in GROUP BY.
    
    TODO we probably fix a superset of problems like in BUG#6658. Check this 
         with Bar, and if we have a more broader set of problems like this.
  */
  Item_ref(Name_resolution_context *context_arg, Item **item,
           const char *table_name_arg, const char *field_name_arg,
           bool alias_of_expr_arg= false);

  /* Constructor need to process subselect with temporary tables (see Item) */
  Item_ref(THD *thd, Item_ref *item)
    :Item_ident(thd, item), result_field(item->result_field), ref(item->ref),
    chop_ref(!ref) {}
  enum Type type() const override { return REF_ITEM; }
  bool eq(const Item *item, bool binary_cmp) const override
  {
    Item *it= ((Item *) item)->real_item();
    return ref && (*ref)->eq(it, binary_cmp);
  }
  double val_real() override;
  longlong val_int() override;
  longlong val_time_temporal() override;
  longlong val_date_temporal() override;
  my_decimal *val_decimal(my_decimal *) override;
  bool val_bool() override;
  String *val_str(String *tmp) override;
  bool val_json(Json_wrapper *result) override;
  bool is_null() override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override;
  double val_result() override;
  longlong val_int_result() override;
  String *str_result(String *tmp) override;
  my_decimal *val_decimal_result(my_decimal *) override;
  bool val_bool_result() override;
  bool is_null_result() override;
  bool send(Protocol *prot, String *tmp) override;
  void make_field(Send_field *field) override;
  bool fix_fields(THD *, Item **) override;
  void fix_after_pullout(SELECT_LEX *parent_select,
                         SELECT_LEX *removed_select) override;
  void save_org_in_field(Field *field) override;
  Item_result result_type() const override { return (*ref)->result_type(); }
  Field *get_tmp_table_field() override
  { return result_field ? result_field : (*ref)->get_tmp_table_field(); }
  Item *get_tmp_table_item(THD *thd) override;
  bool const_item() const override
  {
    return (*ref)->const_item() && (used_tables() == 0);
  }
  table_map used_tables() const override
  {
    return depended_from ? OUTER_REF_TABLE_BIT : (*ref)->used_tables(); 
  }
  void update_used_tables() override
  {
    if (!depended_from)
      (*ref)->update_used_tables();
  }

  table_map not_null_tables() const override
  {
    /*
      It can happen that our 'depended_from' member is set but the
      'depended_from' member of the referenced item is not (example: if a
      field in a subquery belongs to an outer merged view), so we first test
      ours:
    */
    return depended_from ? OUTER_REF_TABLE_BIT : (*ref)->not_null_tables();
  }
  void set_result_field(Field *field) override { result_field= field; }
  bool is_result_field() const override { return true; }
  void save_in_result_field(bool no_conversions) override
  {
    (*ref)->save_in_field(result_field, no_conversions);
  }
  Item *real_item() override
  {
    return ref ? (*ref)->real_item() : this;
  }
  bool walk(Item_processor processor, enum_walk walk, uchar *arg) override
  {
    return
      ((walk & WALK_PREFIX) && (this->*processor)(arg)) ||
      // For having clauses 'ref' will consistently =NULL.
      (ref != NULL ? (*ref)->walk(processor, walk, arg) :false) ||
      ((walk & WALK_POSTFIX) && (this->*processor)(arg));
  }
  Item *transform(Item_transformer, uchar *arg) override;
  Item *compile(Item_analyzer analyzer, uchar **arg_p,
                Item_transformer transformer, uchar *arg_t) override;
  bool explain_subquery_checker(uchar **) override
  {
    /*
      Always return false: we don't need to go deeper into referenced
      expression tree since we have to mark aliased subqueries at
      their original places (select list, derived tables), not by
      references from other expression (order by etc).
    */
    return false;
  }
  void print(String *str, enum_query_type query_type) override;
  void cleanup() override;
  Item_field *field_for_view_update() override
    { return (*ref)->field_for_view_update(); }
  virtual Ref_Type ref_type() const { return REF; }

  // Row emulation: forwarding of ROW-related calls to ref
  uint cols() const override
  {
    return ref && result_type() == ROW_RESULT ? (*ref)->cols() : 1;
  }
  Item *element_index(uint i) override
  {
    return ref && result_type() == ROW_RESULT ? (*ref)->element_index(i) : this;
  }
  Item **addr(uint i) override
  {
    return ref && result_type() == ROW_RESULT ? (*ref)->addr(i) : 0;
  }
  bool check_cols(uint c) override
  {
    return ref && result_type() == ROW_RESULT ? (*ref)->check_cols(c) 
                                              : Item::check_cols(c);
  }
  bool null_inside() override
  {
    return ref && result_type() == ROW_RESULT ? (*ref)->null_inside() : 0;
  }
  void bring_value() override
  {
    if (ref && result_type() == ROW_RESULT)
      (*ref)->bring_value();
  }
  bool get_time(MYSQL_TIME *ltime) override
  {
    DBUG_ASSERT(fixed);
    return (*ref)->get_time(ltime);
  }
  bool basic_const_item() const override
  { return ref && (*ref)->basic_const_item(); }
  bool is_outer_field() const override
  {
    DBUG_ASSERT(fixed);
    DBUG_ASSERT(ref);
    return (*ref)->is_outer_field();
  }

  /**
    Checks if the item tree that ref points to contains a subquery.
  */
  bool has_subquery() const override
  {
    DBUG_ASSERT(ref);
    return (*ref)->has_subquery();
  }

  /**
    Checks if the item tree that ref points to contains a stored program.
  */
  bool has_stored_program() const override
  {
    DBUG_ASSERT(ref);
    return (*ref)->has_stored_program();
  }

  bool created_by_in2exists() const override
  {
    return (*ref)->created_by_in2exists();
  }

  bool repoint_const_outer_ref(uchar *arg) override;
};


/**
  The same as Item_ref, but get value from val_* family of method to get
  value of item on which it referred instead of result* family.
*/
class Item_direct_ref : public Item_ref
{
public:
  Item_direct_ref(Name_resolution_context *context_arg, Item **item,
                  const char *table_name_arg,
                  const char *field_name_arg,
                  bool alias_of_expr_arg= false)
    :Item_ref(context_arg, item, table_name_arg,
              field_name_arg, alias_of_expr_arg)
  {}
  /* Constructor need to process subselect with temporary tables (see Item) */
  Item_direct_ref(THD *thd, Item_direct_ref *item) : Item_ref(thd, item) {}

  double val_real() override;
  longlong val_int() override;
  longlong val_time_temporal() override;
  longlong val_date_temporal() override;
  String *val_str(String *tmp) override;
  my_decimal *val_decimal(my_decimal *) override;
  bool val_bool() override;
  bool is_null() override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override;
  Ref_Type ref_type() const override { return DIRECT_REF; }
};

/**
  Class for fields from derived tables and views.
  The same as Item_direct_ref, but call fix_fields() of reference if
  not called yet.
*/
class Item_direct_view_ref final : public Item_direct_ref
{
  typedef Item_direct_ref super;

public:
  Item_direct_view_ref(Name_resolution_context *context_arg,
                       Item **item,
                       const char *alias_name_arg,
                       const char *table_name_arg,
                       const char *field_name_arg,
                       TABLE_LIST *tl)
    : Item_direct_ref(context_arg, item, alias_name_arg, field_name_arg),
      first_inner_table(NULL)
  {
    orig_table_name= table_name_arg;
    cached_table= tl;
    if (cached_table->is_inner_table_of_outer_join())
    {
      maybe_null= true;
      first_inner_table= cached_table->any_outer_leaf_table();
      // @todo delete this when WL#6570 is implemented
      (*ref)->maybe_null= true;
    }
  }

  /*
    We share one underlying Item_field, so we have to disable
    build_equal_items_for_cond().
    TODO: Implement multiple equality optimization for views.
  */
  bool subst_argument_checker(uchar **) override
  {
    return false;
  }

  bool fix_fields(THD *, Item **) override;
  bool eq(const Item *item, bool) const override;
  Item *get_tmp_table_item(THD *thd) override
  {
    Item *item= Item_ref::get_tmp_table_item(thd);
    item->item_name= item_name;
    return item;
  }
  Ref_Type ref_type() const override { return VIEW_REF; }

  bool check_column_privileges(uchar *arg) override;
  bool mark_field_in_map(uchar *arg) override
  {
    /*
      If this referenced column is marked as used, flag underlying
      selected item from a derived table/view as used.
    */
    Mark_field *mark_field= (Mark_field *)arg;
    if (mark_field->mark != MARK_COLUMNS_NONE)
      // Set the same flag for all the objects that *ref depends on.
      (*ref)->walk(&Item::propagate_set_derived_used,
                   Item::WALK_SUBQUERY_POSTFIX, NULL);

    return false;
  }
  longlong val_int() override;
  double val_real() override;
  my_decimal *val_decimal(my_decimal *dec) override;
  String *val_str(String *str) override;
  bool val_bool() override;
  bool val_json(Json_wrapper *wr) override;
  bool is_null() override;
  bool send(Protocol *prot, String *tmp) override;

protected:
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;

private:
  /// @return true if item is from a null-extended row from an outer join
  bool has_null_row() const
  {
    return first_inner_table && first_inner_table->table->has_null_row();
  }

  /**
    If this column belongs to a view that is an inner table of an outer join,
    then this field points to the first leaf table of the view, otherwise NULL.
  */
  TABLE_LIST *first_inner_table;
};


/*
  Class for outer fields.
  An object of this class is created when the select where the outer field was
  resolved is a grouping one. After it has been fixed the ref field will point
  to either an Item_ref or an Item_direct_ref object which will be used to
  access the field.
  The ref field may also point to an Item_field instance.
  See also comments for the fix_inner_refs() and the
  Item_field::fix_outer_field() functions.
*/

class Item_sum;

class Item_outer_ref final : public Item_direct_ref
{
public:
  Item *outer_ref;
  /* The aggregate function under which this outer ref is used, if any. */
  Item_sum *in_sum_func;
  /*
    TRUE <=> that the outer_ref is already present in the select list
    of the outer select.
  */
  bool found_in_select_list;
  Item_outer_ref(Name_resolution_context *context_arg,
                 Item_ident *ident_arg)
    :Item_direct_ref(context_arg, 0, ident_arg->table_name,
                     ident_arg->field_name),
    outer_ref(ident_arg), in_sum_func(0),
    found_in_select_list(0)
  {
    ref= &outer_ref;
    set_properties();
    fixed= 0;
  }
  Item_outer_ref(Name_resolution_context *context_arg, Item **item,
                 const char *table_name_arg, const char *field_name_arg,
                 bool alias_of_expr_arg)
    :Item_direct_ref(context_arg, item, table_name_arg, field_name_arg,
                     alias_of_expr_arg),
    outer_ref(0), in_sum_func(0), found_in_select_list(1)
  {}
  void save_in_result_field(bool) override
  {
    outer_ref->save_org_in_field(result_field);
  }
  bool fix_fields(THD *, Item **) override;
  void fix_after_pullout(SELECT_LEX *parent_select,
                         SELECT_LEX *removed_select) override;
  table_map used_tables() const override
  {
    return (*ref)->const_item() ? 0 : OUTER_REF_TABLE_BIT;
  }
  table_map not_null_tables() const override { return 0; }

  Ref_Type ref_type() const override { return OUTER_REF; }
};


class Item_in_subselect;


/*
  An object of this class:
   - Converts val_XXX() calls to ref->val_XXX_result() calls, like Item_ref.
   - Sets owner->was_null=TRUE if it has returned a NULL value from any
     val_XXX() function. This allows to inject an Item_ref_null_helper
     object into subquery and then check if the subquery has produced a row
     with NULL value.
*/

class Item_ref_null_helper final : public Item_ref
{
protected:
  Item_in_subselect* owner;
public:
  Item_ref_null_helper(Name_resolution_context *context_arg,
                       Item_in_subselect* master, Item **item,
		       const char *table_name_arg, const char *field_name_arg)
    :Item_ref(context_arg, item, table_name_arg, field_name_arg),
     owner(master) {}
  double val_real() override;
  longlong val_int() override;
  longlong val_time_temporal() override;
  longlong val_date_temporal() override;
  String *val_str(String *s) override;
  my_decimal *val_decimal(my_decimal *) override;
  bool val_bool() override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override;
  void print(String *str, enum_query_type query_type) override;
  /*
    we add RAND_TABLE_BIT to prevent moving this item from HAVING to WHERE
  */
  table_map used_tables() const override
  {
    return (depended_from ?
            OUTER_REF_TABLE_BIT :
            (*ref)->used_tables() | RAND_TABLE_BIT);
  }
};

/*
  The following class is used to optimize comparing of bigint columns.
  We need to save the original item ('ref') to be able to call
  ref->save_in_field(). This is used to create index search keys.
  
  An instance of Item_int_with_ref may have signed or unsigned integer value.
  
*/

class Item_int_with_ref :public Item_int
{
protected:
  Item *ref;
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override
  {
    return ref->save_in_field(field, no_conversions);
  }
public:
  Item_int_with_ref(enum_field_types field_type, longlong i, Item *ref_arg,
                    bool unsigned_arg)
    : Item_int(i), ref(ref_arg)
  {
    set_data_type(field_type);
    unsigned_flag= unsigned_arg;
  }
  Item *clone_item() const override;
  Item *real_item() override { return ref; }
};


/*
  Similar to Item_int_with_ref, but to optimize comparing of temporal columns.
*/
class Item_temporal_with_ref :public Item_int_with_ref
{
public:
  Item_temporal_with_ref(enum_field_types field_type_arg,
                         uint8 decimals_arg, longlong i, Item *ref_arg,
                         bool unsigned_flag)
    : Item_int_with_ref(field_type_arg, i, ref_arg, unsigned_flag)
  {
    decimals= decimals_arg;
  }
  void print(String *str, enum_query_type query_type) override;
  bool get_date(MYSQL_TIME *, my_time_flags_t) override
  {
    DBUG_ASSERT(0);
    return true;
  }
  bool get_time(MYSQL_TIME *) override
  {
    DBUG_ASSERT(0);
    return true;
  }
};


/*
  Item_datetime_with_ref is used to optimize queries like:
    SELECT ... FROM t1 WHERE date_or_datetime_column = 20110101101010;
  The numeric constant is replaced to Item_datetime_with_ref
  by convert_constant_item().
*/
class Item_datetime_with_ref final : public Item_temporal_with_ref
{
public:
  /**
    Constructor for Item_datetime_with_ref.
    @param    field_type_arg Data type: MYSQL_TYPE_DATE or MYSQL_TYPE_DATETIME
    @param    decimals_arg   Number of fractional digits.
    @param    i              Temporal value in packed format.
    @param    ref_arg        Pointer to the original numeric Item.
  */
  Item_datetime_with_ref(enum_field_types field_type_arg,
                         uint8 decimals_arg, longlong i, Item *ref_arg):
    Item_temporal_with_ref(field_type_arg, decimals_arg, i, ref_arg, true)
  {
  }
  Item *clone_item() const override;
  longlong val_date_temporal() override { return val_int(); }
  longlong val_time_temporal() override
  {
    DBUG_ASSERT(0);
    return val_int();
  }
};


/*
  Item_time_with_ref is used to optimize queries like:
    SELECT ... FROM t1 WHERE time_column = 20110101101010;
  The numeric constant is replaced to Item_time_with_ref
  by convert_constant_item().
*/
class Item_time_with_ref final : public Item_temporal_with_ref
{
public:
  /**
    Constructor for Item_time_with_ref.
    @param    decimals_arg   Number of fractional digits.
    @param    i              Temporal value in packed format.
    @param    ref_arg        Pointer to the original numeric Item.
  */
  Item_time_with_ref(uint8 decimals_arg, longlong i, Item *ref_arg):
    Item_temporal_with_ref(MYSQL_TYPE_TIME, decimals_arg, i, ref_arg, 0)
  {
  }
  Item *clone_item() const override;
  longlong val_time_temporal() override { return val_int(); }
  longlong val_date_temporal() override
  {
    DBUG_ASSERT(0);
    return val_int();
  }
};


/**
  Base class to implement typed value caching Item classes

  Item_copy_ classes are very similar to the corresponding Item_
  classes (e.g. Item_copy_int is similar to Item_int) but they add
  the following additional functionality to Item_ :
    1. Nullability
    2. Possibility to store the value not only on instantiation time,
       but also later.
  Item_copy_ classes are a functionality subset of Item_cache_ 
  classes, as e.g. they don't support comparisons with the original Item
  as Item_cache_ classes do.
  Item_copy_ classes are used in GROUP BY calculation.
  TODO: Item_copy should be made an abstract interface and Item_copy_
  classes should inherit both the respective Item_ class and the interface.
  Ideally we should drop Item_copy_ classes altogether and merge 
  their functionality to Item_cache_ (and these should be made to inherit
  from Item_).
*/

class Item_copy :public Item
{
protected:

  /** The original item that is copied */
  Item *item;

  /**
    Stores the result type of the original item, so it can be returned
    without calling the original item's method
  */
  Item_result cached_result_type;

  /**
    Constructor of the Item_copy class

    stores metadata information about the original class as well as a 
    pointer to it.
  */
  Item_copy(Item *i)
  {
    item= i;
    null_value=maybe_null=item->maybe_null;
    decimals=item->decimals;
    max_length=item->max_length;
    item_name= item->item_name;
    set_data_type(item->data_type());
    cached_result_type= item->result_type();
    unsigned_flag= item->unsigned_flag;
    fixed= item->fixed;
    collation.set(item->collation);
  }

  virtual type_conversion_status save_in_field_inner(Field *field,
                                                     bool no_conversions)
    override= 0;

public:
  /**
    Factory method to create the appropriate subclass dependent on the type of
    the original item.

    @param item      the original item.
  */
  static Item_copy *create(Item *item);

  /**
    Update the cache with the value of the original item

    This is the method that updates the cached value.
    It must be explicitly called by the user of this class to store the value
    of the orginal item in the cache.
    @returns false if OK, true on error.
  */
  virtual bool copy(const THD *thd) = 0;

  virtual Item *get_item() { return item; }
  /** All of the subclasses should have the same type tag */
  enum Type type() const override { return COPY_STR_ITEM; }
  enum Item_result result_type() const override { return cached_result_type; }

  void make_field(Send_field *field) override { item->make_field(field); }
  table_map used_tables() const override { return 1; }
  bool const_item() const override { return false; }
  bool is_null() override { return null_value; }

  void no_rows_in_result() override
  {
    item->no_rows_in_result();
  }

  /*  
    Override the methods below as pure virtual to make sure all the 
    sub-classes implement them.
  */

  virtual String *val_str(String *) override= 0;
  virtual my_decimal *val_decimal(my_decimal *) override= 0;
  virtual double val_real() override= 0;
  virtual longlong val_int() override= 0;
  virtual bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate)
    override= 0;
  virtual bool get_time(MYSQL_TIME *ltime)
    override= 0;
  /* purecov: begin deadcode */
  bool val_json(Json_wrapper *) override
  {
    DBUG_ABORT();
    my_error(ER_NOT_SUPPORTED_YET, MYF(0), "item type for JSON");
    return error_json();
  }
  /* purecov: end */
};

/**
 Implementation of a string cache.

 Uses Item::str_value for storage
*/
class Item_copy_string final : public Item_copy
{
protected:
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;
public:
  Item_copy_string (Item *item) : Item_copy(item) {}

  String *val_str(String *) override;
  my_decimal *val_decimal(my_decimal *) override;
  double val_real() override;
  longlong val_int() override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override;
  bool get_time(MYSQL_TIME *ltime) override;
  bool copy(const THD *thd) override;
};

class Item_copy_json final : public Item_copy
{
  Json_wrapper *m_value;
protected:
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;
public:
  explicit Item_copy_json(Item *item);
  virtual ~Item_copy_json();
  bool copy(const THD *thd) override;
  bool val_json(Json_wrapper *) override;
  String *val_str(String *) override;
  my_decimal *val_decimal(my_decimal *) override;
  double val_real() override;
  longlong val_int() override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override;
  bool get_time(MYSQL_TIME *ltime) override;
};


class Item_copy_int : public Item_copy
{
protected:
  longlong cached_value;
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;
public:
  Item_copy_int (Item *i) : Item_copy(i) {}

  String *val_str(String *) override;
  my_decimal *val_decimal(my_decimal *) override;
  double val_real() override
  {
    return null_value ? 0.0 : (double) cached_value;
  }
  longlong val_int() override
  {
    return null_value ? 0LL : cached_value;
  }
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override
  {
    return get_date_from_int(ltime, fuzzydate);
  }
  bool get_time(MYSQL_TIME *ltime) override
  {
    return get_time_from_int(ltime);
  }
  bool copy(const THD *thd) override;
};


class Item_copy_uint final : public Item_copy_int
{
public:
  Item_copy_uint(Item *item) : Item_copy_int(item)
  {
    unsigned_flag= 1;
  }

  String *val_str(String *) override;
  double val_real() override
  {
    return null_value ? 0.0 : (double) (ulonglong) cached_value;
  }
};


class Item_copy_float final : public Item_copy
{
protected:
  double cached_value;
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;
public:
  Item_copy_float(Item *i) : Item_copy(i) {}

  String *val_str(String *) override;
  my_decimal *val_decimal(my_decimal *) override;
  double val_real() override
  {
    return null_value ? 0.0 : cached_value;
  }
  longlong val_int() override
  {
    return (longlong) rint(val_real());
  }
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override
  {
    return get_date_from_real(ltime, fuzzydate);
  }
  bool get_time(MYSQL_TIME *ltime) override
  {
    return get_time_from_real(ltime);
  }
  bool copy(const THD *thd) override;
};


class Item_copy_decimal final : public Item_copy
{
protected:
  my_decimal cached_value;
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;
public:
  Item_copy_decimal(Item *i) : Item_copy(i) {}

  String *val_str(String *) override;
  my_decimal *val_decimal(my_decimal *) override
  {
    return null_value ? NULL: &cached_value; 
  }
  double val_real() override;
  longlong val_int() override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override
  {
    return get_date_from_decimal(ltime, fuzzydate);
  }
  bool get_time(MYSQL_TIME *ltime) override
  {
    return get_time_from_decimal(ltime);
  }
  bool copy(const THD *thd) override;
};


class Cached_item :public Sql_alloc
{
public:
  bool null_value;
  Cached_item() :null_value(0) {}
  virtual bool cmp()= 0;
  virtual ~Cached_item(); /*line -e1509 */
};

class Cached_item_str final : public Cached_item
{
  Item *item;
  uint32 value_max_length;
  String value,tmp_value;
public:
  Cached_item_str(THD *thd, Item *arg);
  bool cmp() override;
  ~Cached_item_str();                           // Deallocate String:s
};


/// Cached_item subclass for JSON values.
class Cached_item_json final : public Cached_item
{
  Item *m_item;              ///< The item whose value to cache.
  Json_wrapper *m_value;     ///< The cached JSON value.
public:
  explicit Cached_item_json(Item *item);
  ~Cached_item_json();
  bool cmp() override;
};


class Cached_item_real final : public Cached_item
{
  Item *item;
  double value;
public:
  Cached_item_real(Item *item_par) :item(item_par),value(0.0) {}
  bool cmp() override;
};

class Cached_item_int final : public Cached_item
{
  Item *item;
  longlong value;
public:
  Cached_item_int(Item *item_par) :item(item_par),value(0) {}
  bool cmp() override;
};

class Cached_item_temporal final : public Cached_item
{
  Item *item;
  longlong value;
public:
  Cached_item_temporal(Item *item_par) :item(item_par), value(0) {}
  bool cmp() override;
};


class Cached_item_decimal final : public Cached_item
{
  Item *item;
  my_decimal value;
public:
  Cached_item_decimal(Item *item_par);
  bool cmp() override;
};

class Cached_item_field final : public Cached_item
{
  uchar *buff;
  Field *field;
  uint length;

public:
#ifndef DBUG_OFF
  void dbug_print() const
  {
    uchar *org_ptr;
    org_ptr= field->ptr;
    fprintf(DBUG_FILE, "new: ");
    field->dbug_print();
    field->ptr= buff;
    fprintf(DBUG_FILE, ", old: ");
    field->dbug_print();
    field->ptr= org_ptr;
    fprintf(DBUG_FILE, "\n");
  }
#endif
  Cached_item_field(Field *arg_field) : field(arg_field)
  {
    field= arg_field;
    /* TODO: take the memory allocation below out of the constructor. */
    buff= (uchar*) sql_calloc(length=field->pack_length());
  }
  bool cmp() override;
};

class Item_default_value final : public Item_field
{
  typedef Item_field super;

protected:
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;

public:
  Item *arg;
  Item_default_value(const POS &pos, Item *a= NULL)
  : super(pos, NULL, NULL, NULL), arg(a)
  {}
  bool itemize(Parse_context *pc, Item **res) override;
  enum Type type() const override { return DEFAULT_VALUE_ITEM; }
  bool eq(const Item *item, bool binary_cmp) const override;
  bool fix_fields(THD *, Item **) override;
  void print(String *str, enum_query_type query_type) override;
  table_map used_tables() const override { return 0; }
  Item *get_tmp_table_item(THD *thd) override { return copy_or_same(thd); }

  bool walk(Item_processor processor, enum_walk walk, uchar *args) override
  {
    return ((walk & WALK_PREFIX) && (this->*processor)(args)) ||
           (arg && arg->walk(processor, walk, args)) ||
           ((walk & WALK_POSTFIX) && (this->*processor)(args));
  }

  bool check_gcol_depend_default_processor(uchar *) override
  { return true; }

  Item *transform(Item_transformer transformer, uchar *args) override;
};

/*
  Item_insert_value -- an implementation of VALUES() function.
  You can use the VALUES(col_name) function in the UPDATE clause
  to refer to column values from the INSERT portion of the INSERT
  ... UPDATE statement. In other words, VALUES(col_name) in the
  UPDATE clause refers to the value of col_name that would be
  inserted, had no duplicate-key conflict occurred.
  In all other places this function returns NULL.
*/

class Item_insert_value final : public Item_field
{
protected:
  type_conversion_status save_in_field_inner(Field *field_arg,
                                             bool no_conversions) override
  {
    return Item_field::save_in_field_inner(field_arg, no_conversions);
  }
public:
  Item *arg;
  Item_insert_value(const POS &pos, Item *a)
    :Item_field(pos, NULL, NULL, NULL),
     arg(a) {}

  bool itemize(Parse_context *pc, Item **res) override
  {
    if (skip_itemize(res))
      return false;
    return super::itemize(pc, res) || arg->itemize(pc, &arg);
  }

  enum Type type() const override { return INSERT_VALUE_ITEM; }
  bool eq(const Item *item, bool binary_cmp) const override;
  bool fix_fields(THD *, Item **) override;
  void print(String *str, enum_query_type query_type) override;
  /*
   We use RAND_TABLE_BIT to prevent Item_insert_value from
   being treated as a constant and precalculated before execution
  */
  table_map used_tables() const override { return RAND_TABLE_BIT; }

  bool walk(Item_processor processor, enum_walk walk, uchar *args) override
  {
    return ((walk & WALK_PREFIX) && (this->*processor)(args)) ||
           arg->walk(processor, walk, args) ||
           ((walk & WALK_POSTFIX) && (this->*processor)(args));
  }
  bool check_gcol_func_processor(uchar *) override { return true; }
};


/**
  Represents NEW/OLD version of field of row which is
  changed/read in trigger.

  Note: For this item main part of actual binding to Field object happens
        not during fix_fields() call (like for Item_field) but right after
        parsing of trigger definition, when table is opened, with special
        setup_field() call. On fix_fields() stage we simply choose one of
        two Field instances representing either OLD or NEW version of this
        field.
*/
class Item_trigger_field final : public Item_field,
                                 private Settable_routine_parameter
{
public:
  /* Is this item represents row from NEW or OLD row ? */
  enum_trigger_variable_type trigger_var_type;
  /* Next in list of all Item_trigger_field's in trigger */
  Item_trigger_field *next_trg_field;
  /*
    Next list of Item_trigger_field's in "sp_head::
    m_list_of_trig_fields_item_lists".
  */
  SQL_I_List<Item_trigger_field> *next_trig_field_list;
  /* Index of the field in the TABLE::field array */
  uint field_idx;
  /* Pointer to an instance of Table_trigger_field_support interface */
  Table_trigger_field_support *triggers;

  Item_trigger_field(Name_resolution_context *context_arg,
                     enum_trigger_variable_type trigger_var_type_arg,
                     const char *field_name_arg,
                     ulong priv, const bool ro)
    :Item_field(context_arg,
               (const char *)NULL, (const char *)NULL, field_name_arg),
     trigger_var_type(trigger_var_type_arg), next_trig_field_list(NULL),
     field_idx((uint)-1), original_privilege(priv),
     want_privilege(priv), table_grants(NULL), read_only (ro)
  {}
  Item_trigger_field(const POS &pos,
                     enum_trigger_variable_type trigger_var_type_arg,
                     const char *field_name_arg,
                     ulong priv, const bool ro)
    :Item_field(pos, NULL, NULL, field_name_arg),
     trigger_var_type(trigger_var_type_arg),
     field_idx((uint)-1), original_privilege(priv),
     want_privilege(priv), table_grants(NULL), read_only (ro)
  {}
  void setup_field(Table_trigger_field_support *table_triggers,
                   GRANT_INFO *table_grant_info);
  enum Type type() const override { return TRIGGER_FIELD_ITEM; }
  bool eq(const Item *item, bool binary_cmp) const override;
  bool fix_fields(THD *, Item **) override;
  void print(String *str, enum_query_type query_type) override;
  table_map used_tables() const override { return 0; }
  Field *get_tmp_table_field() override { return 0; }
  Item *copy_or_same(THD *) override { return this; }
  Item *get_tmp_table_item(THD *thd) override { return copy_or_same(thd); }
  void cleanup() override;
  void set_required_privilege(bool rw) override;

private:
  bool set_value(THD *thd, sp_rcontext *ctx, Item **it) override;

public:
  Settable_routine_parameter *get_settable_routine_parameter() override
  {
    return (read_only ? 0 : this);
  }

  bool set_value(THD *thd, Item **it)
  {
    bool ret= set_value(thd, NULL, it);
    if (!ret)
      bitmap_set_bit(triggers->get_subject_table()->fields_set_during_insert,
                     field_idx);
    return ret;
  }

private:
  /*
    'want_privilege' holds privileges required to perform operation on
    this trigger field (SELECT_ACL if we are going to read it and
    UPDATE_ACL if we are going to update it).  It is initialized at
    parse time but can be updated later if this trigger field is used
    as OUT or INOUT parameter of stored routine (in this case
    set_required_privilege() is called to appropriately update
    want_privilege and cleanup() is responsible for restoring of
    original want_privilege once parameter's value is updated).
  */
  ulong original_privilege;
  ulong want_privilege;
  GRANT_INFO *table_grants;
  /*
    Trigger field is read-only unless it belongs to the NEW row in a
    BEFORE INSERT of BEFORE UPDATE trigger.
  */
  bool read_only;
};


class Item_cache: public Item_basic_constant
{
protected:
  Item *example;
  table_map used_table_map;
  /**
    Field that this object will get value from. This is used by 
    index-based subquery engines to detect and remove the equality injected 
    by IN->EXISTS transformation.
  */  
  Field *cached_field;
  /*
    TRUE <=> cache holds value of the last stored item (i.e actual value).
    store() stores item to be cached and sets this flag to FALSE.
    On the first call of val_xxx function if this flag is set to FALSE the 
    cache_value() will be called to actually cache value of saved item.
    cache_value() will set this flag to TRUE.
  */
  bool value_cached;
public:
  Item_cache():
    example(NULL), used_table_map(0), cached_field(NULL),
    value_cached(false)
  {
    fixed= true;
    maybe_null= true;
    null_value= TRUE;
  }
  Item_cache(enum_field_types field_type_arg):
    example(NULL), used_table_map(0), cached_field(NULL),
    value_cached(false)
  {
    set_data_type(field_type_arg);
    fixed= true;
    maybe_null= true;
    null_value= TRUE;
  }

  void set_used_tables(table_map map) { used_table_map= map; }

  void fix_after_pullout(SELECT_LEX *parent_select,
                         SELECT_LEX *removed_select) override
  {
    if (example == NULL)
      return;
    example->fix_after_pullout(parent_select, removed_select);
    used_table_map= example->used_tables();
  }

  virtual bool allocate(uint) { return 0; }
  virtual bool setup(Item *item)
  {
    example= item;
    max_length= item->max_length;
    decimals= item->decimals;
    collation.set(item->collation);
    unsigned_flag= item->unsigned_flag;
    with_subselect|= item->has_subquery();
    with_stored_program|= item->has_stored_program();
    if (item->type() == FIELD_ITEM)
    {
      cached_field= ((Item_field *)item)->field;
      if (((Item_field *)item)->table_ref)
        used_table_map= ((Item_field *)item)->table_ref->map();
    }
    else
    {
      used_table_map= item->used_tables();
    }
    return 0;
  };
  enum Type type() const override { return CACHE_ITEM; }
  static Item_cache* get_cache(const Item *item);
  static Item_cache* get_cache(const Item* item, const Item_result type);
  table_map used_tables() const override { return used_table_map; }
  virtual void keep_array() {}
  void print(String *str, enum_query_type query_type) override;
  bool eq_def(Field *field)
  {
    return cached_field && cached_field->eq_def(field);
  }
  bool eq(const Item *item, bool) const override { return this == item; }
  /**
     Check if saved item has a non-NULL value.
     Will cache value of saved item if not already done. 
     @return TRUE if cached value is non-NULL.
   */
  bool has_value();

  /** 
    If this item caches a field value, return pointer to underlying field.

    @return Pointer to field, or NULL if this is not a cache for a field value.
  */
  Field* field() { return cached_field; }

  /**
    Assigns to the cache the expression to be cached. Does not evaluate it.
    @param item  the expression to be cached
  */
  virtual void store(Item *item);

  /**
    Force an item to be null. Used for empty subqueries to avoid attempts to
    evaluate expressions which could have uninitialized columns due to
    bypassing the subquery exec.
  */
  void store_null()
  {
    DBUG_ASSERT(maybe_null);
    value_cached= true;
    null_value= true;
  }

  virtual bool cache_value()= 0;
  bool basic_const_item() const override
  { return MY_TEST(example && example->basic_const_item());}
  bool walk(Item_processor processor, enum_walk walk, uchar *arg) override;
  virtual void clear() { null_value= TRUE; value_cached= FALSE; }
  bool is_null() override
  { return value_cached ? null_value : example->is_null(); }
  bool check_gcol_func_processor(uchar *) override { return true; }
  Item_result result_type() const override
  {
    if (!example)
      return INT_RESULT;
    return Field::result_merge_type(example->data_type());
  }
};


class Item_cache_int final : public Item_cache
{
protected:
  longlong value;
public:
  Item_cache_int(): Item_cache(MYSQL_TYPE_LONGLONG),
    value(0)
  {}
  Item_cache_int(enum_field_types field_type_arg):
    Item_cache(field_type_arg), value(0)
  {}

  void store(Item *item) override { Item_cache::store(item); }
  void store(Item *item, longlong val_arg);
  double val_real() override;
  longlong val_int() override;
  longlong val_time_temporal() override { return val_int(); }
  longlong val_date_temporal() override { return val_int(); }
  String *val_str(String *str) override;
  my_decimal *val_decimal(my_decimal *) override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override
  {
    return get_date_from_int(ltime, fuzzydate);
  }
  bool get_time(MYSQL_TIME *ltime) override
  {
    return get_time_from_int(ltime);
  }
  enum Item_result result_type() const override { return INT_RESULT; }
  bool cache_value() override;
};


class Item_cache_real final : public Item_cache
{
  double value;
public:
  Item_cache_real(): Item_cache(MYSQL_TYPE_DOUBLE), value(0)
  {}

  double val_real() override;
  longlong val_int() override;
  String *val_str(String *str) override;
  my_decimal *val_decimal(my_decimal *) override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override
  {
    return get_date_from_real(ltime, fuzzydate);
  }
  bool get_time(MYSQL_TIME *ltime) override
  {
    return get_time_from_real(ltime);
  }
  enum Item_result result_type() const override { return REAL_RESULT; }
  bool cache_value() override;
};


class Item_cache_decimal final : public Item_cache
{
protected:
  my_decimal decimal_value;
public:
  Item_cache_decimal(): Item_cache(MYSQL_TYPE_NEWDECIMAL)
  {}

  double val_real() override;
  longlong val_int() override;
  String *val_str(String *str) override;
  my_decimal *val_decimal(my_decimal *) override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override
  {
    return get_date_from_decimal(ltime, fuzzydate);
  }
  bool get_time(MYSQL_TIME *ltime) override
  {
    return get_time_from_decimal(ltime);
  }
  enum Item_result result_type() const override { return DECIMAL_RESULT; }
  bool cache_value() override;
};


class Item_cache_str final : public Item_cache
{
  char buffer[STRING_BUFFER_USUAL_SIZE];
  String *value, value_buff;
  bool is_varbinary;

protected:
  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;

public:
  Item_cache_str(const Item *item) :
    Item_cache(item->data_type()), value(0),
    is_varbinary(item->type() == FIELD_ITEM &&
                 data_type() == MYSQL_TYPE_VARCHAR &&
                 !((const Item_field *) item)->field->has_charset())
  {
    collation.set(const_cast<DTCollation&>(item->collation));
  }
  double val_real() override;
  longlong val_int() override;
  String *val_str(String *) override;
  my_decimal *val_decimal(my_decimal *) override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override
  {
    return get_date_from_string(ltime, fuzzydate);
  }
  bool get_time(MYSQL_TIME *ltime) override
  {
    return get_time_from_string(ltime);
  }
  enum Item_result result_type() const override { return STRING_RESULT; }
  const CHARSET_INFO *charset() const { return value->charset(); };
  bool cache_value() override;
};

class Item_cache_row final : public Item_cache
{
  Item_cache  **values;
  uint item_count;
  bool save_array;
public:
  Item_cache_row()
    :Item_cache(), values(0), item_count(2),
    save_array(false) {}

  /**
    'allocate' is only used in Item_cache_row::setup()
  */
  bool allocate(uint num) override;
  /*
    'setup' is needed only by row => it not called by simple row subselect
    (only by IN subselect (in subselect optimizer))
  */
  bool setup(Item *item) override;
  void store(Item *item) override;
  void illegal_method_call(const char *) const MY_ATTRIBUTE((cold));
  void make_field(Send_field *) override
  {
    illegal_method_call("make_field");
  };
  double val_real() override
  {
    illegal_method_call("val_real");
    return 0;
  };
  longlong val_int() override
  {
    illegal_method_call("val_int");
    return 0;
  };
  String *val_str(String *) override
  {
    illegal_method_call("val_str");
    return 0;
  };
  my_decimal *val_decimal(my_decimal *) override
  {
    illegal_method_call("val_decimal");
    return 0;
  };
  bool get_date(MYSQL_TIME *, my_time_flags_t) override
  {
    illegal_method_call("get_date");
    return true;
  }
  bool get_time(MYSQL_TIME *) override
  {
    illegal_method_call("get_time");
    return true;
  }

  enum Item_result result_type() const override { return ROW_RESULT; }

  uint cols() const override { return item_count; }
  Item *element_index(uint i) override { return values[i]; }
  Item **addr(uint i) override { return (Item **) (values + i); }
  bool check_cols(uint c) override;
  bool null_inside() override;
  void bring_value() override;
  void keep_array() override { save_array= true; }
  void cleanup() override
  {
    DBUG_ENTER("Item_cache_row::cleanup");
    Item_cache::cleanup();
    if (save_array)
      memset(values, 0, item_count*sizeof(Item**));
    else
      values= 0;
    DBUG_VOID_RETURN;
  }
  bool cache_value() override;
};


class Item_cache_datetime: public Item_cache
{
protected:
  String str_value;
  longlong int_value;
  bool str_value_cached;
public:
  Item_cache_datetime(enum_field_types field_type_arg):
    Item_cache(field_type_arg), int_value(0), str_value_cached(0)
  {
    cmp_context= STRING_RESULT;
  }

  void store(Item *item, longlong val_arg);
  void store(Item *item) override;
  double val_real() override;
  longlong val_int() override;
  longlong val_time_temporal() override;
  longlong val_date_temporal() override;
  String *val_str(String *str) override;
  my_decimal *val_decimal(my_decimal *) override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override;
  bool get_time(MYSQL_TIME *ltime) override;
  enum Item_result result_type() const override { return STRING_RESULT; }
  /*
    In order to avoid INT <-> STRING conversion of a DATETIME value
    two cache_value functions are introduced. One (cache_value) caches STRING
    value, another (cache_value_int) - INT value. Thus this cache item
    completely relies on the ability of the underlying item to do the
    correct conversion.
  */
  bool cache_value_int();
  bool cache_value() override;
  void clear() override { Item_cache::clear(); str_value_cached= false; }
};


/// An item cache for values of type JSON.
class Item_cache_json: public Item_cache
{
  Json_wrapper *m_value;
public:
  Item_cache_json();
  ~Item_cache_json();
  bool cache_value() override;
  bool val_json(Json_wrapper *wr) override;
  longlong val_int() override;
  String *val_str(String *str) override;
  Item_result result_type() const override { return STRING_RESULT; }

  double val_real() override;
  my_decimal *val_decimal(my_decimal *val) override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override;
  bool get_time(MYSQL_TIME *ltime) override;
};


/**
  Item_type_holder stores type, name, length of Item for UNIONS &
  derived tables.

  Item_type_holder do not need cleanup() because its time of live limited by
  single SP/PS execution.
*/
class Item_type_holder final : public Item
{
protected:
  TYPELIB *enum_set_typelib;
  Field::geometry_type geometry_type;

  void get_full_info(Item *item);

  /* It is used to count decimal precision in join_types */
  int prev_decimal_int_part;
public:
  Item_type_holder(THD*, Item*);

  Item_result result_type() const override;
  enum Type type() const override { return TYPE_HOLDER; }
  double val_real() override;
  longlong val_int() override;
  my_decimal *val_decimal(my_decimal *) override;
  String *val_str(String *) override;
  bool get_date(MYSQL_TIME *, my_time_flags_t) override
  {
    DBUG_ASSERT(0);
    return true;
  }
  bool get_time(MYSQL_TIME *) override
  {
    DBUG_ASSERT(0);
    return true;
  }
  bool join_types(THD *, Item *);
  Field *make_field_by_type(TABLE *table);
  static uint32 display_length(Item *item);
  static enum_field_types real_data_type(Item *);
  Field::geometry_type get_geometry_type() const override
  { return geometry_type; };
  void make_field(Send_field *field) override
  {
    Item::make_field(field);
    // Item_type_holder is used for unions and effectively sends Fields
    field->field= true;
  }
};


extern Cached_item *new_Cached_item(THD *thd, Item *item,
                                    bool use_result_field);
extern Item_result item_cmp_type(Item_result a,Item_result b);
extern bool resolve_const_item(THD *thd, Item **ref, Item *cmp_item);
extern int stored_field_cmp_to_item(THD *thd, Field *field, Item *item);

extern const String my_null_string;
void convert_and_print(String *from_str, String *to_str,
                       const CHARSET_INFO *to_cs);
#ifndef DBUG_OFF
bool is_fixed_or_outer_ref(const Item *ref);
#endif

#endif /* ITEM_INCLUDED */
