# Copyright (c) 2026 lefred
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.

MYSQL_ADD_PLUGIN(statement_digest statement_digest.cc
                 RECOMPILE_FOR_EMBEDDED MODULE_ONLY)

