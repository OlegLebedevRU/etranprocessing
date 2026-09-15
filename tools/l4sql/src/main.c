#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "xml_parser.h"
#include "db_discovery.h"
#include "sql_validator.h"
#include "db_odbc.h"
#include "output_formatter.h"
#include "auth_adaptive.h"

#define L4SQL_VERSION "1.7.6"

static void print_version() {
    printf("l4sql version %s (Leo4 / Platerra Terminal SQL Utility)\n", L4SQL_VERSION);
}

static void print_help(const char* prog) {
    printf("l4sql - Native MS SQL client for Leo4 & Platerra terminals\n\n");
    printf("Usage:\n");
    printf("  %s [options] <query|table>\n", prog);
    printf("  %s [options] select <table_name>\n", prog);
    printf("  %s [options] \"EXEC l4_<proc_name> [args]\"\n\n", prog);

    printf("Security Policy:\n");
    printf("  * Read-only mode: Only SELECT, WITH (CTE), SHOW, DESCRIBE are permitted for queries.\n");
    printf("  * Modifying DML/DDL (INSERT, UPDATE, DELETE, DROP, ALTER, TRUNCATE, etc.) is strictly BLOCKED.\n");
    printf("  * Stored Procedures: Only procedures with 'l4_' prefix (e.g. 'EXEC l4_UpdateSysVersion') are permitted.\n\n");

    printf("Auto-Discovery Flow:\n");
    printf("  If --server and --db are omitted, l4sql automatically scans drive roots (C:\\, D:\\, ...),\n");
    printf("  locates terminal folders (*platerra*, *postomat*, *postamat*), inspects the latest log file\n");
    printf("  (PlaterraTerminal.log), and loads connection parameters from DBConfig.xml.\n\n");

    printf("Options:\n");
    printf("  -s, --server <host>        SQL Server instance (default from DBConfig.xml or (local)\\sqlexpress)\n");
    printf("  -d, --db, --database <db>  Database name (default from DBConfig.xml or Terminal)\n");
    printf("  -u, --user <username>      SQL user for SQL Authentication\n");
    printf("  -p, --pass <password>      SQL password for SQL Authentication\n");
    printf("  -a, --auth <0|1>           Authentication type (0 = Windows Auth / Trusted, 1 = SQL Auth)\n");
    printf("  -l, --limit <N>            Row limit (default: 100, maximum: 1000)\n");
    printf("  -f, --format <fmt>         Output format: table (default), csv, json\n");
    printf("  -j, --json                 Shortcut for --format json\n");
    printf("  -c, --csv                  Shortcut for --format csv\n");
    printf("  -n, --no-headers           Suppress column headers and summary footer\n");
    printf("  -G, --grant-system-access  Self-provisioning: grant NT AUTHORITY\\SYSTEM db_datareader rights\n");
    printf("  -v, --verbose              Enable verbose discovery and connection logging\n");
    printf("  -V, --version              Print version information and exit\n");
    printf("  -h, --help                 Display this help message and exit\n\n");

    printf("Examples:\n");
    printf("  l4sql tb_Variables\n");
    printf("  l4sql select Terminal.tb_Variables\n");
    printf("  l4sql \"SELECT TOP 10 Id, Name, Value FROM tb_Variables WHERE Name LIKE 'sys%%'\"\n");
    printf("  l4sql --json tb_Variables\n");
    printf("  l4sql --limit 20 --csv tb_Counters\n");
    printf("  l4sql \"EXEC dbo.l4_UpdateSysVersion '3.22.4711.586'\"\n");
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    DBConfig cfg;
    db_config_init(&cfg);

    bool server_set = false;
    bool db_set = false;
    bool user_set = false;
    bool pass_set = false;
    bool auth_set = false;

    int limit = DEFAULT_ROW_LIMIT;
    OutputFormat format = OUTPUT_FORMAT_TABLE;
    bool no_headers = false;
    bool verbose = false;
    bool grant_system_access = false;

    char query_buf[8192] = {0};
    size_t query_len = 0;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0 || strcmp(arg, "/?") == 0) {
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            verbose = true;
            continue;
        }
        if (strcmp(arg, "-G") == 0 || strcmp(arg, "--grant-system-access") == 0) {
            grant_system_access = true;
            continue;
        }
        if (strcmp(arg, "-j") == 0 || strcmp(arg, "--json") == 0) {
            format = OUTPUT_FORMAT_JSON;
            continue;
        }
        if (strcmp(arg, "-c") == 0 || strcmp(arg, "--csv") == 0) {
            format = OUTPUT_FORMAT_CSV;
            continue;
        }
        if (strcmp(arg, "-n") == 0 || strcmp(arg, "--no-headers") == 0) {
            no_headers = true;
            continue;
        }
        if (strcmp(arg, "-l") == 0 || strcmp(arg, "--limit") == 0) {
            if (i + 1 < argc) {
                limit = atoi(argv[++i]);
                if (limit <= 0) limit = DEFAULT_ROW_LIMIT;
                if (limit > MAX_ALLOWED_ROW_LIMIT) {
                    if (verbose) {
                        fprintf(stderr, "[notice] Requested limit %d exceeds max %d, clamping to %d\n",
                                limit, MAX_ALLOWED_ROW_LIMIT, MAX_ALLOWED_ROW_LIMIT);
                    }
                    limit = MAX_ALLOWED_ROW_LIMIT;
                }
            }
            continue;
        }
        if (strcmp(arg, "-f") == 0 || strcmp(arg, "--format") == 0) {
            if (i + 1 < argc) {
                const char* fmt = argv[++i];
                if (_stricmp(fmt, "json") == 0) format = OUTPUT_FORMAT_JSON;
                else if (_stricmp(fmt, "csv") == 0) format = OUTPUT_FORMAT_CSV;
                else format = OUTPUT_FORMAT_TABLE;
            }
            continue;
        }
        if (strcmp(arg, "-s") == 0 || strcmp(arg, "--server") == 0) {
            if (i + 1 < argc) {
                strncpy(cfg.server, argv[++i], sizeof(cfg.server) - 1);
                server_set = true;
            }
            continue;
        }
        if (strcmp(arg, "-d") == 0 || strcmp(arg, "--db") == 0 || strcmp(arg, "--database") == 0) {
            if (i + 1 < argc) {
                strncpy(cfg.database, argv[++i], sizeof(cfg.database) - 1);
                db_set = true;
            }
            continue;
        }
        if (strcmp(arg, "-u") == 0 || strcmp(arg, "--user") == 0) {
            if (i + 1 < argc) {
                strncpy(cfg.user, argv[++i], sizeof(cfg.user) - 1);
                user_set = true;
            }
            continue;
        }
        if (strcmp(arg, "-p") == 0 || strcmp(arg, "--pass") == 0 || strcmp(arg, "--password") == 0) {
            if (i + 1 < argc) {
                strncpy(cfg.password, argv[++i], sizeof(cfg.password) - 1);
                pass_set = true;
            }
            continue;
        }
        if (strcmp(arg, "-a") == 0 || strcmp(arg, "--auth") == 0) {
            if (i + 1 < argc) {
                cfg.auth_type = atoi(argv[++i]);
                auth_set = true;
            }
            continue;
        }

        // Positional argument: part of SQL query
        if (query_len > 0 && query_len + 1 < sizeof(query_buf)) {
            query_buf[query_len++] = ' ';
        }
        size_t arg_len = strlen(arg);
        if (query_len + arg_len < sizeof(query_buf)) {
            strcpy(query_buf + query_len, arg);
            query_len += arg_len;
        }
    }

    // If query was not passed on command line, check stdin
    if (query_len == 0 && !grant_system_access) {
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        DWORD fileType = GetFileType(hStdin);
        if (fileType == FILE_TYPE_PIPE || fileType == FILE_TYPE_DISK) {
            DWORD bytesRead = 0;
            if (ReadFile(hStdin, query_buf, sizeof(query_buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
                query_buf[bytesRead] = '\0';
                query_len = bytesRead;
            }
        }
    }

    if (query_len == 0 && !grant_system_access) {
        print_help(argv[0]);
        return 0;
    }

    // 1. Auto-discovery of active DB configuration if server/db was not explicitly specified
    if (!server_set || !db_set) {
        DBConfig disc_cfg;
        if (db_discovery_find_terminal_config(&disc_cfg, verbose)) {
            if (!server_set) strncpy(cfg.server, disc_cfg.server, sizeof(cfg.server) - 1);
            if (!db_set) strncpy(cfg.database, disc_cfg.database, sizeof(cfg.database) - 1);
            if (!user_set && strlen(disc_cfg.user) > 0) strncpy(cfg.user, disc_cfg.user, sizeof(cfg.user) - 1);
            if (!pass_set && strlen(disc_cfg.password) > 0) strncpy(cfg.password, disc_cfg.password, sizeof(cfg.password) - 1);
            if (!auth_set) cfg.auth_type = disc_cfg.auth_type;
        }
    }

    // Special maintenance mode: grant system access
    if (grant_system_access) {
        SQLHENV hEnv = NULL;
        SQLHDBC hDbc = NULL;
        char conn_err[1024] = {0};
        if (!db_odbc_connect_adaptive(&cfg, &hEnv, &hDbc, conn_err, sizeof(conn_err), verbose)) {
            fprintf(stderr, "Error: Failed to connect to MS SQL Server for self-provisioning:\n%s\n", conn_err);
            return 3;
        }

        char grant_err[1024] = {0};
        bool grant_ok = l4sql_grant_system_access(hDbc, cfg.database, grant_err, sizeof(grant_err), verbose);
        db_odbc_disconnect(hEnv, hDbc);

        if (grant_ok) {
            printf("[OK] Successfully granted NT AUTHORITY\\SYSTEM db_datareader rights to database '%s'.\n", cfg.database);
            return 0;
        } else {
            fprintf(stderr, "Error: Failed to grant permissions: %s\n", grant_err);
            return 1;
        }
    }

    // 2. Expand shortcuts (e.g. "tb_Variables" or "select tb_Variables")
    char expanded_sql[8192];
    sql_validator_expand_shortcuts(query_buf, expanded_sql, sizeof(expanded_sql));

    if (verbose) {
        printf("[query] Original: %s\n", query_buf);
        printf("[query] Expanded: %s\n", expanded_sql);
    }

    // 3. Security validation: Read-Only mode + 'l4_' SP prefix check
    char val_err[512];
    SqlValidationResult val_res = sql_validator_validate(expanded_sql, val_err, sizeof(val_err));
    if (val_res != SQL_VALID_OK) {
        QueryResult err_res;
        query_result_init(&err_res, limit);
        strncpy(err_res.error_message, val_err, sizeof(err_res.error_message) - 1);
        output_formatter_print(&err_res, format, no_headers, verbose);
        query_result_free(&err_res);
        return 2; // Security rejection exit code
    }

    // 4. Connect to MS SQL Server using adaptive connection pipeline
    SQLHENV hEnv = NULL;
    SQLHDBC hDbc = NULL;
    char conn_err[1024];
    if (!db_odbc_connect_adaptive(&cfg, &hEnv, &hDbc, conn_err, sizeof(conn_err), verbose)) {
        QueryResult err_res;
        query_result_init(&err_res, limit);
        strncpy(err_res.error_message, conn_err, sizeof(err_res.error_message) - 1);
        output_formatter_print(&err_res, format, no_headers, verbose);
        query_result_free(&err_res);
        return 3; // Connection error exit code
    }

    // 5. Execute Query
    QueryResult res;
    query_result_init(&res, limit);
    bool exec_ok = db_odbc_execute(hDbc, expanded_sql, limit, &res, verbose);

    output_formatter_print(&res, format, no_headers, verbose);

    int exit_code = (exec_ok && res.error_message[0] == '\0') ? 0 : 1;

    query_result_free(&res);
    db_odbc_disconnect(hEnv, hDbc);

    return exit_code;
}
