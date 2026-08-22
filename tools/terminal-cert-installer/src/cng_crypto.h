#ifndef CNG_CRYPTO_H
#define CNG_CRYPTO_H

#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Generate a non-exportable RSA 2048 key in Microsoft Software Key Storage Provider
 * and create a PKCS#10 CSR.
 * 
 * provider_name: e.g. L"Microsoft Software Key Storage Provider" (NULL for default)
 * key_container_name: e.g. L"EtranTerminalKey"
 * dn_with_sign: full DN string with CN replaced by sign (e.g. "CN=B2B...,O=1,OU=773,...")
 * key_bits: key length in bits (e.g. 2048)
 * is_machine_context: true for LocalMachine, false for CurrentUser
 * out_pkcs10_b64: dynamically allocated base64 string, caller must free()
 */
bool cng_generate_key_and_csr(
    const WCHAR* provider_name,
    const WCHAR* key_container_name,
    const char* dn_with_sign,
    DWORD key_bits,
    bool is_machine_context,
    char** out_pkcs10_b64
);

/**
 * Delete a CNG key container if it exists.
 */
bool cng_delete_key_container(
    const WCHAR* provider_name,
    const WCHAR* key_container_name,
    bool is_machine_context
);

#ifdef __cplusplus
}
#endif

#endif // CNG_CRYPTO_H
