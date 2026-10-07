# Copyright (c) 2019, MariaDB corporation. All rights reserved.
# Copyright (c) 2026, lefred (Frédéric Descamps). All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1335  USA


cmake_minimum_required(VERSION 3.12)

project(statement_digest)

set(PLUGIN_NAME "statement_digest")
set(PLUGIN_AUTHOR "lefred")
set(PLUGIN_VERSION "0.2.2")
set(PLUGIN_HEX_VERSION "0x000202")
set(PLUGIN_DESCRIPTION "Functions invoking MariaDB's parser and native digest implementation")
set(PLUGIN_LICENSE "PLUGIN_LICENSE_GPL")
SET(PLUGIN_MATURITY "MariaDB_PLUGIN_MATURITY_BETA")

set(TYPE_PLUGIN_SOURCES
    statement_digest.cc
)

#
# New MariaDB external plugin infrastructure.
# QUIET is important so older MariaDB versions can fall back to
# MYSQL_ADD_PLUGIN().
#
find_package(mariadb-plugin QUIET)

if(COMMAND MARIADB_ADD_PLUGIN)

    message(STATUS "Building ${PLUGIN_NAME} using MARIADB_ADD_PLUGIN")

    MARIADB_ADD_PLUGIN(${PLUGIN_NAME}
        ${TYPE_PLUGIN_SOURCES}
        MODULE_ONLY
        RECOMPILE_FOR_EMBEDDED
        AUTHOR "${PLUGIN_AUTHOR}"
        VERSION "${PLUGIN_VERSION}"
        DESCRIPTION "${PLUGIN_DESCRIPTION}"
    )

    #
    # MARIADB_ADD_PLUGIN does not provide a maturity definition.
    #
    target_compile_definitions(${PLUGIN_NAME} PRIVATE
        PLUGIN_MATURITY=${PLUGIN_MATURITY}
    )

elseif(COMMAND MYSQL_ADD_PLUGIN)

    message(STATUS "Building ${PLUGIN_NAME} using legacy MYSQL_ADD_PLUGIN")

    MYSQL_ADD_PLUGIN(${PLUGIN_NAME}
        ${TYPE_PLUGIN_SOURCES}
        MODULE_ONLY
        RECOMPILE_FOR_EMBEDDED
        COMPONENT Test
    )

    #
    # MARIADB_ADD_PLUGIN provides these definitions automatically.
    # Legacy MYSQL_ADD_PLUGIN does not, so provide them explicitly.
    #
    target_compile_definitions(${PLUGIN_NAME} PRIVATE
        PLUGIN_AUTHOR="${PLUGIN_AUTHOR}"
        PLUGIN_DESCRIPTION="${PLUGIN_DESCRIPTION}"
        PLUGIN_VERSION="${PLUGIN_VERSION}"
        PLUGIN_HEX_VERSION=${PLUGIN_HEX_VERSION}
        PLUGIN_LICENSE=${PLUGIN_LICENSE}
        PLUGIN_MATURITY=${PLUGIN_MATURITY}
        PLUGIN_NAME="${PLUGIN_NAME}"
    )

else()

    message(FATAL_ERROR
        "Cannot build ${PLUGIN_NAME}: neither MARIADB_ADD_PLUGIN nor "
        "MYSQL_ADD_PLUGIN is available."
    )

endif()

include(CPack)

