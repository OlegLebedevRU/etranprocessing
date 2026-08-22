#include "xml_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char* stristr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    size_t needle_len = strlen(needle);
    if (needle_len == 0) return haystack;

    while (*haystack) {
        if (_strnicmp(haystack, needle, needle_len) == 0) {
            return haystack;
        }
        haystack++;
    }
    return NULL;
}

bool xml_get_tag_value(const char* xml, const char* tag_name, char* out_val, size_t out_val_size) {
    if (!xml || !tag_name || !out_val || out_val_size == 0) return false;
    out_val[0] = '\0';

    char open_tag[128];
    char close_tag[128];
    snprintf(open_tag, sizeof(open_tag), "<%s>", tag_name);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag_name);

    const char* start = stristr(xml, open_tag);
    if (!start) return false;
    start += strlen(open_tag);

    const char* end = stristr(start, close_tag);
    if (!end) return false;

    size_t len = (size_t)(end - start);
    if (len >= out_val_size) {
        len = out_val_size - 1;
    }

    memcpy(out_val, start, len);
    out_val[len] = '\0';
    return true;
}

bool xml_get_tag_value_alloc(const char* xml, const char* tag_name, char** out_val, size_t* out_len) {
    if (!xml || !tag_name || !out_val) return false;
    *out_val = NULL;
    if (out_len) *out_len = 0;

    char open_tag[128];
    char close_tag[128];
    snprintf(open_tag, sizeof(open_tag), "<%s>", tag_name);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag_name);

    const char* start = stristr(xml, open_tag);
    if (!start) return false;
    start += strlen(open_tag);

    const char* end = stristr(start, close_tag);
    if (!end) return false;

    size_t len = (size_t)(end - start);
    char* buf = (char*)malloc(len + 1);
    if (!buf) return false;

    memcpy(buf, start, len);
    buf[len] = '\0';

    *out_val = buf;
    if (out_len) *out_len = len;
    return true;
}

bool parse_check_response(const char* xml, CheckResponse* out_resp) {
    if (!xml || !out_resp) return false;
    memset(out_resp, 0, sizeof(CheckResponse));

    xml_get_tag_value(xml, "Result", out_resp->result, sizeof(out_resp->result));
    
    char code_str[32] = { 0 };
    if (xml_get_tag_value(xml, "code", code_str, sizeof(code_str))) {
        out_resp->code = atoi(code_str);
    }

    xml_get_tag_value(xml, "Description", out_resp->description, sizeof(out_resp->description));
    xml_get_tag_value(xml, "prov", out_resp->prov, sizeof(out_resp->prov));
    xml_get_tag_value(xml, "dn", out_resp->dn, sizeof(out_resp->dn));
    xml_get_tag_value(xml, "pin", out_resp->pin, sizeof(out_resp->pin));
    xml_get_tag_value(xml, "sign", out_resp->sign, sizeof(out_resp->sign));

    return (_stricmp(out_resp->result, "OK") == 0 && out_resp->code == 0);
}

bool parse_setup_response(const char* xml, SetupResponse* out_resp) {
    if (!xml || !out_resp) return false;
    memset(out_resp, 0, sizeof(SetupResponse));

    xml_get_tag_value(xml, "Result", out_resp->result, sizeof(out_resp->result));

    char code_str[32] = { 0 };
    if (xml_get_tag_value(xml, "code", code_str, sizeof(code_str))) {
        out_resp->code = atoi(code_str);
    }

    xml_get_tag_value(xml, "Description", out_resp->description, sizeof(out_resp->description));
    xml_get_tag_value_alloc(xml, "CERTDATA", &out_resp->certdata, &out_resp->certdata_len);

    return (_stricmp(out_resp->result, "OK") == 0 && out_resp->code == 0 && out_resp->certdata != NULL);
}

void free_setup_response(SetupResponse* resp) {
    if (resp && resp->certdata) {
        free(resp->certdata);
        resp->certdata = NULL;
        resp->certdata_len = 0;
    }
}

char* replace_cn_in_dn(const char* dn, const char* new_cn) {
    if (!dn || !new_cn) return NULL;

    // DN is format: "CN=A99D2F...,O=1,OU=773,S=msk,C=ru,L=1,E=1.terminal@forpay.ru"
    // Find "CN="
    const char* cn_start = stristr(dn, "CN=");
    if (!cn_start) {
        // Just prepend CN=new_cn,dn
        size_t len = strlen(new_cn) + strlen(dn) + 8;
        char* res = (char*)malloc(len);
        if (res) snprintf(res, len, "CN=%s,%s", new_cn, dn);
        return res;
    }

    const char* cn_val_start = cn_start + 3;
    const char* cn_val_end = strchr(cn_val_start, ',');
    if (!cn_val_end) {
        cn_val_end = cn_val_start + strlen(cn_val_start);
    }

    size_t prefix_len = (size_t)(cn_start - dn);
    size_t suffix_len = strlen(cn_val_end);
    size_t new_cn_len = strlen(new_cn);

    size_t total_len = prefix_len + 3 + new_cn_len + suffix_len + 1;
    char* res = (char*)malloc(total_len);
    if (!res) return NULL;

    char* p = res;
    if (prefix_len > 0) {
        memcpy(p, dn, prefix_len);
        p += prefix_len;
    }
    memcpy(p, "CN=", 3);
    p += 3;
    memcpy(p, new_cn, new_cn_len);
    p += new_cn_len;
    if (suffix_len > 0) {
        memcpy(p, cn_val_end, suffix_len);
        p += suffix_len;
    }
    *p = '\0';

    return res;
}

bool extract_email_from_dn(const char* dn, char* out_email, size_t out_email_size) {
    if (!dn || !out_email || out_email_size == 0) return false;
    out_email[0] = '\0';

    // Look for "E=" or "EMAIL=" or "EMAILADDRESS="
    const char* e_start = stristr(dn, "E=");
    if (!e_start) {
        e_start = stristr(dn, "EMAIL=");
        if (e_start) e_start += 6;
    } else {
        e_start += 2;
    }
    if (!e_start) return false;

    const char* e_end = strchr(e_start, ',');
    if (!e_end) e_end = e_start + strlen(e_start);

    size_t len = (size_t)(e_end - e_start);
    if (len >= out_email_size) len = out_email_size - 1;

    memcpy(out_email, e_start, len);
    out_email[len] = '\0';

    // Trim whitespace
    char* end_trim = out_email + strlen(out_email) - 1;
    while (end_trim >= out_email && isspace((unsigned char)*end_trim)) {
        *end_trim = '\0';
        end_trim--;
    }
    return (out_email[0] != '\0');
}
