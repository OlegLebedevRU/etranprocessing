#include "db_odbc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "odbc32.lib")

static const char* ODBC_CANDIDATE_DRIVERS[] = {
    "ODBC Driver 18 for SQL Server",
    "ODBC Driver 17 for SQL Server",
    "ODBC Driver 13 for SQL Server",
    "ODBC Driver 11 for SQL Server",
    "SQL Server Native Client 11.0",
    "SQL Server Native Client 10.0",
    "SQL Server",
    NULL
};

static void get_odbc_error(SQLSMALLINT handleType, SQLHANDLE handle, char* out_msg, size_t out_max) {
    if (!out_msg || out_max == 0) return;
    out_msg[0] = '\0';

    SQLCHAR sqlState[6];
    SQLINTEGER nativeError;
    SQLCHAR messageText[512];
    SQLSMALLINT textLength;
    SQLSMALLINT i = 1;

    size_t written = 0;
    while (SQLGetDiagRecA(handleType, handle, i++, sqlState, &nativeError,
                          messageText, sizeof(messageText), &textLength) == SQL_SUCCESS) {
        int n = snprintf(out_msg + written, out_max - written,
                         "[%s] (Error %ld): %s\n", sqlState, (long)nativeError, messageText);
        if (n <= 0 || (size_t)n >= out_max - written) break;
        written += (size_t)n;
    }
}

bool db_odbc_connect(const DBConfig* cfg, SQLHENV* out_hEnv, SQLHDBC* out_hDbc,
                     char* err_msg, size_t err_msg_max, bool verbose) {
    if (!cfg || !out_hEnv || !out_hDbc) return false;
    *out_hEnv = NULL;
    *out_hDbc = NULL;

    if (err_msg && err_msg_max > 0) err_msg[0] = '\0';

    SQLHENV hEnv = NULL;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv);
    if (!SQL_SUCCEEDED(ret)) {
        if (err_msg) snprintf(err_msg, err_msg_max, "Failed to allocate ODBC Environment Handle");
        return false;
    }

    ret = SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (!SQL_SUCCEEDED(ret)) {
        get_odbc_error(SQL_HANDLE_ENV, hEnv, err_msg, err_msg_max);
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        return false;
    }

    SQLHDBC hDbc = NULL;
    ret = SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc);
    if (!SQL_SUCCEEDED(ret)) {
        get_odbc_error(SQL_HANDLE_ENV, hEnv, err_msg, err_msg_max);
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        return false;
    }

    // Set connection timeout to 10 seconds
    SQLSetConnectAttrA(hDbc, SQL_ATTR_LOGIN_TIMEOUT, (SQLPOINTER)10, 0);

    bool connected = false;
    char last_driver_err[1024] = {0};

    for (int i = 0; ODBC_CANDIDATE_DRIVERS[i] != NULL; i++) {
        const char* driver = ODBC_CANDIDATE_DRIVERS[i];
        char conn_str[1024];

        if (cfg->auth_type == 0 || strlen(cfg->user) == 0) {
            // Windows Authentication (Trusted Connection)
            if (strstr(driver, "18") != NULL) {
                snprintf(conn_str, sizeof(conn_str),
                         "Driver={%s};Server=%s;Database=%s;Trusted_Connection=yes;TrustServerCertificate=yes;",
                         driver, cfg->server, cfg->database);
            } else {
                snprintf(conn_str, sizeof(conn_str),
                         "Driver={%s};Server=%s;Database=%s;Trusted_Connection=yes;",
                         driver, cfg->server, cfg->database);
            }
        } else {
            // SQL Authentication
            if (strstr(driver, "18") != NULL) {
                snprintf(conn_str, sizeof(conn_str),
                         "Driver={%s};Server=%s;Database=%s;Uid=%s;Pwd=%s;TrustServerCertificate=yes;",
                         driver, cfg->server, cfg->database, cfg->user, cfg->password);
            } else {
                snprintf(conn_str, sizeof(conn_str),
                         "Driver={%s};Server=%s;Database=%s;Uid=%s;Pwd=%s;",
                         driver, cfg->server, cfg->database, cfg->user, cfg->password);
            }
        }

        if (verbose) {
            printf("[odbc] Attempting connection with driver: [%s]...\n", driver);
        }

        char out_conn[1024];
        SQLSMALLINT out_len;
        ret = SQLDriverConnectA(hDbc, NULL, (SQLCHAR*)conn_str, SQL_NTS,
                                (SQLCHAR*)out_conn, sizeof(out_conn), &out_len,
                                SQL_DRIVER_NOPROMPT);

        if (SQL_SUCCEEDED(ret)) {
            connected = true;
            if (verbose) {
                printf("[odbc] Connected successfully using driver: [%s]\n", driver);
            }
            break;
        } else {
            get_odbc_error(SQL_HANDLE_DBC, hDbc, last_driver_err, sizeof(last_driver_err));
            if (verbose) {
                printf("[odbc] Driver [%s] connection failed: %s", driver, last_driver_err);
            }
        }
    }

    if (!connected) {
        if (err_msg) {
            snprintf(err_msg, err_msg_max,
                     "Failed to connect to MS SQL Server '%s' (Database: '%s'):\n%s",
                     cfg->server, cfg->database, last_driver_err);
        }
        SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        return false;
    }

    *out_hEnv = hEnv;
    *out_hDbc = hDbc;
    return true;
}

bool db_odbc_execute(SQLHDBC hDbc, const char* sql, int limit,
                     QueryResult* out_result, bool verbose) {
    if (!hDbc || !sql || !out_result) return false;
    query_result_init(out_result, limit);

    SQLHSTMT hStmt = NULL;
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
    if (!SQL_SUCCEEDED(ret)) {
        get_odbc_error(SQL_HANDLE_DBC, hDbc, out_result->error_message, sizeof(out_result->error_message));
        return false;
    }

    // 1. Set transaction isolation level to READ UNCOMMITTED (avoids locking active terminal services)
    SQLExecDirectA(hStmt, (SQLCHAR*)"SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED", SQL_NTS);
    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    hStmt = NULL;

    ret = SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
    if (!SQL_SUCCEEDED(ret)) {
        get_odbc_error(SQL_HANDLE_DBC, hDbc, out_result->error_message, sizeof(out_result->error_message));
        return false;
    }

    // Set max rows limit on statement handle if applicable
    if (limit > 0) {
        SQLSetStmtAttr(hStmt, SQL_ATTR_MAX_ROWS, (SQLPOINTER)(INT_PTR)limit, 0);
    }

    if (verbose) {
        printf("[odbc] Executing query: %s\n", sql);
    }

    ret = SQLExecDirectA(hStmt, (SQLCHAR*)sql, SQL_NTS);
    if (!SQL_SUCCEEDED(ret) && ret != SQL_NO_DATA) {
        get_odbc_error(SQL_HANDLE_STMT, hStmt, out_result->error_message, sizeof(out_result->error_message));
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    // Check if result set exists
    SQLSMALLINT col_count = 0;
    SQLNumResultCols(hStmt, &col_count);

    if (col_count == 0) {
        // No result set (e.g. SP call with only DML or status)
        SQLLEN row_count = 0;
        SQLRowCount(hStmt, &row_count);
        out_result->affected_rows = (int)row_count;
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return true;
    }

    if (!query_result_set_columns(out_result, col_count)) {
        snprintf(out_result->error_message, sizeof(out_result->error_message), "Failed to allocate column metadata");
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    for (SQLSMALLINT i = 1; i <= col_count; i++) {
        SQLCHAR col_name[128];
        SQLSMALLINT name_len = 0;
        SQLSMALLINT data_type = 0;
        SQLULEN col_size = 0;
        SQLSMALLINT dec_digits = 0;
        SQLSMALLINT nullable = 0;

        SQLDescribeColA(hStmt, i, col_name, sizeof(col_name), &name_len,
                        &data_type, &col_size, &dec_digits, &nullable);

        if (name_len == 0) {
            snprintf(out_result->columns[i - 1].name, sizeof(out_result->columns[i - 1].name), "Column%d", i);
        } else {
            strncpy(out_result->columns[i - 1].name, (char*)col_name, sizeof(out_result->columns[i - 1].name) - 1);
        }
        out_result->columns[i - 1].sql_type = data_type;
        out_result->columns[i - 1].max_content_len = strlen(out_result->columns[i - 1].name);
    }

    char** row_values = (char**)calloc((size_t)col_count, sizeof(char*));
    bool* row_nulls = (bool*)calloc((size_t)col_count, sizeof(bool));
    char (*cell_bufs)[4096] = (char (*)[4096])calloc((size_t)col_count, sizeof(char[4096]));

    while ((ret = SQLFetch(hStmt)) == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        for (SQLSMALLINT i = 1; i <= col_count; i++) {
            SQLLEN indicator = 0;
            cell_bufs[i - 1][0] = '\0';
            SQLRETURN get_ret = SQLGetData(hStmt, i, SQL_C_CHAR, cell_bufs[i - 1], sizeof(cell_bufs[i - 1]) - 1, &indicator);

            if (indicator == SQL_NULL_DATA || !SQL_SUCCEEDED(get_ret)) {
                row_nulls[i - 1] = true;
                row_values[i - 1] = NULL;
            } else {
                row_nulls[i - 1] = false;
                row_values[i - 1] = cell_bufs[i - 1];
            }
        }

        if (!query_result_add_row(out_result, row_values, row_nulls)) {
            break; // Limit reached
        }
    }

    free(cell_bufs);
    free(row_values);
    free(row_nulls);
    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return true;
}

void db_odbc_disconnect(SQLHENV hEnv, SQLHDBC hDbc) {
    if (hDbc) {
        SQLDisconnect(hDbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
    }
    if (hEnv) {
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
    }
}
