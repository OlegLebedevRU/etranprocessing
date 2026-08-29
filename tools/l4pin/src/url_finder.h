#ifndef URL_FINDER_H
#define URL_FINDER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_CERTIFICATES_URL "https://iot-processing.ru/api/certificates"

/**
 * Resolves the certificate endpoint URL according to the strategy:
 * 1. CLI argument (if provided and non-empty)
 * 2. leo4proxy detection (https://127.0.0.1/_leo4/info?format=json or http://127.0.0.1:18443/_leo4/info)
 *    extracts listeners.http_local -> https://<http_local>/api/certificates (or http://...)
 * 3. Default fallback: https://iot-processing.ru/api/certificates
 *
 * @param cli_url_arg Explicit URL from CLI or NULL
 * @param out_url Output buffer for resolved URL
 * @param out_url_size Size of out_url buffer (recommended >= 512)
 * @return true if URL resolved successfully, false otherwise.
 */
bool resolve_certificates_url(const char* cli_url_arg, char* out_url, size_t out_url_size);

#ifdef __cplusplus
}
#endif

#endif // URL_FINDER_H
