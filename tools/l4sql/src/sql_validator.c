#include "sql_validator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char* FORBIDDEN_KEYWORDS[] = {
    "INSERT",
    "UPDATE",
    "DELETE",
    "DROP",
    "ALTER",
    "CREATE",
    "TRUNCATE",
    "MERGE",
    "GRANT",
    "REVOKE",
    "DENY",
    "BULK",
    "OPENROWSET",
    "OPENDATASOURCE",
    "XP_CMDSHELL",
    "SP_EXECUTESQL",
    "WRITETEXT",
    "UPDATETEXT",
    NULL
};

static void trim(char* str) {
    if (!str) return;
    char* p = str;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != str) memmove(str, p, strlen(p) + 1);
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        len--;
    }
}

static bool is_word_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '$' || c == '@' || c == '#';
}

void sql_validator_expand_shortcuts(const char* input_sql, char* out_sql, size_t out_max) {
    if (!input_sql || !out_sql || out_max == 0) return;
    out_sql[0] = '\0';

    char trimmed[4096];
    strncpy(trimmed, input_sql, sizeof(trimmed) - 1);
    trimmed[sizeof(trimmed) - 1] = '\0';
    trim(trimmed);

    if (strlen(trimmed) == 0) return;

    // 1. Direct table name shortcut: "tb_Variables" or "dbo.tb_Variables" or "Terminal.dbo.tb_Variables"
    if (_strnicmp(trimmed, "tb_", 3) == 0 ||
        _strnicmp(trimmed, "[tb_", 4) == 0 ||
        strstr(trimmed, ".tb_") != NULL ||
        strstr(trimmed, ".[tb_") != NULL) {

        // Make sure it doesn't already start with SELECT / EXEC
        if (_strnicmp(trimmed, "select", 6) != 0 &&
            _strnicmp(trimmed, "exec", 4) != 0 &&
            _strnicmp(trimmed, "with", 4) != 0) {
            snprintf(out_sql, out_max, "SELECT * FROM %s", trimmed);
            return;
        }
    }

    // 2. Shorthand "select <table_name>" without FROM / WHERE / JOIN / TOP / DISTINCT / commas
    if (_strnicmp(trimmed, "select ", 7) == 0) {
        const char* rest = trimmed + 7;
        while (*rest && isspace((unsigned char)*rest)) rest++;

        // Check if "from" is in the query (case-insensitive word boundary)
        bool has_from = false;
        bool has_special = false;
        const char* p = rest;
        while (*p) {
            if (*p == ',' || *p == '(' || *p == ')' || *p == '=' || *p == '<' || *p == '>') {
                has_special = true;
                break;
            }
            if ((p == rest || !is_word_char(*(p - 1))) &&
                _strnicmp(p, "from", 4) == 0 &&
                !is_word_char(*(p + 4))) {
                has_from = true;
                break;
            }
            if ((p == rest || !is_word_char(*(p - 1))) &&
                (_strnicmp(p, "top", 3) == 0 || _strnicmp(p, "distinct", 8) == 0 || _strnicmp(p, "where", 5) == 0)) {
                has_special = true;
                break;
            }
            p++;
        }

        if (!has_from && !has_special && strlen(rest) > 0) {
            // It is just "select <table_name>"
            snprintf(out_sql, out_max, "SELECT * FROM %s", rest);
            return;
        }
    }

    // Default: copy as is
    strncpy(out_sql, trimmed, out_max - 1);
    out_sql[out_max - 1] = '\0';
}

static const char* skip_whitespace_and_comments(const char* p) {
    while (*p) {
        if (isspace((unsigned char)*p)) {
            p++;
            continue;
        }
        // Single line comment: --
        if (p[0] == '-' && p[1] == '-') {
            p += 2;
            while (*p && *p != '\n' && *p != '\r') p++;
            continue;
        }
        // Multi-line comment: /* ... */
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (p[0] == '*' && p[1] == '/') p += 2;
            continue;
        }
        break;
    }
    return p;
}

static const char* get_next_token(const char* p, char* token, size_t token_max) {
    p = skip_whitespace_and_comments(p);
    if (!*p) {
        token[0] = '\0';
        return p;
    }

    // String literal '...'
    if (*p == '\'') {
        size_t idx = 0;
        if (idx + 1 < token_max) token[idx++] = *p;
        p++;
        while (*p) {
            if (*p == '\'') {
                if (idx + 1 < token_max) token[idx++] = *p;
                p++;
                if (*p == '\'') { // Escaped quote
                    if (idx + 1 < token_max) token[idx++] = *p;
                    p++;
                } else {
                    break;
                }
            } else {
                if (idx + 1 < token_max) token[idx++] = *p;
                p++;
            }
        }
        token[idx] = '\0';
        return p;
    }

    // Bracketed identifier [...]
    if (*p == '[') {
        size_t idx = 0;
        while (*p && *p != ']') {
            if (idx + 1 < token_max) token[idx++] = *p;
            p++;
        }
        if (*p == ']') {
            if (idx + 1 < token_max) token[idx++] = *p;
            p++;
        }
        token[idx] = '\0';
        return p;
    }

    // Regular word token (alphanumeric, _, $, @, #)
    if (is_word_char(*p)) {
        size_t idx = 0;
        while (*p && is_word_char(*p)) {
            if (idx + 1 < token_max) token[idx++] = *p;
            p++;
        }
        token[idx] = '\0';
        return p;
    }

    // Single character operator or punctuation (*, ;, ,, (, ), =, <, >, ., etc.)
    token[0] = *p;
    token[1] = '\0';
    return p + 1;
}

static void extract_clean_proc_name(const char* raw_proc, char* clean_proc, size_t clean_max) {
    clean_proc[0] = '\0';
    if (!raw_proc || clean_max == 0) return;

    // Handle possible prefixes: [dbo].[l4_test] or dbo.l4_test or Terminal.dbo.l4_test
    const char* last_dot = strrchr(raw_proc, '.');
    const char* p = last_dot ? (last_dot + 1) : raw_proc;

    size_t idx = 0;
    while (*p && idx + 1 < clean_max) {
        if (*p != '[' && *p != ']' && *p != '"' && !isspace((unsigned char)*p)) {
            clean_proc[idx++] = *p;
        }
        p++;
    }
    clean_proc[idx] = '\0';
}

SqlValidationResult sql_validator_validate(const char* sql, char* err_msg, size_t err_msg_max) {
    if (err_msg && err_msg_max > 0) err_msg[0] = '\0';

    if (!sql || strlen(sql) == 0) {
        if (err_msg) snprintf(err_msg, err_msg_max, "Empty SQL query");
        return SQL_ERR_EMPTY;
    }

    const char* p = sql;
    char token[256];

    bool statement_started = false;
    bool is_exec = false;
    bool checked_proc_name = false;

    while (*p) {
        p = get_next_token(p, token, sizeof(token));
        if (token[0] == '\0') break;

        if (token[0] == ';') {
            // End of current statement in batch
            statement_started = false;
            is_exec = false;
            checked_proc_name = false;
            continue;
        }

        if (!statement_started) {
            statement_started = true;

            // Check statement initial keyword
            if (_stricmp(token, "SELECT") == 0 ||
                _stricmp(token, "WITH") == 0 ||
                _stricmp(token, "SHOW") == 0 ||
                _stricmp(token, "DESCRIBE") == 0 ||
                _stricmp(token, "EXPLAIN") == 0) {
                is_exec = false;
            } else if (_stricmp(token, "EXEC") == 0 || _stricmp(token, "EXECUTE") == 0) {
                is_exec = true;
                checked_proc_name = false;
            } else {
                if (err_msg) {
                    snprintf(err_msg, err_msg_max,
                             "Blocked command '%s': only SELECT / read-only queries and 'EXEC l4_*' procedures are permitted",
                             token);
                }
                return SQL_ERR_FORBIDDEN_KEYWORD;
            }
            continue;
        }

        if (is_exec && !checked_proc_name) {
            // Next token(s) after EXEC/EXECUTE is the procedure name (which may be schema-qualified)
            char full_proc[512];
            full_proc[0] = '\0';
            strncat(full_proc, token, sizeof(full_proc) - 1);

            while (*p) {
                const char* next_p = p;
                char next_token[256];
                next_p = get_next_token(next_p, next_token, sizeof(next_token));
                if (next_token[0] == '.') {
                    p = next_p;
                    strncat(full_proc, ".", sizeof(full_proc) - strlen(full_proc) - 1);
                    next_p = get_next_token(p, next_token, sizeof(next_token));
                    if (next_token[0] != '\0') {
                        p = next_p;
                        strncat(full_proc, next_token, sizeof(full_proc) - strlen(full_proc) - 1);
                    }
                } else {
                    break;
                }
            }

            char clean_name[128];
            extract_clean_proc_name(full_proc, clean_name, sizeof(clean_name));

            if (_strnicmp(clean_name, "l4_", 3) != 0) {
                if (err_msg) {
                    snprintf(err_msg, err_msg_max,
                             "Blocked procedure '%s': only stored procedures with 'l4_' prefix are permitted (e.g. 'EXEC l4_...')",
                             clean_name);
                }
                return SQL_ERR_SP_PREFIX_REQUIRED;
            }
            checked_proc_name = true;
            continue;
        }

        // Check for forbidden keywords in non-exec queries
        if (!is_exec) {
            // Check if token matches forbidden keywords list
            for (int i = 0; FORBIDDEN_KEYWORDS[i] != NULL; i++) {
                if (_stricmp(token, FORBIDDEN_KEYWORDS[i]) == 0) {
                    if (err_msg) {
                        snprintf(err_msg, err_msg_max,
                                 "Blocked keyword '%s': modification/DDL operations are prohibited in read-only mode",
                                 token);
                    }
                    return SQL_ERR_FORBIDDEN_KEYWORD;
                }
            }

            // Check for "SELECT ... INTO ..."
            if (_stricmp(token, "INTO") == 0) {
                if (err_msg) {
                    snprintf(err_msg, err_msg_max, "Blocked 'INTO' keyword: creating tables via SELECT INTO is prohibited");
                }
                return SQL_ERR_FORBIDDEN_KEYWORD;
            }
        }
    }

    return SQL_VALID_OK;
}

const char* sql_validator_error_str(SqlValidationResult res) {
    switch (res) {
        case SQL_VALID_OK: return "OK";
        case SQL_ERR_EMPTY: return "Query is empty";
        case SQL_ERR_FORBIDDEN_KEYWORD: return "Forbidden keyword or statement";
        case SQL_ERR_SP_PREFIX_REQUIRED: return "Stored procedure must start with 'l4_'";
        case SQL_ERR_INVALID_SYNTAX: return "Invalid SQL syntax";
        default: return "Validation error";
    }
}
