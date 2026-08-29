#include "xml_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void db_config_init(DBConfig* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(DBConfig));
    strncpy(cfg->server, "(local)\\sqlexpress", sizeof(cfg->server) - 1);
    strncpy(cfg->database, "Terminal", sizeof(cfg->database) - 1);
    cfg->auth_type = 0; // Windows Auth
}

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

static void decode_xml_entities(char* str) {
    if (!str) return;
    char* src = str;
    char* dst = str;
    while (*src) {
        if (strncmp(src, "&amp;", 5) == 0) {
            *dst++ = '&';
            src += 5;
        } else if (strncmp(src, "&lt;", 4) == 0) {
            *dst++ = '<';
            src += 4;
        } else if (strncmp(src, "&gt;", 4) == 0) {
            *dst++ = '>';
            src += 4;
        } else if (strncmp(src, "&quot;", 6) == 0) {
            *dst++ = '"';
            src += 6;
        } else if (strncmp(src, "&apos;", 6) == 0) {
            *dst++ = '\'';
            src += 6;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static bool extract_tag_value(const char* xml, const char* tag, char* out_val, size_t out_max) {
    if (!xml || !tag || !out_val || out_max == 0) return false;
    out_val[0] = '\0';

    char open_tag[64];
    char close_tag[64];
    snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

    const char* p_start = strstr(xml, open_tag);
    if (!p_start) {
        // Try case-insensitive search or tag with attributes
        char open_tag_attr[64];
        snprintf(open_tag_attr, sizeof(open_tag_attr), "<%s ", tag);
        p_start = strstr(xml, open_tag_attr);
        if (p_start) {
            p_start = strchr(p_start, '>');
            if (p_start) p_start++;
        }
    } else {
        p_start += strlen(open_tag);
    }

    if (!p_start) return false;

    const char* p_end = strstr(p_start, close_tag);
    if (!p_end) return false;

    size_t len = (size_t)(p_end - p_start);
    if (len >= out_max) len = out_max - 1;
    strncpy(out_val, p_start, len);
    out_val[len] = '\0';

    decode_xml_entities(out_val);
    trim(out_val);
    return true;
}

bool db_config_parse_string(const char* xml_str, DBConfig* out_cfg) {
    if (!xml_str || !out_cfg) return false;
    db_config_init(out_cfg);

    char val[256];
    if (extract_tag_value(xml_str, "Server", val, sizeof(val)) && strlen(val) > 0) {
        strncpy(out_cfg->server, val, sizeof(out_cfg->server) - 1);
    }
    if (extract_tag_value(xml_str, "Database", val, sizeof(val)) && strlen(val) > 0) {
        strncpy(out_cfg->database, val, sizeof(out_cfg->database) - 1);
    }
    if (extract_tag_value(xml_str, "User", val, sizeof(val)) && strlen(val) > 0) {
        strncpy(out_cfg->user, val, sizeof(out_cfg->user) - 1);
    }
    if (extract_tag_value(xml_str, "Password", val, sizeof(val)) && strlen(val) > 0) {
        strncpy(out_cfg->password, val, sizeof(out_cfg->password) - 1);
    }
    if (extract_tag_value(xml_str, "Name", val, sizeof(val)) && strlen(val) > 0) {
        strncpy(out_cfg->name, val, sizeof(out_cfg->name) - 1);
    }
    if (extract_tag_value(xml_str, "AuthType", val, sizeof(val)) && strlen(val) > 0) {
        out_cfg->auth_type = atoi(val);
    } else {
        // If user and password are empty, default to Windows Auth (0)
        out_cfg->auth_type = (strlen(out_cfg->user) > 0) ? 1 : 0;
    }

    return true;
}

bool db_config_parse_file(const wchar_t* xml_file_path, DBConfig* out_cfg) {
    if (!xml_file_path || !out_cfg) return false;

    FILE* fp = _wfopen(xml_file_path, L"rb");
    if (!fp) return false;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz <= 0 || sz > 1024 * 1024) { // DBConfig is small (< 1MB)
        fclose(fp);
        return false;
    }

    char* buffer = (char*)malloc((size_t)sz + 1);
    if (!buffer) {
        fclose(fp);
        return false;
    }

    size_t read_bytes = fread(buffer, 1, (size_t)sz, fp);
    fclose(fp);
    buffer[read_bytes] = '\0';

    bool ok = db_config_parse_string(buffer, out_cfg);
    free(buffer);

    if (ok) {
        WideCharToMultiByte(CP_UTF8, 0, xml_file_path, -1, out_cfg->config_file_path, MAX_PATH, NULL, NULL);
    }
    return ok;
}
