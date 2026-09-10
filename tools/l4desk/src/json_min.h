#ifndef L4DESK_JSON_MIN_H
#define L4DESK_JSON_MIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool json_extract_str(const char* json, const char* key, char* out, size_t max_len);
bool json_extract_int(const char* json, const char* key, int* out);
bool json_extract_int64(const char* json, const char* key, int64_t* out);
bool json_extract_double(const char* json, const char* key, double* out);
bool json_extract_bool(const char* json, const char* key, bool* out);

#endif /* L4DESK_JSON_MIN_H */
