#ifndef SQL_VALIDATOR_H
#define SQL_VALIDATOR_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SQL_VALID_OK = 0,
    SQL_ERR_EMPTY = 1,
    SQL_ERR_FORBIDDEN_KEYWORD = 2,
    SQL_ERR_SP_PREFIX_REQUIRED = 3,
    SQL_ERR_INVALID_SYNTAX = 4
} SqlValidationResult;

// Expand shortcuts like "tb_Variables" or "select tb_Variables" to full "SELECT * FROM tb_Variables"
void sql_validator_expand_shortcuts(const char* input_sql, char* out_sql, size_t out_max);

// Validate that the query is read-only or an authorized l4_ SP call
SqlValidationResult sql_validator_validate(const char* sql, char* err_msg, size_t err_msg_max);

// Helper to get human-readable error description
const char* sql_validator_error_str(SqlValidationResult res);

#endif // SQL_VALIDATOR_H
