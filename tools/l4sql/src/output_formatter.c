#include "output_formatter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void query_result_init(QueryResult* res, int limit) {
    if (!res) return;
    memset(res, 0, sizeof(QueryResult));
    if (limit <= 0) limit = DEFAULT_ROW_LIMIT;
    if (limit > MAX_ALLOWED_ROW_LIMIT) limit = MAX_ALLOWED_ROW_LIMIT;
    res->limit = limit;
    res->affected_rows = -1;
}

void query_result_free(QueryResult* res) {
    if (!res) return;
    if (res->columns) {
        free(res->columns);
        res->columns = NULL;
    }
    if (res->rows) {
        for (int i = 0; i < res->row_count; i++) {
            if (res->rows[i].values) {
                for (int j = 0; j < res->col_count; j++) {
                    if (res->rows[i].values[j]) {
                        free(res->rows[i].values[j]);
                    }
                }
                free(res->rows[i].values);
            }
            if (res->rows[i].is_null) {
                free(res->rows[i].is_null);
            }
        }
        free(res->rows);
        res->rows = NULL;
    }
    res->row_count = 0;
    res->row_capacity = 0;
    res->col_count = 0;
}

bool query_result_set_columns(QueryResult* res, int col_count) {
    if (!res || col_count <= 0) return false;
    res->columns = (ColumnInfo*)calloc((size_t)col_count, sizeof(ColumnInfo));
    if (!res->columns) return false;
    res->col_count = col_count;
    return true;
}

static size_t utf8_char_count(const char* s) {
    if (!s) return 0;
    size_t count = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        if ((*p & 0xC0) != 0x80) {
            count++;
        }
    }
    return count;
}

static void utf8_safe_copy(char* dest, size_t dest_size, const char* src, size_t max_chars) {
    if (!dest || dest_size == 0) return;
    dest[0] = '\0';
    if (!src) return;

    size_t chars = 0;
    size_t bytes = 0;
    const unsigned char* p = (const unsigned char*)src;

    while (*p && chars < max_chars && bytes + 1 < dest_size) {
        size_t cp_len = 1;
        if ((*p & 0xE0) == 0xC0) cp_len = 2;
        else if ((*p & 0xF0) == 0xE0) cp_len = 3;
        else if ((*p & 0xF8) == 0xF0) cp_len = 4;

        if (bytes + cp_len >= dest_size) break;
        for (size_t k = 0; k < cp_len && p[k]; k++) {
            dest[bytes++] = (char)p[k];
        }
        p += cp_len;
        chars++;
    }
    dest[bytes] = '\0';
}

bool query_result_add_row(QueryResult* res, char** values, const bool* is_null) {
    if (!res || !values || !is_null) return false;

    if (res->row_count >= res->limit) {
        res->truncated_by_limit = true;
        return false;
    }

    if (res->row_count >= res->row_capacity) {
        int new_cap = (res->row_capacity == 0) ? 16 : (res->row_capacity * 2);
        if (new_cap > res->limit) new_cap = res->limit;
        RowData* new_rows = (RowData*)realloc(res->rows, (size_t)new_cap * sizeof(RowData));
        if (!new_rows) return false;
        res->rows = new_rows;
        res->row_capacity = new_cap;
    }

    RowData* r = &res->rows[res->row_count];
    r->values = (char**)calloc((size_t)res->col_count, sizeof(char*));
    r->is_null = (bool*)calloc((size_t)res->col_count, sizeof(bool));
    if (!r->values || !r->is_null) return false;

    for (int j = 0; j < res->col_count; j++) {
        r->is_null[j] = is_null[j];
        if (!is_null[j] && values[j]) {
            size_t len = utf8_char_count(values[j]);
            r->values[j] = _strdup(values[j]);
            if (len > res->columns[j].max_content_len) {
                res->columns[j].max_content_len = len;
            }
        } else {
            r->values[j] = NULL;
            if (6 > res->columns[j].max_content_len) { // "<NULL>" length is 6
                res->columns[j].max_content_len = 6;
            }
        }
    }

    res->row_count++;
    return true;
}

static void print_json_escaped(const char* str) {
    if (!str) {
        printf("null");
        return;
    }
    putchar('"');
    for (const char* p = str; *p; p++) {
        switch (*p) {
            case '"':  printf("\\\""); break;
            case '\\': printf("\\\\"); break;
            case '\b': printf("\\b");  break;
            case '\f': printf("\\f");  break;
            case '\n': printf("\\n");  break;
            case '\r': printf("\\r");  break;
            case '\t': printf("\\t");  break;
            default:
                if ((unsigned char)*p < 0x20) {
                    printf("\\u%04x", (unsigned char)*p);
                } else {
                    putchar(*p);
                }
                break;
        }
    }
    putchar('"');
}

static void print_csv_field(const char* str, bool is_null) {
    if (is_null || !str) {
        return;
    }
    bool need_quotes = false;
    for (const char* p = str; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
            need_quotes = true;
            break;
        }
    }

    if (!need_quotes) {
        printf("%s", str);
    } else {
        putchar('"');
        for (const char* p = str; *p; p++) {
            if (*p == '"') {
                printf("\"\"");
            } else {
                putchar(*p);
            }
        }
        putchar('"');
    }
}

void output_formatter_print(const QueryResult* res, OutputFormat format, bool no_headers, bool verbose) {
    if (!res) return;

    if (res->error_message[0] != '\0') {
        if (format == OUTPUT_FORMAT_JSON) {
            printf("{\"status\":\"error\",\"message\":");
            print_json_escaped(res->error_message);
            printf("}\n");
        } else {
            fprintf(stderr, "Error: %s\n", res->error_message);
        }
        return;
    }

    if (res->col_count == 0) {
        if (format == OUTPUT_FORMAT_JSON) {
            printf("{\"status\":\"ok\",\"rows_affected\":%d}\n", res->affected_rows >= 0 ? res->affected_rows : 0);
        } else {
            if (res->affected_rows >= 0) {
                printf("Command executed successfully. Rows affected: %d\n", res->affected_rows);
            } else {
                printf("Command executed successfully.\n");
            }
        }
        return;
    }

    if (format == OUTPUT_FORMAT_JSON) {
        printf("[\n");
        for (int i = 0; i < res->row_count; i++) {
            printf("  {");
            for (int j = 0; j < res->col_count; j++) {
                print_json_escaped(res->columns[j].name);
                printf(":");
                if (res->rows[i].is_null[j] || !res->rows[i].values[j]) {
                    printf("null");
                } else {
                    print_json_escaped(res->rows[i].values[j]);
                }
                if (j < res->col_count - 1) printf(", ");
            }
            printf("}%s\n", (i < res->row_count - 1) ? "," : "");
        }
        printf("]\n");
        return;
    }

    if (format == OUTPUT_FORMAT_CSV) {
        if (!no_headers) {
            for (int j = 0; j < res->col_count; j++) {
                print_csv_field(res->columns[j].name, false);
                if (j < res->col_count - 1) printf(",");
            }
            printf("\n");
        }
        for (int i = 0; i < res->row_count; i++) {
            for (int j = 0; j < res->col_count; j++) {
                print_csv_field(res->rows[i].values[j], res->rows[i].is_null[j]);
                if (j < res->col_count - 1) printf(",");
            }
            printf("\n");
        }
        return;
    }

    // Default: FORMAT_TABLE
    int* col_widths = (int*)malloc((size_t)res->col_count * sizeof(int));
    if (!col_widths) return;

    for (int j = 0; j < res->col_count; j++) {
        size_t name_len = utf8_char_count(res->columns[j].name);
        size_t max_len = (res->columns[j].max_content_len > name_len) ? res->columns[j].max_content_len : name_len;
        if (max_len < 3) max_len = 3;
        if (max_len > 60) max_len = 60; // Clamp column width to 60 for terminal readability
        col_widths[j] = (int)max_len;
    }

    if (!no_headers) {
        for (int j = 0; j < res->col_count; j++) {
            size_t name_chars = utf8_char_count(res->columns[j].name);
            if (name_chars > (size_t)col_widths[j]) {
                char truncated[256];
                utf8_safe_copy(truncated, sizeof(truncated), res->columns[j].name, (size_t)col_widths[j]);
                printf("%s", truncated);
            } else {
                printf("%s", res->columns[j].name);
                for (size_t k = name_chars; k < (size_t)col_widths[j]; k++) putchar(' ');
            }
            if (j < res->col_count - 1) printf(" | ");
        }
        printf("\n");

        for (int j = 0; j < res->col_count; j++) {
            for (int k = 0; k < col_widths[j]; k++) putchar('-');
            if (j < res->col_count - 1) printf("-+-");
        }
        printf("\n");
    }

    for (int i = 0; i < res->row_count; i++) {
        for (int j = 0; j < res->col_count; j++) {
            if (res->rows[i].is_null[j] || !res->rows[i].values[j]) {
                printf("%-*s", col_widths[j], "<NULL>");
            } else {
                const char* val = res->rows[i].values[j];
                size_t val_chars = utf8_char_count(val);
                if (val_chars > (size_t)col_widths[j]) {
                    char truncated[512];
                    utf8_safe_copy(truncated, sizeof(truncated), val, (size_t)col_widths[j]);
                    printf("%s", truncated);
                } else {
                    printf("%s", val);
                    for (size_t k = val_chars; k < (size_t)col_widths[j]; k++) putchar(' ');
                }
            }
            if (j < res->col_count - 1) printf(" | ");
        }
        printf("\n");
    }

    free(col_widths);

    if (verbose || !no_headers) {
        if (res->truncated_by_limit) {
            printf("\n(%d rows shown, limit %d reached)\n", res->row_count, res->limit);
        } else {
            printf("\n(%d rows)\n", res->row_count);
        }
    }
}
