#ifndef CERT_STORE_H
#define CERT_STORE_H

#include <windows.h>
#include <wincrypt.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char subject[512];
    char issuer[512];
    char serial[128];
    char email[256];
    char not_before[64];
    char not_after[64];
    char thumbprint[128];
    bool has_private_key;
} CertDetails;

/**
 * Extract email address from certificate context (Subject E=... or SAN rfc822Name).
 */
bool cert_get_email(PCCERT_CONTEXT pCert, char* out_email, size_t out_email_size);

/**
 * Format certificate serial number as HEX string.
 */
bool cert_get_serial_hex(PCCERT_CONTEXT pCert, char* out_serial, size_t out_serial_size);

/**
 * Format certificate SHA-1 thumbprint as HEX string.
 */
bool cert_get_thumbprint_hex(PCCERT_CONTEXT pCert, char* out_thumbprint, size_t out_thumbprint_size);

/**
 * Install PKCS#7 certificate chain into Windows Certificate Store (LocalMachine\MY or CurrentUser\MY),
 * delete all previous certificates with matching email, and bind CNG private key.
 * 
 * pkcs7_b64: raw Base64 string of the PKCS#7 response
 * key_container_name: CNG key container name (e.g. L"EtranTerminalKey")
 * is_machine_store: true for LocalMachine\MY, false for CurrentUser\MY
 * expected_email: email filter (optional, if NULL extracted from certificate)
 * out_installed_details: optional output struct with installed certificate details
 */
bool cert_store_install_pkcs7(
    const char* pkcs7_b64,
    const WCHAR* key_container_name,
    bool is_machine_store,
    const char* expected_email,
    CertDetails* out_installed_details
);

/**
 * List all certificates in MY store (and optionally filter by email).
 */
bool cert_store_list(bool is_machine_store, const char* filter_email);

#ifdef __cplusplus
}
#endif

#endif // CERT_STORE_H
