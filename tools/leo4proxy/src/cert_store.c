/**
 * @file cert_store.c
 * @brief Windows Certificate Store inspection and certificate selection for Leo4Proxy.
 */

#include "cert_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <ncrypt.h>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")

static void filetime_to_iso(const FILETIME* ft, char* out_str, size_t out_str_size) {
    if (!ft || !out_str || out_str_size == 0) return;
    SYSTEMTIME st;
    FileTimeToSystemTime(ft, &st);
    snprintf(out_str, out_str_size, "%04d-%02d-%02d %02d:%02d:%02d UTC",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static bool cert_get_serial_hex(PCCERT_CONTEXT pCert, char* out_serial, size_t out_serial_size) {
    if (!pCert || !out_serial || out_serial_size == 0) return false;
    out_serial[0] = '\0';

    PCRYPT_INTEGER_BLOB pSerial = &pCert->pCertInfo->SerialNumber;
    if (pSerial->cbData == 0 || pSerial->cbData * 2 + 1 > out_serial_size) {
        return false;
    }

    char* p = out_serial;
    for (DWORD i = 0; i < pSerial->cbData; i++) {
        BYTE b = pSerial->pbData[pSerial->cbData - 1 - i];
        sprintf(p, "%02X", b);
        p += 2;
    }
    *p = '\0';
    return true;
}

static bool cert_get_thumbprint_hex(PCCERT_CONTEXT pCert, char* out_thumbprint, size_t out_thumbprint_size) {
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

static bool extract_value_from_dn(const char* dn, const char* key, char* out_val, size_t out_val_size) {
    if (!dn || !key || !out_val || out_val_size == 0) return false;
    out_val[0] = '\0';

    const char* p = dn;
    size_t key_len = strlen(key);

    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        if (_strnicmp(p, key, key_len) == 0 && p[key_len] == '=') {
            p += key_len + 1;
            while (*p == ' ' || *p == '\"') p++;

            size_t i = 0;
            while (*p && *p != ',' && *p != '\"' && i + 1 < out_val_size) {
                out_val[i++] = *p++;
            }
            out_val[i] = '\0';
            return i > 0;
        }
        while (*p && *p != ',') p++;
    }

    return false;
}

static bool cert_get_email(PCCERT_CONTEXT pCert, char* out_email, size_t out_email_size) {
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
        if (extract_value_from_dn(szSubject, "E", out_email, out_email_size) ||
            extract_value_from_dn(szSubject, "emailAddress", out_email, out_email_size)) {
            return true;
        }
    }

    return false;
}

static bool cert_get_cn(PCCERT_CONTEXT pCert, char* out_cn, size_t out_cn_size) {
    if (!pCert || !out_cn || out_cn_size == 0) return false;
    out_cn[0] = '\0';

    // 1. Try CertGetNameStringA with CERT_NAME_ATTR_TYPE for CN
    DWORD len = CertGetNameStringA(
        pCert,
        CERT_NAME_ATTR_TYPE,
        0,
        szOID_COMMON_NAME,
        out_cn,
        (DWORD)out_cn_size
    );
    if (len > 1 && out_cn[0] != '\0') {
        return true;
    }

    // 2. Fallback: Parse CN= from Subject Name
    char szSubject[1024] = { 0 };
    len = CertNameToStrA(
        X509_ASN_ENCODING,
        &pCert->pCertInfo->Subject,
        CERT_X500_NAME_STR,
        szSubject,
        sizeof(szSubject)
    );
    if (len > 1) {
        if (extract_value_from_dn(szSubject, "CN", out_cn, out_cn_size)) {
            return true;
        }
    }

    return false;
}

static bool cert_get_san_urn(PCCERT_CONTEXT pCert, char* out_urn, size_t out_urn_size) {
    if (!pCert || !out_urn || out_urn_size == 0) return false;
    out_urn[0] = '\0';

    PCERT_EXTENSION pExt = CertFindExtension(szOID_SUBJECT_ALT_NAME2, pCert->pCertInfo->cExtension, pCert->pCertInfo->rgExtension);
    if (!pExt) {
        pExt = CertFindExtension(szOID_SUBJECT_ALT_NAME, pCert->pCertInfo->cExtension, pCert->pCertInfo->rgExtension);
    }
    if (!pExt) return false;

    DWORD cbDecoded = 0;
    if (!CryptDecodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, szOID_SUBJECT_ALT_NAME2, pExt->Value.pbData, pExt->Value.cbData, 0, NULL, &cbDecoded)) {
        return false;
    }

    CERT_ALT_NAME_INFO* pAltName = (CERT_ALT_NAME_INFO*)malloc(cbDecoded);
    if (!pAltName) return false;

    bool found = false;
    if (CryptDecodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, szOID_SUBJECT_ALT_NAME2, pExt->Value.pbData, pExt->Value.cbData, 0, pAltName, &cbDecoded)) {
        for (DWORD i = 0; i < pAltName->cAltEntry; i++) {
            if (pAltName->rgAltEntry[i].dwAltNameChoice == CERT_ALT_NAME_URL && pAltName->rgAltEntry[i].pwszURL) {
                WideCharToMultiByte(CP_UTF8, 0, pAltName->rgAltEntry[i].pwszURL, -1, out_urn, (int)out_urn_size, NULL, NULL);
                found = true;
                break;
            }
        }
    }

    free(pAltName);
    return found;
}

static int cert_get_all_san_dns_names(PCCERT_CONTEXT pCert, char out_dns[8][128], int max_count) {
    if (!pCert || !out_dns || max_count <= 0) return 0;
    int count = 0;

    PCERT_EXTENSION pExt = CertFindExtension(szOID_SUBJECT_ALT_NAME2, pCert->pCertInfo->cExtension, pCert->pCertInfo->rgExtension);
    if (!pExt) {
        pExt = CertFindExtension(szOID_SUBJECT_ALT_NAME, pCert->pCertInfo->cExtension, pCert->pCertInfo->rgExtension);
    }
    if (!pExt) return 0;

    DWORD cbDecoded = 0;
    if (!CryptDecodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, szOID_SUBJECT_ALT_NAME2, pExt->Value.pbData, pExt->Value.cbData, 0, NULL, &cbDecoded)) {
        return 0;
    }

    CERT_ALT_NAME_INFO* pAltName = (CERT_ALT_NAME_INFO*)malloc(cbDecoded);
    if (!pAltName) return 0;

    if (CryptDecodeObject(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, szOID_SUBJECT_ALT_NAME2, pExt->Value.pbData, pExt->Value.cbData, 0, pAltName, &cbDecoded)) {
        for (DWORD i = 0; i < pAltName->cAltEntry && count < max_count; i++) {
            if (pAltName->rgAltEntry[i].dwAltNameChoice == CERT_ALT_NAME_DNS_NAME && pAltName->rgAltEntry[i].pwszDNSName) {
                WideCharToMultiByte(CP_UTF8, 0, pAltName->rgAltEntry[i].pwszDNSName, -1, out_dns[count], 128, NULL, NULL);
                if (out_dns[count][0] != '\0') {
                    count++;
                }
            }
        }
    }

    free(pAltName);
    return count;
}

static bool cert_check_private_key(PCCERT_CONTEXT pCert) {
    if (!pCert) return false;
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE hKey = 0;
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
        if (bFreeKey && hKey) {
            NCryptFreeObject(hKey);
        }
        return true;
    }

    // Fallback check with standard flags
    if (CryptAcquireCertificatePrivateKey(
            pCert,
            CRYPT_ACQUIRE_SILENT_FLAG,
            NULL,
            &hKey,
            &dwKeySpec,
            &bFreeKey
        )) {
        if (bFreeKey && hKey) {
            if (dwKeySpec == CERT_NCRYPT_KEY_SPEC) {
                NCryptFreeObject(hKey);
            } else {
                CryptReleaseContext(hKey, 0);
            }
        }
        return true;
    }

    return false;
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
    cert_get_thumbprint_hex(pCert, details->thumbprint, sizeof(details->thumbprint));
    cert_get_email(pCert, details->email, sizeof(details->email));
    cert_get_cn(pCert, details->sn, sizeof(details->sn));
    cert_get_san_urn(pCert, details->urn, sizeof(details->urn));

    details->san_dns_count = cert_get_all_san_dns_names(pCert, details->san_dns_list, 8);
    if (details->san_dns_count > 0) {
        strncpy_s(details->san_dns, sizeof(details->san_dns), details->san_dns_list[0], _TRUNCATE);
    }

    // Determine canonical local hostname (prefer .device.leo4.ru or .local or .term.leo4.ru)
    if (details->san_dns[0] != '\0') {
        strncpy_s(details->local_hostname, sizeof(details->local_hostname), details->san_dns, _TRUNCATE);
    } else if (details->sn[0] != '\0') {
        snprintf(details->local_hostname, sizeof(details->local_hostname), "leo4-%s.local", details->sn);
    } else {
        strncpy_s(details->local_hostname, sizeof(details->local_hostname), "leo4-device.local", _TRUNCATE);
    }

    // Convert local_hostname to lower case
    for (char* p = details->local_hostname; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }

    details->ft_not_before = pCert->pCertInfo->NotBefore;
    details->ft_not_after = pCert->pCertInfo->NotAfter;

    filetime_to_iso(&details->ft_not_before, details->not_before, sizeof(details->not_before));
    filetime_to_iso(&details->ft_not_after, details->not_after, sizeof(details->not_after));

    details->has_private_key = cert_check_private_key(pCert);
    details->pCertContext = CertDuplicateCertificateContext(pCert);
}

void cert_store_free_details(CertDetails* details) {
    if (!details) return;
    if (details->pCertContext) {
        CertFreeCertificateContext(details->pCertContext);
        details->pCertContext = NULL;
    }
}

void cert_store_print_details(const CertDetails* details) {
    if (!details) return;
    printf("--- Terminal Certificate Details ---\n");
    printf("  Subject:        %s\n", details->subject);
    printf("  Device SN (CN): %s\n", details->sn);
    if (details->urn[0] != '\0') {
        printf("  SAN URN:        %s\n", details->urn);
    }
    if (details->san_dns[0] != '\0') {
        printf("  SAN DNS:        %s\n", details->san_dns);
    }
    printf("  Local Hostname: %s\n", details->local_hostname);
    printf("  Email:          %s\n", details->email);
    printf("  Issuer:         %s\n", details->issuer);
    printf("  Serial:         %s\n", details->serial);
    printf("  Thumbprint:     %s\n", details->thumbprint);
    printf("  Valid From:     %s\n", details->not_before);
    printf("  Valid To:       %s\n", details->not_after);
    printf("  Has PrivateKey: %s\n", details->has_private_key ? "YES" : "NO");
    printf("------------------------------------\n");
}

static int str_contains_case_insensitive(const char* haystack, const char* needle) {
    if (!haystack || !needle || !needle[0]) return 0;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (hlen < nlen) return 0;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        if (_strnicmp(haystack + i, needle, nlen) == 0) {
            return 1;
        }
    }
    return 0;
}

static int get_cert_tier(const char* email, const char* subject) {
    if (!email && !subject) return 0;
    // Tier 4 (Best): *.terminal@leo4.ru or contains .terminal@leo4.ru / terminal@leo4.ru
    if (str_contains_case_insensitive(email, ".terminal@leo4.ru") ||
        str_contains_case_insensitive(email, "terminal@leo4.ru") ||
        str_contains_case_insensitive(subject, ".terminal@leo4.ru") ||
        str_contains_case_insensitive(subject, "terminal@leo4.ru")) {
        return 4;
    }
    // Tier 3: *.terminal@forpay.ru or contains .terminal@forpay.ru / terminal@forpay.ru
    if (str_contains_case_insensitive(email, ".terminal@forpay.ru") ||
        str_contains_case_insensitive(email, "terminal@forpay.ru") ||
        str_contains_case_insensitive(subject, ".terminal@forpay.ru") ||
        str_contains_case_insensitive(subject, "terminal@forpay.ru")) {
        return 3;
    }
    // Tier 2: @leo4.ru
    if (str_contains_case_insensitive(email, "@leo4.ru") ||
        str_contains_case_insensitive(subject, "@leo4.ru")) {
        return 2;
    }
    // Tier 1: @forpay.ru
    if (str_contains_case_insensitive(email, "@forpay.ru") ||
        str_contains_case_insensitive(subject, "@forpay.ru")) {
        return 1;
    }
    return 0;
}

static bool search_in_single_store(HCERTSTORE hStore, const ProxyConfig* config, CertDetails* out_details) {
    if (!hStore || !out_details) return false;

    PCCERT_CONTEXT pBestCert = NULL;
    int bestTier = 0;
    FILETIME bestNotBefore = { 0, 0 };

    PCCERT_CONTEXT pCur = NULL;
    while ((pCur = CertEnumCertificatesInStore(hStore, pCur)) != NULL) {
        // 1. Must have accessible private key
        if (!cert_check_private_key(pCur)) {
            continue;
        }

        // 2. If drop_on_expire is enabled, ignore expired certificates
        if (config->drop_on_expire) {
            FILETIME ftNow;
            GetSystemTimeAsFileTime(&ftNow);
            if (CompareFileTime(&ftNow, &pCur->pCertInfo->NotAfter) > 0) {
                continue;
            }
        }

        char email[128] = { 0 };
        char thumbprint[64] = { 0 };
        char subject[512] = { 0 };

        cert_get_email(pCur, email, sizeof(email));
        cert_get_thumbprint_hex(pCur, thumbprint, sizeof(thumbprint));
        CertNameToStrA(
            X509_ASN_ENCODING,
            &pCur->pCertInfo->Subject,
            CERT_X500_NAME_STR,
            subject,
            sizeof(subject)
        );

        // Check exact thumbprint match if specified
        if (config->cert_thumbprint[0] != '\0') {
            if (_stricmp(thumbprint, config->cert_thumbprint) == 0) {
                if (pBestCert) CertFreeCertificateContext(pBestCert);
                pBestCert = CertDuplicateCertificateContext(pCur);
                bestTier = 10;
                break;
            }
            continue;
        }

        // Check custom email pattern if specified
        if (config->cert_email_pattern[0] != '\0') {
            const char* pattern = config->cert_email_pattern;
            if (pattern[0] == '*' && pattern[1] == '.') {
                pattern += 1;
            }
            if (str_contains_case_insensitive(email, pattern) ||
                str_contains_case_insensitive(subject, pattern)) {
                int isNewer = (pBestCert == NULL) || (CompareFileTime(&pCur->pCertInfo->NotBefore, &bestNotBefore) > 0);
                if (isNewer) {
                    if (pBestCert) CertFreeCertificateContext(pBestCert);
                    pBestCert = CertDuplicateCertificateContext(pCur);
                    bestTier = 9;
                    bestNotBefore = pCur->pCertInfo->NotBefore;
                }
            }
            continue;
        }

        // Tiered priority matching
        int currentTier = get_cert_tier(email, subject);
        if (currentTier > 0) {
            bool selectThis = false;
            if (currentTier > bestTier) {
                selectThis = true;
            } else if (currentTier == bestTier) {
                if (CompareFileTime(&pCur->pCertInfo->NotBefore, &bestNotBefore) > 0) {
                    selectThis = true;
                }
            }

            if (selectThis) {
                if (pBestCert) CertFreeCertificateContext(pBestCert);
                pBestCert = CertDuplicateCertificateContext(pCur);
                bestTier = currentTier;
                bestNotBefore = pCur->pCertInfo->NotBefore;
            }
        }
    }

    if (pBestCert) {
        fill_cert_details(pBestCert, out_details);
        CertFreeCertificateContext(pBestCert);
        return true;
    }

    return false;
}

bool cert_store_find_best_cert(const ProxyConfig* config, CertDetails* out_details) {
    if (!config || !out_details) return false;
    memset(out_details, 0, sizeof(CertDetails));

    const WCHAR* storeName = L"MY";
    DWORD storeFlags = CERT_STORE_READONLY_FLAG;

    if (config->is_machine_store) {
        storeFlags |= CERT_SYSTEM_STORE_LOCAL_MACHINE;
    } else {
        storeFlags |= CERT_SYSTEM_STORE_CURRENT_USER;
    }

    HCERTSTORE hStore = CertOpenStore(
        CERT_STORE_PROV_SYSTEM_W,
        0,
        0,
        storeFlags,
        storeName
    );

    bool found = false;
    if (hStore) {
        found = search_in_single_store(hStore, config, out_details);
        CertCloseStore(hStore, 0);
    }

    // Fallback: If not found in LocalMachine and store wasn't explicitly user-only, try CurrentUser
    if (!found && config->is_machine_store) {
        hStore = CertOpenStore(
            CERT_STORE_PROV_SYSTEM_W,
            0,
            0,
            CERT_STORE_READONLY_FLAG | CERT_SYSTEM_STORE_CURRENT_USER,
            storeName
        );
        if (hStore) {
            found = search_in_single_store(hStore, config, out_details);
            CertCloseStore(hStore, 0);
        }
    }

    return found;
}

bool cert_store_get_active_sn(char* out_sn, size_t out_sn_size) {
    if (!out_sn || out_sn_size == 0) return false;
    out_sn[0] = '\0';

    ProxyConfig config;
    proxy_config_init_defaults(&config);

    CertDetails details;
    if (cert_store_find_best_cert(&config, &details)) {
        if (details.sn[0] != '\0') {
            strncpy_s(out_sn, out_sn_size, details.sn, _TRUNCATE);
            cert_store_free_details(&details);
            return true;
        }
        cert_store_free_details(&details);
    }

    return false;
}

bool cert_store_is_cert_expired(const CertDetails* details) {
    if (!details || details->pCertContext == NULL) return true;
    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);
    return CompareFileTime(&ftNow, &details->ft_not_after) > 0;
}
