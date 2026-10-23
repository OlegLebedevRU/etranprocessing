/* Bounded, fail-closed evaluator for the keywords used by frozen HEVC 2.0.0.
 * Jansson performs strict UTF-8 parsing and rejects duplicate keys. No v1 parser
 * is changed. The build checks the vendored schema against the TASK-00 digest. */
#ifndef MEDIA_SCHEMA_H
#define MEDIA_SCHEMA_H
#include <jansson.h>
#include <ctype.h>

#define MEDIA_V2_BODY_LIMIT 32768
#define MEDIA_DESCRIPTOR_LIMIT 4096
static json_t* g_media_schema = NULL;
static json_t* g_media_camera_schema = NULL;

static inline bool ms_equal(const json_t* a, const json_t* b) {
    if (json_is_number(a) && json_is_number(b)) return json_number_value(a) == json_number_value(b);
    if (json_typeof(a) != json_typeof(b)) return false;
    if (json_is_array(a)) {
        if (json_array_size(a) != json_array_size(b)) return false;
        for (size_t i = 0; i < json_array_size(a); i++)
            if (!ms_equal(json_array_get(a, i), json_array_get(b, i))) return false;
        return true;
    }
    if (json_is_object(a)) {
        if (json_object_size(a) != json_object_size(b)) return false;
        const char* key; json_t* value;
        json_object_foreach((json_t*)a, key, value) {
            json_t* other = json_object_get(b, key);
            if (!other || !ms_equal(value, other)) return false;
        }
        return true;
    }
    return json_equal(a, b);
}

static inline bool ms_uuid(const char* value) {
    if (!value || strlen(value) != 36) return false;
    for (size_t i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (value[i] != '-') return false; }
        else if (!isxdigit((unsigned char)value[i])) return false;
    }
    return true;
}

static inline bool ms_pattern(const char* pattern, const char* value) {
    if (!strcmp(pattern, "^/")) return value[0] == '/';
    if (!strcmp(pattern, "^[A-Za-z0-9._-]+$")) {
        for (const unsigned char* p = (const unsigned char*)value; *p; p++)
            if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                  (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-')) return false;
        return value[0] != 0;
    }
    if (!strcmp(pattern, "^42[0-9a-f]{4}$") || !strcmp(pattern, "^4d[0-9a-f]{4}$") ||
        !strcmp(pattern, "^64[0-9a-f]{4}$") || !strcmp(pattern, "^(42|4d|64)[0-9a-f]{4}$")) {
        if (strlen(value) != 6 || strspn(value + 2, "0123456789abcdef") != 4) return false;
        if (!strcmp(pattern, "^(42|4d|64)[0-9a-f]{4}$"))
            return !strncmp(value, "42", 2) || !strncmp(value, "4d", 2) || !strncmp(value, "64", 2);
        return !strncmp(value, pattern + 1, 2);
    }
    return false; /* New schema keywords/patterns need explicit implementation. */
}

static bool ms_validate_root(const json_t* root, const json_t* schema, const json_t* value, int depth) {
    if (!schema || !value || depth > 48) return false;
    if (json_is_boolean(schema)) return json_is_true(schema);
    const char* reference = json_string_value(json_object_get(schema, "$ref"));
    if (reference) {
        if (strncmp(reference, "#/$defs/", 8)) return false;
        if (!ms_validate_root(root, json_object_get(json_object_get(root, "$defs"), reference + 8), value, depth + 1)) return false;
    }
    json_t* choice = json_object_get(schema, "const");
    if (choice && !ms_equal(choice, value)) return false;
    json_t* list = json_object_get(schema, "enum");
    if (list) {
        bool found = false;
        for (size_t i = 0; i < json_array_size(list); i++) found |= ms_equal(json_array_get(list, i), value);
        if (!found) return false;
    }
    const char* type = json_string_value(json_object_get(schema, "type"));
    if (type) {
        bool valid = (!strcmp(type, "object") && json_is_object(value)) ||
            (!strcmp(type, "array") && json_is_array(value)) ||
            (!strcmp(type, "string") && json_is_string(value)) ||
            (!strcmp(type, "boolean") && json_is_boolean(value)) ||
            (!strcmp(type, "null") && json_is_null(value)) ||
            (!strcmp(type, "integer") && json_is_number(value) &&
              json_number_value(value) >= -9007199254740991.0 && json_number_value(value) <= 9007199254740991.0 &&
              json_number_value(value) == (double)(int64_t)json_number_value(value));
        if (!valid) return false;
    }
    if (json_is_number(value)) {
        double n = json_number_value(value);
        choice = json_object_get(schema, "minimum"); if (choice && n < json_number_value(choice)) return false;
        choice = json_object_get(schema, "maximum"); if (choice && n > json_number_value(choice)) return false;
        choice = json_object_get(schema, "multipleOf");
        if (choice && (int64_t)n % json_integer_value(choice)) return false;
    }
    if (json_is_string(value)) {
        const char* text = json_string_value(value); size_t length = 0;
        for (const unsigned char* p = (const unsigned char*)text; *p; p++) if ((*p & 0xc0) != 0x80) length++;
        choice = json_object_get(schema, "minLength"); if (choice && length < (size_t)json_integer_value(choice)) return false;
        choice = json_object_get(schema, "maxLength"); if (choice && length > (size_t)json_integer_value(choice)) return false;
        const char* pattern = json_string_value(json_object_get(schema, "pattern"));
        if (pattern && !ms_pattern(pattern, text)) return false;
        const char* format = json_string_value(json_object_get(schema, "format"));
        if (format && !strcmp(format, "uuid") && !ms_uuid(text)) return false;
        /* date-time occurs only in agent events, never in service requests. */
        if (format && !strcmp(format, "date-time") && (strlen(text) < 20 || text[10] != 'T')) return false;
    }
    if (json_is_object(value)) {
        json_t* properties = json_object_get(schema, "properties");
        list = json_object_get(schema, "required");
        for (size_t i = 0; i < json_array_size(list); i++)
            if (!json_object_get(value, json_string_value(json_array_get(list, i)))) return false;
        const char* key; json_t* item;
        json_object_foreach((json_t*)value, key, item) {
            json_t* subschema = json_object_get(properties, key);
            if (subschema && !ms_validate_root(root, subschema, item, depth + 1)) return false;
            if (!subschema && json_is_false(json_object_get(schema, "additionalProperties"))) return false;
        }
        json_t* dependencies = json_object_get(schema, "dependentRequired");
        json_t* required;
        json_object_foreach(dependencies, key, required) {
            if (!json_object_get(value, key)) continue;
            for (size_t i = 0; i < json_array_size(required); i++)
                if (!json_object_get(value, json_string_value(json_array_get(required, i)))) return false;
        }
    }
    if (json_is_array(value)) {
        size_t size = json_array_size(value);
        choice = json_object_get(schema, "minItems"); if (choice && size < (size_t)json_integer_value(choice)) return false;
        choice = json_object_get(schema, "maxItems"); if (choice && size > (size_t)json_integer_value(choice)) return false;
        json_t* items = json_object_get(schema, "items");
        json_t* contains = json_object_get(schema, "contains"); bool found = false;
        for (size_t i = 0; i < size; i++) {
            json_t* item = json_array_get(value, i);
            if (items && !ms_validate_root(root, items, item, depth + 1)) return false;
            if (contains && ms_validate_root(root, contains, item, depth + 1)) found = true;
            if (json_is_true(json_object_get(schema, "uniqueItems")))
                for (size_t j = 0; j < i; j++) if (ms_equal(item, json_array_get(value, j))) return false;
        }
        if (contains && !found) return false;
    }
    const char* combinations[] = {"allOf", "anyOf", "oneOf"};
    for (int k = 0; k < 3; k++) {
        list = json_object_get(schema, combinations[k]);
        if (!list) continue;
        size_t matches = 0;
        for (size_t i = 0; i < json_array_size(list); i++)
            matches += ms_validate_root(root, json_array_get(list, i), value, depth + 1) ? 1 : 0;
        if ((k == 0 && matches != json_array_size(list)) || (k == 1 && matches == 0) || (k == 2 && matches != 1)) return false;
    }
    choice = json_object_get(schema, "if");
    if (choice) {
        choice = json_object_get(schema, ms_validate_root(root, choice, value, depth + 1) ? "then" : "else");
        if (choice && !ms_validate_root(root, choice, value, depth + 1)) return false;
    }
    choice = json_object_get(schema, "not");
    if (choice && ms_validate_root(root, choice, value, depth + 1)) return false;
    return true;
}

static inline bool ms_validate(const json_t* schema, const json_t* value, int depth) {
    return ms_validate_root(g_media_schema, schema, value, depth);
}

static inline bool media_camera_schema_load(const char* path) {
    json_error_t error;
    json_t* schema = json_load_file(path, JSON_REJECT_DUPLICATES, &error);
    if (!schema) return false;
    json_decref(g_media_camera_schema); g_media_camera_schema = schema;
    return true;
}

static inline bool media_schema_load(const char* path) {
    json_error_t error;
    json_t* schema = json_load_file(path, JSON_REJECT_DUPLICATES, &error);
    if (!schema) return false;
    json_decref(g_media_schema); g_media_schema = schema;
    return true;
}

static inline json_t* media_parse_schema(json_t* root, const char* body, const char* definition) {
    if (!body || strlen(body) > MEDIA_V2_BODY_LIMIT || !root) return NULL;
    json_error_t error;
    json_t* value = json_loads(body, JSON_REJECT_DUPLICATES, &error);
    json_t* schema = json_object_get(json_object_get(root, "$defs"), definition);
    if (value && ms_validate_root(root, schema, value, 0)) return value;
    json_decref(value); return NULL;
}

static inline json_t* media_v2_parse(const char* body, const char* definition) {
    return media_parse_schema(g_media_schema, body, definition);
}
#endif
