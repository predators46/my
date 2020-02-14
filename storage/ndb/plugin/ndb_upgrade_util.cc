/*
   Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

// Implements the functions declared in ndb_upgrade_util.h
#include "storage/ndb/plugin/ndb_upgrade_util.h"

#include "include/my_dbug.h"
#include "storage/ndb/include/ndb_version.h"
#include "storage/ndb/include/ndbapi/ndb_cluster_connection.hpp"

// The first 8.0 GA version for MySQL NDBCluster
static constexpr unsigned int NDB_VERSION_8_0_19 = NDB_MAKE_VERSION(8, 0, 19);

extern Ndb_cluster_connection *g_ndb_cluster_connection;
extern bool opt_ndb_schema_dist_upgrade_allowed;

/**
   @brief Check if it is okay to upgrade the ndb_schema table
   @return true if it is okay to upgrade, false otherwise
 */
bool ndb_allow_ndb_schema_upgrade() {
  DBUG_TRACE;

  // Find out the minimum API version connected to cluster.
  const unsigned int min_api_version =
      g_ndb_cluster_connection->get_min_api_version();

  if (min_api_version == 0) {
    // Minimum connected API version is not available in the NDBAPI, which
    // implies that a data node with a lower version is connected to the
    // cluster. The table upgrade is denied in this case as there is a chance
    // that a MySQL Server with lower version is connected to the cluster. The
    // table will be upgraded after all the data nodes are upgraded.
    // This requirement means that the ndb_schema table upgrade will be denied
    // even if there is one connected data node, running a version, that does
    // not have the support for maintaining the min_api_version in NDBAPI.
    return false;
  }

  // Allow ndb_schema table upgrade if all API nodes connected are atleast
  // on 8.0.19 and the ndb-schema-dist-upgrade-allowed option is enabled
  return (min_api_version >= NDB_VERSION_8_0_19) &&
         opt_ndb_schema_dist_upgrade_allowed;
}
