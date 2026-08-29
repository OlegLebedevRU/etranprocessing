#ifndef DB_DISCOVERY_H
#define DB_DISCOVERY_H

#include "xml_parser.h"
#include <stdbool.h>

// Automatically discover active terminal directory, log, and DBConfig.xml
bool db_discovery_find_terminal_config(DBConfig* out_cfg, bool verbose);

#endif // DB_DISCOVERY_H
