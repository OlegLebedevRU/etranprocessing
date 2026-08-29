#ifndef DB_ODBC_H
#define DB_ODBC_H

#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <stdbool.h>
#include "xml_parser.h"
#include "output_formatter.h"

// Connect to MS SQL Server using best available ODBC driver
bool db_odbc_connect(const DBConfig* cfg, SQLHENV* out_hEnv, SQLHDBC* out_hDbc,
                     char* err_msg, size_t err_msg_max, bool verbose);

// Execute query and populate QueryResult
bool db_odbc_execute(SQLHDBC hDbc, const char* sql, int limit,
                     QueryResult* out_result, bool verbose);

// Disconnect and release ODBC handles
void db_odbc_disconnect(SQLHENV hEnv, SQLHDBC hDbc);

#endif // DB_ODBC_H
