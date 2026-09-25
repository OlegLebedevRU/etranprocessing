#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include "cert_discovery.h"

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")
#pragma comment(lib, "advapi32.lib")

static wchar_t g_test_key_container[96];

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define RUN_TEST(fn) do { \
    g_tests_run++; \
    printf("[RUN]  %s\n", #fn); \
    if (fn()) { \
        g_tests_passed++; \
        printf("[PASS] %s\n", #fn); \
    } else { \
        printf("[FAIL] %s\n", #fn); \
    } \
} while(0)

#define RUN_TEST_KEY(fn, k) do { \
    g_tests_run++; \
    printf("[RUN]  %s\n", #fn); \
    if (fn(k)) { \
        g_tests_passed++; \
        printf("[PASS] %s\n", #fn); \
    } else { \
        printf("[FAIL] %s\n", #fn); \
    } \
} while(0)

static PCCERT_CONTEXT create_test_cert(
    NCRYPT_KEY_HANDLE hKey,
    const char* dn,
    int days_valid,
    bool bind_cng_key
) {
    SYSTEMTIME stStart;
    GetSystemTime(&stStart);

    FILETIME ftNow;
    SystemTimeToFileTime(&stStart, &ftNow);
    ULARGE_INTEGER uli;
    uli.LowPart = ftNow.dwLowDateTime;
    uli.HighPart = ftNow.dwHighDateTime;
    uli.QuadPart += (ULONGLONG)days_valid * 864000000000ULL;

    FILETIME ftEnd;
    ftEnd.dwLowDateTime = uli.LowPart;
    ftEnd.dwHighDateTime = uli.HighPart;

    SYSTEMTIME stEnd;
    FileTimeToSystemTime(&ftEnd, &stEnd);

    BYTE pbEncoded[512] = { 0 };
    DWORD cbEncoded = sizeof(pbEncoded);
    if (!CertStrToNameA(X509_ASN_ENCODING, dn, CERT_X500_NAME_STR, NULL, pbEncoded, &cbEncoded, NULL)) {
        fprintf(stderr, "CertStrToNameA failed: %lu\n", GetLastError());
        return NULL;
    }

    CERT_NAME_BLOB nameBlob;
    nameBlob.cbData = cbEncoded;
    nameBlob.pbData = pbEncoded;

    CRYPT_KEY_PROV_INFO kpi;
    memset(&kpi, 0, sizeof(kpi));
    kpi.pwszContainerName = (LPWSTR)g_test_key_container;
    kpi.pwszProvName = (LPWSTR)MS_KEY_STORAGE_PROVIDER;
    kpi.dwProvType = 0;
    kpi.dwFlags = 0; // Current user
    kpi.dwKeySpec = 0;

    PCCERT_CONTEXT pCert = CertCreateSelfSignCertificate(
        bind_cng_key ? (HCRYPTPROV_OR_NCRYPT_KEY_HANDLE)hKey : 0,
        &nameBlob,
        0,
        bind_cng_key ? &kpi : NULL,
        NULL,
        &stStart,
        &stEnd,
        NULL
    );

    if (pCert && bind_cng_key) {
        CertSetCertificateContextProperty(pCert, CERT_KEY_PROV_INFO_PROP_ID, 0, &kpi);
    }

    return pCert;
}

static bool test_absent(void) {
    HCERTSTORE hMemStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    if (!hMemStore) return false;

    cert_info info;
    cert_state st = cert_discover_in_store(hMemStore, L"test_sn", &info);
    CertCloseStore(hMemStore, 0);

    if (st != CERT_ABSENT) {
        fprintf(stderr, "Expected CERT_ABSENT, got %d\n", st);
        return false;
    }
    if (info.thumbprint_hex[0] != '\0' || info.days_left != 0) {
        fprintf(stderr, "Expected empty info for ABSENT\n");
        return false;
    }
    return true;
}

static bool test_valid(NCRYPT_KEY_HANDLE hKey) {
    HCERTSTORE hMemStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    if (!hMemStore) return false;

    PCCERT_CONTEXT pCert = create_test_cert(hKey, "CN=iot.leo4.ru", 90, true);
    if (!pCert) {
        CertCloseStore(hMemStore, 0);
        return false;
    }
    CertAddCertificateContextToStore(hMemStore, pCert, CERT_STORE_ADD_ALWAYS, NULL);
    CertFreeCertificateContext(pCert);

    cert_info info;
    cert_state st = cert_discover_in_store(hMemStore, L"iot.leo4.ru", &info);
    CertCloseStore(hMemStore, 0);

    if (st != CERT_VALID) {
        fprintf(stderr, "Expected CERT_VALID, got %d\n", st);
        return false;
    }
    if (info.days_left <= 30) {
        fprintf(stderr, "Expected days_left > 30, got %d\n", info.days_left);
        return false;
    }
    if (!info.has_private_key) {
        fprintf(stderr, "Expected has_private_key == true\n");
        return false;
    }
    if (info.cert_duplicates != 0) {
        fprintf(stderr, "Expected duplicates == 0, got %d\n", info.cert_duplicates);
        return false;
    }
    if (strlen(info.thumbprint_hex) != 40) {
        fprintf(stderr, "Expected 40-char thumbprint, got %s\n", info.thumbprint_hex);
        return false;
    }
    return true;
}

static bool test_expiring(NCRYPT_KEY_HANDLE hKey) {
    HCERTSTORE hMemStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    if (!hMemStore) return false;

    PCCERT_CONTEXT pCert = create_test_cert(hKey, "CN=iot.leo4.ru", 10, true);
    if (!pCert) {
        CertCloseStore(hMemStore, 0);
        return false;
    }
    CertAddCertificateContextToStore(hMemStore, pCert, CERT_STORE_ADD_ALWAYS, NULL);
    CertFreeCertificateContext(pCert);

    cert_info info;
    cert_state st = cert_discover_in_store(hMemStore, L"iot.leo4.ru", &info);
    CertCloseStore(hMemStore, 0);

    if (st != CERT_EXPIRING) {
        fprintf(stderr, "Expected CERT_EXPIRING, got %d\n", st);
        return false;
    }
    if (info.days_left > 30 || info.days_left < 1) {
        fprintf(stderr, "Expected 1 <= days_left <= 30, got %d\n", info.days_left);
        return false;
    }
    if (!info.has_private_key) {
        fprintf(stderr, "Expected has_private_key == true\n");
        return false;
    }
    return true;
}

static bool test_broken_no_key(NCRYPT_KEY_HANDLE hKey) {
    HCERTSTORE hMemStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    if (!hMemStore) return false;

    // Create cert with key initially, but add encoded copy without key provider property
    PCCERT_CONTEXT pCert = create_test_cert(hKey, "CN=iot.leo4.ru", 90, false);
    if (!pCert) {
        CertCloseStore(hMemStore, 0);
        return false;
    }
    // Explicitly ensure no key property is attached
    CertSetCertificateContextProperty(pCert, CERT_KEY_PROV_INFO_PROP_ID, 0, NULL);
    CertAddCertificateContextToStore(hMemStore, pCert, CERT_STORE_ADD_ALWAYS, NULL);
    CertFreeCertificateContext(pCert);

    cert_info info;
    cert_state st = cert_discover_in_store(hMemStore, L"iot.leo4.ru", &info);
    CertCloseStore(hMemStore, 0);

    if (st != CERT_BROKEN) {
        fprintf(stderr, "Expected CERT_BROKEN for missing key, got %d\n", st);
        return false;
    }
    if (info.has_private_key) {
        fprintf(stderr, "Expected has_private_key == false\n");
        return false;
    }
    return true;
}

static bool test_broken_wrong_sn(NCRYPT_KEY_HANDLE hKey) {
    HCERTSTORE hMemStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    if (!hMemStore) return false;

    PCCERT_CONTEXT pCert = create_test_cert(hKey, "CN=iot.leo4.ru", 90, true);
    if (!pCert) {
        CertCloseStore(hMemStore, 0);
        return false;
    }
    CertAddCertificateContextToStore(hMemStore, pCert, CERT_STORE_ADD_ALWAYS, NULL);
    CertFreeCertificateContext(pCert);

    cert_info info;
    // Query with different expected SN
    cert_state st = cert_discover_in_store(hMemStore, L"WRONGSN", &info);
    CertCloseStore(hMemStore, 0);

    if (st != CERT_BROKEN) {
        fprintf(stderr, "Expected CERT_BROKEN for wrong SN, got %d\n", st);
        return false;
    }
    return true;
}

static bool test_duplicates(NCRYPT_KEY_HANDLE hKey) {
    HCERTSTORE hMemStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    if (!hMemStore) return false;

    // Add first candidate: valid for 60 days
    PCCERT_CONTEXT pCert1 = create_test_cert(hKey, "CN=iot.leo4.ru", 60, true);
    if (!pCert1) {
        CertCloseStore(hMemStore, 0);
        return false;
    }
    CertAddCertificateContextToStore(hMemStore, pCert1, CERT_STORE_ADD_ALWAYS, NULL);
    CertFreeCertificateContext(pCert1);

    // Add second candidate: valid for 120 days
    PCCERT_CONTEXT pCert2 = create_test_cert(hKey, "CN=iot.leo4.ru", 120, true);
    if (!pCert2) {
        CertCloseStore(hMemStore, 0);
        return false;
    }
    CertAddCertificateContextToStore(hMemStore, pCert2, CERT_STORE_ADD_ALWAYS, NULL);
    CertFreeCertificateContext(pCert2);

    cert_info info;
    cert_state st = cert_discover_in_store(hMemStore, L"iot.leo4.ru", &info);
    CertCloseStore(hMemStore, 0);

    if (st != CERT_VALID) {
        fprintf(stderr, "Expected CERT_VALID, got %d\n", st);
        return false;
    }
    if (info.cert_duplicates != 1) {
        fprintf(stderr, "Expected cert_duplicates == 1, got %d\n", info.cert_duplicates);
        return false;
    }
    // The candidate with 120 days should be selected (days_left approx 119..120)
    if (info.days_left < 100) {
        fprintf(stderr, "Expected best candidate with ~120 days, got %d days\n", info.days_left);
        return false;
    }
    return true;
}

static bool test_non_leo4_issuer(NCRYPT_KEY_HANDLE hKey) {
    HCERTSTORE hMemStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    if (!hMemStore) return false;

    // Create cert with unrelated issuer
    PCCERT_CONTEXT pCert = create_test_cert(hKey, "CN=other.unrelated.domain", 90, true);
    if (!pCert) {
        CertCloseStore(hMemStore, 0);
        return false;
    }
    CertAddCertificateContextToStore(hMemStore, pCert, CERT_STORE_ADD_ALWAYS, NULL);
    CertFreeCertificateContext(pCert);

    cert_info info;
    cert_state st = cert_discover_in_store(hMemStore, NULL, &info);
    CertCloseStore(hMemStore, 0);

    if (st != CERT_ABSENT) {
        fprintf(stderr, "Expected CERT_ABSENT for non-leo4 issuer, got %d\n", st);
        return false;
    }
    return true;
}

int main(void) {
    SetConsoleOutputCP(CP_UTF8);

    printf("=======================================================\n");
    printf(" Running Cert Discovery Unit Tests (In-Memory Stores)\n");
    printf("=======================================================\n\n");

    // Initialize CNG test key in Current User KSP
    NCRYPT_PROV_HANDLE hProv = 0;
    NCRYPT_KEY_HANDLE hKey = 0;
    SECURITY_STATUS ss = NCryptOpenStorageProvider(&hProv, MS_KEY_STORAGE_PROVIDER, 0);
    if (ss != ERROR_SUCCESS) {
        fprintf(stderr, "NCryptOpenStorageProvider failed: 0x%08lx\n", ss);
        return 1;
    }

    // Create only a fresh test-owned key; never open or delete an existing container.
    HCRYPTPROV random_provider = 0;
    BYTE nonce[16] = { 0 };
    if (!CryptAcquireContextW(&random_provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) ||
        !CryptGenRandom(random_provider, sizeof(nonce), nonce)) {
        if (random_provider) CryptReleaseContext(random_provider, 0);
        NCryptFreeObject(hProv);
        return 1;
    }
    CryptReleaseContext(random_provider, 0);
    swprintf_s(g_test_key_container, sizeof(g_test_key_container) / sizeof(g_test_key_container[0]),
        L"L4PinDiscoveryTest-%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
        nonce[0], nonce[1], nonce[2], nonce[3], nonce[4], nonce[5], nonce[6], nonce[7],
        nonce[8], nonce[9], nonce[10], nonce[11], nonce[12], nonce[13], nonce[14], nonce[15]);
    SecureZeroMemory(nonce, sizeof(nonce));
    ss = NCryptCreatePersistedKey(hProv, &hKey, BCRYPT_RSA_ALGORITHM, g_test_key_container, 0, 0);
    if (ss == ERROR_SUCCESS) {
        DWORD keyLength = 2048;
        ss = NCryptSetProperty(hKey, NCRYPT_LENGTH_PROPERTY, (PBYTE)&keyLength, sizeof(keyLength), 0);
        if (ss == ERROR_SUCCESS) ss = NCryptFinalizeKey(hKey, 0);
    }

    if (ss != ERROR_SUCCESS) {
        fprintf(stderr, "Failed to initialize CNG test key: 0x%08lx\n", ss);
        if (hKey) {
            if (NCryptDeleteKey(hKey, 0) != ERROR_SUCCESS) NCryptFreeObject(hKey);
        }
        NCryptFreeObject(hProv);
        return 1;
    }

    RUN_TEST(test_absent);
    RUN_TEST_KEY(test_valid, hKey);
    RUN_TEST_KEY(test_expiring, hKey);
    RUN_TEST_KEY(test_broken_no_key, hKey);
    RUN_TEST_KEY(test_broken_wrong_sn, hKey);
    RUN_TEST_KEY(test_duplicates, hKey);
    RUN_TEST_KEY(test_non_leo4_issuer, hKey);

    // Cleanup key container
    if (NCryptDeleteKey(hKey, 0) != ERROR_SUCCESS) {
        fprintf(stderr, "Failed to remove test-owned CNG key\n");
        NCryptFreeObject(hKey);
        NCryptFreeObject(hProv);
        return 1;
    }
    NCryptFreeObject(hProv);

    printf("\n=======================================================\n");
    printf(" Test Results: %d / %d PASSED\n", g_tests_passed, g_tests_run);
    printf("=======================================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
