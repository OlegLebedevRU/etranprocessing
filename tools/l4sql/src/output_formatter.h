#ifndef OUTPUT_FORMATTER_H
#define OUTPUT_FORMATTER_H

#include <stdbool.h>
#include <stddef.h>

#define DEFAULT_ROW_LIMIT 100
#define MAX_ALLOWED_ROW_LIMIT 1000

typedef enum {
    OUTPUT_FORMAT_TABLE = 0,
    OUTPUT_FORMAT_CSV = 1,
    OUTPUT_FORMAT_JSON = 2
} OutputFormat;

typedef struct {
    char name[128];
    int sql_type;
    size_t max_content_len;
} ColumnInfo;

typedef struct {
    char** values; // values[col_idx]
    bool* is_null; // is_null[col_idx]
} RowData;

typedef struct {
    ColumnInfo* columns;
    int col_count;
    RowData* rows;
    int row_count;
    int row_capacity;
    int limit;
    bool truncated_by_limit;
    int affected_rows; // for DML/SP executions
    char error_message[1024];
} QueryResult;

void query_result_init(QueryResult* res, int limit);
void query_result_free(QueryResult* res);

// Allocate columns
bool query_result_set_columns(QueryResult* res, int col_count);

// Add a row to results
bool query_result_add_row(QueryResult* res, char** values, const bool* is_null);

// Print results in the selected format
void output_formatter_print(const QueryResult* res, OutputFormat format, bool no_headers, bool verbose);

#endif // OUTPUT_FORMATTER_H
