#ifndef CERT_DISCOVERY_H
#define CERT_DISCOVERY_H

#include <windows.h>
#include <wincrypt.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cert_state {
    CERT_VALID = 0,
    CERT_EXPIRING = 1,
    CERT_BROKEN = 2,
    CERT_ABSENT = 3,
    CERT_STORE_ERROR = 20
} cert_state;

typedef struct cert_info {
    char thumbprint_hex[41];
    char sn[64];
    char not_after_utc[32];
    char issuer_cn[64];
    bool has_private_key;
    int days_left;
    int cert_duplicates;
} cert_info;

/**
 * Discover terminal certificate in LocalMachine\MY store according to Contract 4.1.
 *
 * @param expected_sn  Target terminal serial number / Common Name (may be NULL).
 * @param out          Output info structure (may be NULL).
 * @return cert_state  CERT_VALID (0), CERT_EXPIRING (1), CERT_BROKEN (2), CERT_ABSENT (3),
 *                     or CERT_STORE_ERROR (>= 20) on store access failure.
 */
cert_state cert_discover(const wchar_t *expected_sn, cert_info *out);

/**
 * Discover certificate in a specific store (e.g. for testing with memory stores).
 *
 * @param hStore       Target certificate store handle.
 * @param expected_sn  Target terminal serial number / Common Name (may be NULL).
 * @param out          Output info structure (may be NULL).
 * @return cert_state  CERT_VALID, CERT_EXPIRING, CERT_BROKEN, or CERT_ABSENT.
 */
cert_state cert_discover_in_store(HCERTSTORE hStore, const wchar_t *expected_sn, cert_info *out);

/**
 * Returns string representation of cert_state: "valid", "expiring", "broken", "absent", or "error".
 */
const char* cert_state_to_str(cert_state state);

#ifdef __cplusplus
}
#endif

#endif /* CERT_DISCOVERY_H */
