#include "cert_store.h"
#include "cert_discovery.h"
#include "xml_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")

bool cert_get_email(PCCERT_CONTEXT pCert, char* out_email, size_t out_email_size) {
    if (!pCert || !out_email || out_email_size == 0) return false;
    out_email[0] = '\0';

    // 1. Try standard CERT_NAME_EMAIL_TYPE
    DWORD len = CertGetNameStringA(
        pCert,
        CERT_NAME_EMAIL_TYPE,
        0,
        NULL,
        out_email,
        (DWORD)out_email_size
    );
    if (len > 1 && out_email[0] != '\0') {
        return true;
    }

    // 2. Parse E= from Subject Name
    char szSubject[1024] = { 0 };
    len = CertNameToStrA(
        X509_ASN_ENCODING,
        &pCert->pCertInfo->Subject,
        CERT_X500_NAME_STR,
        szSubject,
        sizeof(szSubject)
    );
    if (len > 1) {
        if (extract_email_from_dn(szSubject, out_email, out_email_size)) {
            return true;
        }
    }

    return false;
}

bool cert_get_serial_hex(PCCERT_CONTEXT pCert, char* out_serial, size_t out_serial_size) {
    if (!pCert || !out_serial || out_serial_size == 0) return false;
    out_serial[0] = '\0';

    PCRYPT_INTEGER_BLOB pSerial = &pCert->pCertInfo->SerialNumber;
    if (pSerial->cbData == 0 || pSerial->cbData * 2 + 1 > out_serial_size) {
        return false;
    }

    // CryptoAPI stores serial number in little-endian byte order
    char* p = out_serial;
    for (DWORD i = 0; i < pSerial->cbData; i++) {
        BYTE b = pSerial->pbData[pSerial->cbData - 1 - i];
        sprintf(p, "%02X", b);
        p += 2;
    }
    *p = '\0';
    return true;
}

bool cert_get_thumbprint_hex(PCCERT_CONTEXT pCert, char* out_thumbprint, size_t out_thumbprint_size) {
    if (!pCert || !out_thumbprint || out_thumbprint_size < 41) return false;
    out_thumbprint[0] = '\0';

    BYTE hash[20];
    DWORD hash_len = sizeof(hash);
    if (!CertGetCertificateContextProperty(pCert, CERT_SHA1_HASH_PROP_ID, hash, &hash_len)) {
        return false;
    }

    char* p = out_thumbprint;
    for (DWORD i = 0; i < hash_len; i++) {
        sprintf(p, "%02X", hash[i]);
        p += 2;
    }
    *p = '\0';
    return true;
}

static void filetime_to_str(const FILETIME* ft, char* out_str, size_t out_str_size) {
    if (!ft || !out_str || out_str_size == 0) return;
    SYSTEMTIME st;
    FileTimeToSystemTime(ft, &st);
    snprintf(out_str, out_str_size, "%04d-%02d-%02d %02d:%02d:%02d UTC",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static void fill_cert_details(PCCERT_CONTEXT pCert, CertDetails* details) {
    if (!pCert || !details) return;
    memset(details, 0, sizeof(CertDetails));

    CertNameToStrA(
        X509_ASN_ENCODING,
        &pCert->pCertInfo->Subject,
        CERT_X500_NAME_STR,
        details->subject,
        sizeof(details->subject)
    );

    CertNameToStrA(
        X509_ASN_ENCODING,
        &pCert->pCertInfo->Issuer,
        CERT_X500_NAME_STR,
        details->issuer,
        sizeof(details->issuer)
    );

    cert_get_serial_hex(pCert, details->serial, sizeof(details->serial));
    cert_get_email(pCert, details->email, sizeof(details->email));
    cert_get_thumbprint_hex(pCert, details->thumbprint, sizeof(details->thumbprint));

    filetime_to_str(&pCert->pCertInfo->NotBefore, details->not_before, sizeof(details->not_before));
    filetime_to_str(&pCert->pCertInfo->NotAfter, details->not_after, sizeof(details->not_after));

    // Test if private key is accessible
    NCRYPT_KEY_HANDLE hKey = 0;
    DWORD dwKeySpec = 0;
    BOOL bFreeKey = FALSE;
    if (CryptAcquireCertificatePrivateKey(
            pCert,
            CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_SILENT_FLAG,
            NULL,
            &hKey,
            &dwKeySpec,
            &bFreeKey
        )) {
        details->has_private_key = true;
        if (bFreeKey && hKey) {
            NCryptFreeObject(hKey);
        }
    } else {
        details->has_private_key = false;
    }
}

bool cert_store_cleanup_previous(HCERTSTORE store, PCCERT_CONTEXT installed_cert,
                                 const char* email, int* removed_count) {
    if (removed_count) *removed_count = 0;
    if (!store || !installed_cert || !email || !email[0]) return false;
    char installed_email[256] = { 0 };
    if (!cert_is_leo4_issuer(installed_cert) ||
        !cert_get_email(installed_cert, installed_email, sizeof(installed_email)) ||
        _stricmp(installed_email, email) != 0) return false;

    PCCERT_CONTEXT* previous = NULL;
    size_t count = 0;
    size_t capacity = 0;
    PCCERT_CONTEXT current = NULL;
    bool success = true;
    while ((current = CertEnumCertificatesInStore(store, current)) != NULL) {
        if (CertCompareCertificate(X509_ASN_ENCODING, current->pCertInfo,
                                   installed_cert->pCertInfo) ||
            !cert_is_leo4_issuer(current)) continue;

        if (count == capacity) {
            size_t new_capacity = capacity ? capacity * 2 : 8;
            PCCERT_CONTEXT* grown = realloc(previous, new_capacity * sizeof(*previous));
            if (!grown) { success = false; break; }
            previous = grown;
            capacity = new_capacity;
        }
        previous[count] = CertDuplicateCertificateContext(current);
        if (!previous[count]) { success = false; break; }
        count++;
    }
    if (current) CertFreeCertificateContext(current);

    size_t removed = 0;
    if (success) {
        for (size_t i = 0; i < count; i++) {
            PCCERT_CONTEXT to_delete = CertDuplicateCertificateContext(previous[i]);
            if (!to_delete || !CertDeleteCertificateFromStore(to_delete)) {
                if (!to_delete) fprintf(stderr, "[STORE] Could not duplicate old certificate\n");
                else fprintf(stderr, "[STORE] Could not remove old certificate: %lu\n", GetLastError());
                success = false;
                break;
            }
            removed++;
        }
    }

    if (success) {
        size_t matching = 0;
        bool found_new = false;
        current = NULL;
        while ((current = CertEnumCertificatesInStore(store, current)) != NULL) {
            if (!cert_is_leo4_issuer(current)) continue;
            matching++;
            if (CertCompareCertificate(X509_ASN_ENCODING, current->pCertInfo,
                                       installed_cert->pCertInfo)) found_new = true;
        }
        success = matching == 1 && found_new;
        if (!success) fprintf(stderr, "[STORE] Cleanup verification failed: %zu matching certificates\n", matching);
    }

    bool restored = true;
    if (!success && removed > 0) {
        for (size_t i = 0; i < count; i++) {
            if (!CertAddCertificateContextToStore(store, previous[i],
                                                  CERT_STORE_ADD_REPLACE_EXISTING, NULL)) {
                fprintf(stderr, "[STORE] CRITICAL: Could not restore previous certificate: %lu\n",
                        GetLastError());
                restored = false;
            }
        }
    }
    if (!success && !restored) {
        fprintf(stderr, "[STORE] CRITICAL: Previous certificate set could not be fully restored; inspect LocalMachine\\MY before restarting services\n");
    }
    for (size_t i = 0; i < count; i++) CertFreeCertificateContext(previous[i]);
    free(previous);
    if (success && removed_count) *removed_count = (int)removed;
    return success;
}

static void rollback_installed_certificate(PCCERT_CONTEXT installed_cert) {
    if (!installed_cert) return;
    PCCERT_CONTEXT to_delete = CertDuplicateCertificateContext(installed_cert);
    if (!to_delete || !CertDeleteCertificateFromStore(to_delete)) {
        fprintf(stderr, "[STORE] CRITICAL: Could not roll back new certificate: %lu\n",
                GetLastError());
    }
}

bool cert_store_install_pkcs7(
    const char* pkcs7_b64,
    const WCHAR* key_container_name,
    bool is_machine_store,
    const char* expected_email,
    CertDetails* out_installed_details
) {
    if (!pkcs7_b64 || !key_container_name) return false;

    // 1. Decode Base64 PKCS#7 to DER binary
    DWORD cbDer = 0;
    if (!CryptStringToBinaryA(
            pkcs7_b64,
            (DWORD)strlen(pkcs7_b64),
            CRYPT_STRING_BASE64_ANY,
            NULL,
            &cbDer,
            NULL,
            NULL
        )) {
        fprintf(stderr, "CryptStringToBinaryA failed to decode PKCS#7: %lu\n", GetLastError());
        return false;
    }

    BYTE* pbDer = (BYTE*)malloc(cbDer);
    if (!pbDer) return false;

    if (!CryptStringToBinaryA(
            pkcs7_b64,
            (DWORD)strlen(pkcs7_b64),
            CRYPT_STRING_BASE64_ANY,
            pbDer,
            &cbDer,
            NULL,
            NULL
        )) {
        fprintf(stderr, "CryptStringToBinaryA failed: %lu\n", GetLastError());
        free(pbDer);
        return false;
    }

    // 2. Open temporary PKCS#7 memory store
    CRYPT_DATA_BLOB blob;
    blob.cbData = cbDer;
    blob.pbData = pbDer;

    HCERTSTORE hPkcs7Store = CertOpenStore(
        CERT_STORE_PROV_PKCS7,
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        0,
        0,
        &blob
    );
    if (!hPkcs7Store) {
        fprintf(stderr, "CertOpenStore(PKCS7) failed: %lu\n", GetLastError());
        free(pbDer);
        return false;
    }

    // 3. Find Leaf and CA certificates
    PCCERT_CONTEXT pLeafCert = NULL;
    PCCERT_CONTEXT pCaCert = NULL;

    PCCERT_CONTEXT pCur = NULL;
    while ((pCur = CertEnumCertificatesInStore(hPkcs7Store, pCur)) != NULL) {
        // Compare Subject and Issuer
        if (CertCompareCertificateName(X509_ASN_ENCODING, &pCur->pCertInfo->Subject, &pCur->pCertInfo->Issuer)) {
            // Self-signed CA
            if (!pCaCert) pCaCert = CertDuplicateCertificateContext(pCur);
        } else {
            // Leaf cert or intermediate
            if (!pLeafCert) {
                pLeafCert = CertDuplicateCertificateContext(pCur);
            } else if (!pCaCert) {
                pCaCert = CertDuplicateCertificateContext(pCur);
            }
        }
    }

    if (!pLeafCert && pCaCert) {
        pLeafCert = pCaCert;
        pCaCert = NULL;
    }

    if (!pLeafCert) {
        fprintf(stderr, "No certificates found inside PKCS#7 payload\n");
        CertCloseStore(hPkcs7Store, 0);
        free(pbDer);
        return false;
    }

    // Determine target email for filtering
    char new_cert_email[256] = { 0 };
    cert_get_email(pLeafCert, new_cert_email, sizeof(new_cert_email));
    if (new_cert_email[0] == '\0' ||
        (expected_email && expected_email[0] && _stricmp(new_cert_email, expected_email) != 0)) {
        fprintf(stderr, "[STORE] Signed certificate email is missing or differs from CHECK; refusing replacement\n");
        CertFreeCertificateContext(pLeafCert);
        if (pCaCert) CertFreeCertificateContext(pCaCert);
        CertCloseStore(hPkcs7Store, 0);
        free(pbDer);
        return false;
    }

    printf("[STORE] New certificate target email: [%s]\n", new_cert_email);

    // 4. Open destination MY store
    DWORD store_flags = is_machine_store ? CERT_SYSTEM_STORE_LOCAL_MACHINE : CERT_SYSTEM_STORE_CURRENT_USER;
    HCERTSTORE hMyStore = CertOpenStore(
        CERT_STORE_PROV_SYSTEM_W,
        0,
        0,
        store_flags,
        L"MY"
    );
    if (!hMyStore) {
        DWORD err = GetLastError();
        fprintf(stderr, "CertOpenStore(MY, %s) failed: %lu (Make sure to run with Administrator privileges for LocalMachine)\n",
                is_machine_store ? "LocalMachine" : "CurrentUser", err);
        CertFreeCertificateContext(pLeafCert);
        if (pCaCert) CertFreeCertificateContext(pCaCert);
        CertCloseStore(hPkcs7Store, 0);
        free(pbDer);
        return false;
    }

    // 5. Add new leaf certificate before touching the previous identity.
    PCCERT_CONTEXT pAddedCert = NULL;
    if (!CertAddCertificateContextToStore(
            hMyStore,
            pLeafCert,
            CERT_STORE_ADD_REPLACE_EXISTING,
            &pAddedCert
        )) {
        fprintf(stderr, "CertAddCertificateContextToStore(MY) failed: %lu\n", GetLastError());
        CertCloseStore(hMyStore, 0);
        CertFreeCertificateContext(pLeafCert);
        if (pCaCert) CertFreeCertificateContext(pCaCert);
        CertCloseStore(hPkcs7Store, 0);
        free(pbDer);
        return false;
    }

    // 6. Bind CNG Key Storage Provider to the installed certificate
    CRYPT_KEY_PROV_INFO provInfo;
    memset(&provInfo, 0, sizeof(provInfo));
    provInfo.pwszContainerName = (LPWSTR)key_container_name;
    provInfo.pwszProvName = (LPWSTR)MS_KEY_STORAGE_PROVIDER;
    provInfo.dwProvType = 0; // 0 for CNG
    provInfo.dwFlags = is_machine_store ? CRYPT_MACHINE_KEYSET : 0;
    provInfo.dwKeySpec = 0; // 0 for CNG (required by Schannel; 0xFFFFFFFF causes SEC_E_UNKNOWN_CREDENTIALS 0x8009030D)

    if (!CertSetCertificateContextProperty(
            pAddedCert,
            CERT_KEY_PROV_INFO_PROP_ID,
            0,
            &provInfo
        )) {
        fprintf(stderr, "CertSetCertificateContextProperty(CERT_KEY_PROV_INFO_PROP_ID) failed: %lu\n", GetLastError());
        rollback_installed_certificate(pAddedCert);
        CertFreeCertificateContext(pAddedCert);
        CertCloseStore(hMyStore, 0);
        CertFreeCertificateContext(pLeafCert);
        if (pCaCert) CertFreeCertificateContext(pCaCert);
        CertCloseStore(hPkcs7Store, 0);
        free(pbDer);
        return false;
    } else {
        printf("[STORE] Successfully bound CNG private key [%ls] to certificate\n", key_container_name);
    }

    CertDetails installed_details;
    fill_cert_details(pAddedCert, &installed_details);
    if (!installed_details.has_private_key) {
        fprintf(stderr, "[STORE] New certificate key is not accessible; previous certificates retained\n");
        rollback_installed_certificate(pAddedCert);
        CertFreeCertificateContext(pAddedCert);
        CertCloseStore(hMyStore, 0);
        CertFreeCertificateContext(pLeafCert);
        if (pCaCert) CertFreeCertificateContext(pCaCert);
        CertCloseStore(hPkcs7Store, 0);
        free(pbDer);
        return false;
    }

    // 7. Remove previous identities only after the new key is usable.
    int removed_count = 0;
    bool cleanup_ok = cert_store_cleanup_previous(hMyStore, pAddedCert,
                                                   new_cert_email, &removed_count);
    printf("[STORE] Removed %d previous certificate(s) for email [%s]\n",
           removed_count, new_cert_email);
    if (!cleanup_ok) {
        rollback_installed_certificate(pAddedCert);
        fprintf(stderr, "[STORE] Certificate replacement failed; check CRITICAL rollback messages and inspect the store before restarting services\n");
    }

    // 8. If CA certificate is present, install to CA store
    if (pCaCert) {
        HCERTSTORE hCaStore = CertOpenStore(
            CERT_STORE_PROV_SYSTEM_W,
            0,
            0,
            store_flags,
            L"CA"
        );
        if (hCaStore) {
            if (CertAddCertificateContextToStore(hCaStore, pCaCert, CERT_STORE_ADD_REPLACE_EXISTING, NULL)) {
                printf("[STORE] Installed CA certificate to Intermediate Store (CA)\n");
            }
            CertCloseStore(hCaStore, 0);
        }
    }

    // 9. Output details
    if (cleanup_ok && out_installed_details) {
        *out_installed_details = installed_details;
    }

    if (pAddedCert) CertFreeCertificateContext(pAddedCert);
    if (pLeafCert) CertFreeCertificateContext(pLeafCert);
    if (pCaCert) CertFreeCertificateContext(pCaCert);

    CertCloseStore(hMyStore, 0);
    CertCloseStore(hPkcs7Store, 0);
    free(pbDer);

    return cleanup_ok;
}

bool cert_store_list(bool is_machine_store, const char* filter_email) {
    DWORD store_flags = is_machine_store ? CERT_SYSTEM_STORE_LOCAL_MACHINE : CERT_SYSTEM_STORE_CURRENT_USER;
    HCERTSTORE hMyStore = CertOpenStore(
        CERT_STORE_PROV_SYSTEM_W,
        0,
        0,
        store_flags | CERT_STORE_READONLY_FLAG,
        L"MY"
    );
    if (!hMyStore) {
        fprintf(stderr, "CertOpenStore(MY, %s) failed: %lu\n",
                is_machine_store ? "LocalMachine" : "CurrentUser", GetLastError());
        return false;
    }

    printf("\n=== Certificates in Store [%s\\MY] ===\n", is_machine_store ? "LocalMachine" : "CurrentUser");

    int index = 0;
    PCCERT_CONTEXT pCur = NULL;
    while ((pCur = CertEnumCertificatesInStore(hMyStore, pCur)) != NULL) {
        CertDetails details;
        fill_cert_details(pCur, &details);

        if (filter_email && filter_email[0] != '\0') {
            if (_stricmp(details.email, filter_email) != 0) {
                continue;
            }
        }

        index++;
        printf("[%d] Subject:     %s\n", index, details.subject);
        printf("    Email:       %s\n", details.email[0] ? details.email : "(none)");
        printf("    Serial:      %s\n", details.serial);
        printf("    Thumbprint:  %s\n", details.thumbprint);
        printf("    Valid:       %s to %s\n", details.not_before, details.not_after);
        printf("    Private Key: %s\n", details.has_private_key ? "YES (Accessible via CNG)" : "NO");
        printf("    Issuer:      %s\n\n", details.issuer);
    }

    if (index == 0) {
        printf("No certificates found%s.\n\n", filter_email ? " matching filter" : "");
    }

    CertCloseStore(hMyStore, 0);
    return true;
}
