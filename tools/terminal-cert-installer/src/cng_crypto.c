#include "cng_crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <ncrypt.h>
#include <wincrypt.h>

#pragma comment(lib, "ncrypt.lib")
#pragma comment(lib, "crypt32.lib")

bool cng_delete_key_container(
    const WCHAR* provider_name,
    const WCHAR* key_container_name,
    bool is_machine_context
) {
    if (!key_container_name) return false;

    NCRYPT_PROV_HANDLE hProv = 0;
    SECURITY_STATUS sec_status = NCryptOpenStorageProvider(
        &hProv,
        provider_name ? provider_name : MS_KEY_STORAGE_PROVIDER,
        0
    );
    if (sec_status != ERROR_SUCCESS) return false;

    NCRYPT_KEY_HANDLE hKey = 0;
    DWORD flags = is_machine_context ? NCRYPT_MACHINE_KEY_FLAG : 0;
    sec_status = NCryptOpenKey(hProv, &hKey, key_container_name, 0, flags);
    if (sec_status == ERROR_SUCCESS && hKey != 0) {
        NCryptDeleteKey(hKey, 0);
    }

    NCryptFreeObject(hProv);
    return true;
}

bool cng_generate_key_and_csr(
    const WCHAR* provider_name,
    const WCHAR* key_container_name,
    const char* dn_with_sign,
    DWORD key_bits,
    bool is_machine_context,
    char** out_pkcs10_b64
) {
    if (!key_container_name || !dn_with_sign || !out_pkcs10_b64) return false;
    *out_pkcs10_b64 = NULL;

    const WCHAR* prov = (provider_name && wcslen(provider_name) > 0) ? provider_name : MS_KEY_STORAGE_PROVIDER;

    // 1. Open CNG Storage Provider
    NCRYPT_PROV_HANDLE hProv = 0;
    SECURITY_STATUS status = NCryptOpenStorageProvider(&hProv, prov, 0);
    if (status != ERROR_SUCCESS) {
        fprintf(stderr, "NCryptOpenStorageProvider failed: 0x%08lX\n", status);
        return false;
    }

    // 2. Create persisted key in provider
    NCRYPT_KEY_HANDLE hKey = 0;
    DWORD create_flags = (is_machine_context ? NCRYPT_MACHINE_KEY_FLAG : 0) | NCRYPT_OVERWRITE_KEY_FLAG;
    status = NCryptCreatePersistedKey(
        hProv,
        &hKey,
        BCRYPT_RSA_ALGORITHM,
        key_container_name,
        0,
        create_flags
    );
    if (status != ERROR_SUCCESS) {
        fprintf(stderr, "NCryptCreatePersistedKey failed: 0x%08lX (is_machine=%d)\n", status, is_machine_context);
        NCryptFreeObject(hProv);
        return false;
    }

    // 3. Set key length (e.g. 2048)
    DWORD dwLength = key_bits > 0 ? key_bits : 2048;
    status = NCryptSetProperty(
        hKey,
        NCRYPT_LENGTH_PROPERTY,
        (PBYTE)&dwLength,
        sizeof(dwLength),
        0
    );
    if (status != ERROR_SUCCESS) {
        fprintf(stderr, "NCryptSetProperty(NCRYPT_LENGTH_PROPERTY) failed: 0x%08lX\n", status);
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    // 4. CRITICAL: SET EXPORT POLICY = 0 (NON-EXPORTABLE)
    // NCRYPT_ALLOW_EXPORT_NONE = 0
    DWORD dwExportPolicy = 0;
    status = NCryptSetProperty(
        hKey,
        NCRYPT_EXPORT_POLICY_PROPERTY,
        (PBYTE)&dwExportPolicy,
        sizeof(dwExportPolicy),
        0
    );
    if (status != ERROR_SUCCESS) {
        fprintf(stderr, "NCryptSetProperty(NCRYPT_EXPORT_POLICY_PROPERTY) failed: 0x%08lX\n", status);
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    // 5. Finalize key
    status = NCryptFinalizeKey(hKey, 0);
    if (status != ERROR_SUCCESS) {
        fprintf(stderr, "NCryptFinalizeKey failed: 0x%08lX\n", status);
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    // 6. Export Public Key Info for CSR
    DWORD cbPubKeyInfo = 0;
    if (!CryptExportPublicKeyInfoEx(
            (HCRYPTPROV_OR_NCRYPT_KEY_HANDLE)hKey,
            CERT_NCRYPT_KEY_SPEC,
            X509_ASN_ENCODING,
            (LPSTR)szOID_RSA_RSA,
            0,
            NULL,
            NULL,
            &cbPubKeyInfo
        )) {
        fprintf(stderr, "CryptExportPublicKeyInfoEx size query failed: %lu\n", GetLastError());
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    PCERT_PUBLIC_KEY_INFO pPubKeyInfo = (PCERT_PUBLIC_KEY_INFO)malloc(cbPubKeyInfo);
    if (!pPubKeyInfo) {
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    if (!CryptExportPublicKeyInfoEx(
            (HCRYPTPROV_OR_NCRYPT_KEY_HANDLE)hKey,
            CERT_NCRYPT_KEY_SPEC,
            X509_ASN_ENCODING,
            (LPSTR)szOID_RSA_RSA,
            0,
            NULL,
            pPubKeyInfo,
            &cbPubKeyInfo
        )) {
        fprintf(stderr, "CryptExportPublicKeyInfoEx failed: %lu\n", GetLastError());
        free(pPubKeyInfo);
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    // 7. Encode Subject Distinguished Name
    DWORD cbSubject = 0;
    if (!CertStrToNameA(
            X509_ASN_ENCODING,
            dn_with_sign,
            CERT_X500_NAME_STR,
            NULL,
            NULL,
            &cbSubject,
            NULL
        )) {
        fprintf(stderr, "CertStrToNameA size query failed: %lu (DN: %s)\n", GetLastError(), dn_with_sign);
        free(pPubKeyInfo);
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    BYTE* pbSubject = (BYTE*)malloc(cbSubject);
    if (!pbSubject) {
        free(pPubKeyInfo);
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    if (!CertStrToNameA(
            X509_ASN_ENCODING,
            dn_with_sign,
            CERT_X500_NAME_STR,
            NULL,
            pbSubject,
            &cbSubject,
            NULL
        )) {
        fprintf(stderr, "CertStrToNameA failed: %lu\n", GetLastError());
        free(pbSubject);
        free(pPubKeyInfo);
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    // 8. Build CERT_REQUEST_INFO
    CERT_REQUEST_INFO certReqInfo;
    memset(&certReqInfo, 0, sizeof(certReqInfo));
    certReqInfo.dwVersion = CERT_REQUEST_V1;
    certReqInfo.Subject.cbData = cbSubject;
    certReqInfo.Subject.pbData = pbSubject;
    certReqInfo.SubjectPublicKeyInfo = *pPubKeyInfo;
    certReqInfo.cAttribute = 0;
    certReqInfo.rgAttribute = NULL;

    // 9. Sign and encode PKCS#10
    CRYPT_ALGORITHM_IDENTIFIER sigAlg;
    memset(&sigAlg, 0, sizeof(sigAlg));
    sigAlg.pszObjId = (LPSTR)szOID_RSA_SHA256RSA;

    DWORD cbDer = 0;
    if (!CryptSignAndEncodeCertificate(
            (HCRYPTPROV_OR_NCRYPT_KEY_HANDLE)hKey,
            CERT_NCRYPT_KEY_SPEC,
            X509_ASN_ENCODING,
            X509_CERT_REQUEST_TO_BE_SIGNED,
            &certReqInfo,
            &sigAlg,
            NULL,
            NULL,
            &cbDer
        )) {
        fprintf(stderr, "CryptSignAndEncodeCertificate size query failed: %lu\n", GetLastError());
        free(pbSubject);
        free(pPubKeyInfo);
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    BYTE* pbDer = (BYTE*)malloc(cbDer);
    if (!pbDer) {
        free(pbSubject);
        free(pPubKeyInfo);
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    if (!CryptSignAndEncodeCertificate(
            (HCRYPTPROV_OR_NCRYPT_KEY_HANDLE)hKey,
            CERT_NCRYPT_KEY_SPEC,
            X509_ASN_ENCODING,
            X509_CERT_REQUEST_TO_BE_SIGNED,
            &certReqInfo,
            &sigAlg,
            NULL,
            pbDer,
            &cbDer
        )) {
        fprintf(stderr, "CryptSignAndEncodeCertificate failed: %lu\n", GetLastError());
        free(pbDer);
        free(pbSubject);
        free(pPubKeyInfo);
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    // 10. Convert DER to Base64 (single-line or standard base64)
    DWORD cchBase64 = 0;
    if (!CryptBinaryToStringA(
            pbDer,
            cbDer,
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
            NULL,
            &cchBase64
        )) {
        fprintf(stderr, "CryptBinaryToStringA size query failed: %lu\n", GetLastError());
        free(pbDer);
        free(pbSubject);
        free(pPubKeyInfo);
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    char* szBase64 = (char*)malloc(cchBase64);
    if (!szBase64) {
        free(pbDer);
        free(pbSubject);
        free(pPubKeyInfo);
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    if (!CryptBinaryToStringA(
            pbDer,
            cbDer,
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
            szBase64,
            &cchBase64
        )) {
        fprintf(stderr, "CryptBinaryToStringA failed: %lu\n", GetLastError());
        free(szBase64);
        free(pbDer);
        free(pbSubject);
        free(pPubKeyInfo);
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return false;
    }

    free(pbDer);
    free(pbSubject);
    free(pPubKeyInfo);
    NCryptFreeObject(hKey);
    NCryptFreeObject(hProv);

    *out_pkcs10_b64 = szBase64;
    return true;
}
