#ifndef XML_UTILS_H
#define XML_UTILS_H

#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char result[32];        // "OK" or "Error"
    int code;               // 0 for OK, >0 for errors
    char description[512];  // Error description
    char prov[256];         // Cryptographic provider
    char dn[512];           // Distinguished Name
    char pin[32];           // PIN
    char sign[128];         // Sign / MD5 hash
} CheckResponse;

typedef struct {
    char result[32];        // "OK" or "Error"
    int code;               // 0 for OK, >0 for errors
    char description[512];  // Error description
    char* certdata;         // Raw Base64 PKCS#7 (allocated dynamically, must be freed)
    size_t certdata_len;
} SetupResponse;

/**
 * Extract text between <tagName> and </tagName> into out_val.
 * Returns true if tag was found.
 */
bool xml_get_tag_value(const char* xml, const char* tag_name, char* out_val, size_t out_val_size);

/**
 * Extract dynamically allocated text between <tagName> and </tagName>.
 * Caller must free(*out_val).
 */
bool xml_get_tag_value_alloc(const char* xml, const char* tag_name, char** out_val, size_t* out_len);

/**
 * Parse CHECK response XML.
 */
bool parse_check_response(const char* xml, CheckResponse* out_resp);

/**
 * Parse SETUP response XML.
 */
bool parse_setup_response(const char* xml, SetupResponse* out_resp);

/**
 * Free resources in SetupResponse.
 */
void free_setup_response(SetupResponse* resp);

/**
 * Replace CN=<value> in DN string with CN=<new_cn>.
 * Returns dynamically allocated string, caller must free.
 */
char* replace_cn_in_dn(const char* dn, const char* new_cn);

/**
 * Extract email value (E=<email>) from DN string.
 */
bool extract_email_from_dn(const char* dn, char* out_email, size_t out_email_size);

#ifdef __cplusplus
}
#endif

#endif // XML_UTILS_H
