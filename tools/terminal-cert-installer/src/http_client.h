#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Generate a unique base64 sysinfo string for tosign parameter.
 * Out_tosign buffer must be at least 128 bytes.
 */
bool generate_tosign(char* out_tosign, size_t out_tosign_size);

/**
 * Perform CHECK HTTP request (GET).
 * 
 * base_url: e.g. "https://iot-processing.ru" or "https://iot-processing.ru/api/certificates"
 * pin: terminal pin
 * tosign: base64 sysinfo
 * v: version string (e.g. "26")
 * out_response: dynamically allocated response XML, caller must free().
 * out_response_len: optional output length.
 */
bool http_check(
    const char* base_url,
    const char* pin,
    const char* tosign,
    const char* v,
    char** out_response,
    size_t* out_response_len
);

/**
 * Perform SETUP HTTP request (POST).
 * 
 * base_url: e.g. "https://iot-processing.ru" or "https://iot-processing.ru/api/certificates"
 * pin: terminal pin
 * sign: sign hash from CHECK step (used as cpserial)
 * pkcs10_b64: base64 encoded PKCS#10 CSR
 * out_response: dynamically allocated response XML, caller must free().
 * out_response_len: optional output length.
 */
bool http_setup(
    const char* base_url,
    const char* pin,
    const char* sign,
    const char* pkcs10_b64,
    char** out_response,
    size_t* out_response_len
);

#ifdef __cplusplus
}
#endif

#endif // HTTP_CLIENT_H
