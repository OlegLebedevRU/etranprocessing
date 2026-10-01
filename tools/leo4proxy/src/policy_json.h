#ifndef LEO4_POLICY_JSON_H
#define LEO4_POLICY_JSON_H
#include <stdbool.h>
#include <stddef.h>
/* Bounded JSON reader: validates the whole document, rejects duplicate keys. */
typedef struct { int start, end, next; char type; } PolicyJsonToken;
typedef struct { const char* text; PolicyJsonToken tokens[512]; int count; } PolicyJson;
bool policy_json_parse(PolicyJson* json, const char* text, size_t len);
int policy_json_field(const PolicyJson* json, int object, const char* key);
bool policy_json_string(const PolicyJson* json, int token, char* out, size_t size);
bool policy_json_bool(const PolicyJson* json, int token, bool* out);
bool policy_json_uint(const PolicyJson* json, int token, unsigned long long* out);
#endif
