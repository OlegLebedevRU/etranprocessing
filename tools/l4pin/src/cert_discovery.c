#include "cert_discovery.h"

#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")
#pragma comment(lib, "advapi32.lib")

static const char* stristr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    size_t needle_len = strlen(needle);
    for (; *haystack; haystack++) {
        if (_strnicmp(haystack, needle, needle_len) == 0) {
            return haystack;
        }
    }
    return NULL;
}

static bool discovery_get_cn(PCCERT_CONTEXT pCert, bool is_issuer, char* out_cn, size_t out_cn_size) {
    if (!pCert || !out_cn || out_cn_size == 0) return false;
    out_cn[0] = '\0';

    DWORD flags = is_issuer ? CERT_NAME_ISSUER_FLAG : 0;
    DWORD len = CertGetNameStringA(
        pCert,
        CERT_NAME_ATTR_TYPE,
        flags,
        szOID_COMMON_NAME,
        out_cn,
        (DWORD)out_cn_size
    );
    if (len > 1 && out_cn[0] != '\0') {
        return true;
    }

    // Fallback: parse CN= from distinguished name
    CERT_NAME_BLOB* pBlob = is_issuer ? &pCert->pCertInfo->Issuer : &pCert->pCertInfo->Subject;
    char szDn[1024] = { 0 };
    len = CertNameToStrA(
        X509_ASN_ENCODING,
        pBlob,
        CERT_X500_NAME_STR,
        szDn,
        sizeof(szDn)
    );
    if (len > 1) {
        const char* p = strstr(szDn, "CN=");
        if (!p) p = strstr(szDn, "cn=");
        if (p) {
            p += 3;
            size_t idx = 0;
            while (*p && *p != ',' && *p != ';' && idx + 1 < out_cn_size) {
                out_cn[idx++] = *p++;
            }
            out_cn[idx] = '\0';
            return (idx > 0);
        }
    }
    return false;
}

static bool discovery_is_issuer_leo4(PCCERT_CONTEXT pCert, char* out_issuer_cn, size_t out_size) {
    char issuer_cn[128] = { 0 };
    discovery_get_cn(pCert, true, issuer_cn, sizeof(issuer_cn));

    char szIssuerDn[1024] = { 0 };
    CertNameToStrA(
        X509_ASN_ENCODING,
        &pCert->pCertInfo->Issuer,
        CERT_X500_NAME_STR,
        szIssuerDn,
        sizeof(szIssuerDn)
    );

    bool matched = false;
    if (stristr(issuer_cn, "iot.leo4.ru") != NULL || stristr(szIssuerDn, "iot.leo4.ru") != NULL) {
        matched = true;
    }

    if (matched && out_issuer_cn && out_size > 0) {
        if (issuer_cn[0] != '\0') {
            strncpy_s(out_issuer_cn, out_size, issuer_cn, _TRUNCATE);
        } else {
            strncpy_s(out_issuer_cn, out_size, "iot.leo4.ru", _TRUNCATE);
        }
    }
    return matched;
}

bool cert_is_leo4_issuer(PCCERT_CONTEXT cert) {
    return cert && discovery_is_issuer_leo4(cert, NULL, 0);
}

static void discovery_filetime_to_str(const FILETIME* ft, char* out_str, size_t out_str_size) {
    if (!ft || !out_str || out_str_size == 0) return;
    SYSTEMTIME st;
    if (FileTimeToSystemTime(ft, &st)) {
        snprintf(out_str, out_str_size, "%04u-%02u-%02u %02u:%02u:%02u UTC",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    } else {
        out_str[0] = '\0';
    }
}

static bool discovery_get_thumbprint_hex(PCCERT_CONTEXT pCert, char* out_hex, size_t out_size) {
    if (!pCert || !out_hex || out_size < 41) return false;
    BYTE hash[20] = { 0 };
    DWORD cbHash = sizeof(hash);
    if (!CertGetCertificateContextProperty(pCert, CERT_SHA1_HASH_PROP_ID, hash, &cbHash)) {
        return false;
    }
    for (DWORD i = 0; i < cbHash && (i * 2 + 2) < out_size; i++) {
        snprintf(out_hex + (i * 2), 3, "%02X", hash[i]);
    }
    out_hex[cbHash * 2] = '\0';
    return true;
}

static bool discovery_has_cng_private_key(PCCERT_CONTEXT pCert) {
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE hKey = 0;
    DWORD dwKeySpec = 0;
    BOOL bCallerFree = FALSE;
    BOOL ok = CryptAcquireCertificatePrivateKey(
        pCert,
        CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_CACHE_FLAG,
        NULL,
        &hKey,
        &dwKeySpec,
        &bCallerFree
    );
    if (ok) {
        if (bCallerFree && hKey) {
            NCryptFreeObject(hKey);
        }
        return true;
    }
    return false;
}

static bool discovery_get_subject_cn(PCCERT_CONTEXT pCert, char* out_sn, size_t out_size) {
    return discovery_get_cn(pCert, false, out_sn, out_size);
}

static bool discovery_sn_matches(const char* actual_sn, const wchar_t* expected_sn) {
    if (!expected_sn || expected_sn[0] == L'\0') {
        return true;
    }
    wchar_t w_actual[128] = { 0 };
    MultiByteToWideChar(CP_UTF8, 0, actual_sn, -1, w_actual, sizeof(w_actual) / sizeof(wchar_t));
    return (_wcsicmp(w_actual, expected_sn) == 0);
}

cert_state cert_discover_in_store(HCERTSTORE hStore, const wchar_t *expected_sn, cert_info *out) {
    if (!hStore) {
        if (out) memset(out, 0, sizeof(*out));
        return CERT_STORE_ERROR;
    }

    if (out) {
        memset(out, 0, sizeof(*out));
    }

    FILETIME now_ft;
    GetSystemTimeAsFileTime(&now_ft);
    ULARGE_INTEGER ul_now;
    ul_now.LowPart = now_ft.dwLowDateTime;
    ul_now.HighPart = now_ft.dwHighDateTime;

    bool found_issuer_cert = false;
    int candidate_count = 0;
    PCCERT_CONTEXT pBestCandidate = NULL;
    FILETIME best_candidate_not_after = { 0, 0 };

    PCCERT_CONTEXT pBestBroken = NULL;
    FILETIME best_broken_not_after = { 0, 0 };
    bool best_broken_sn_matched = false;
    bool best_broken_has_key = false;

    PCCERT_CONTEXT pCert = NULL;
    while ((pCert = CertEnumCertificatesInStore(hStore, pCert)) != NULL) {
        char issuer_cn[64] = { 0 };
        if (!discovery_is_issuer_leo4(pCert, issuer_cn, sizeof(issuer_cn))) {
            continue;
        }

        found_issuer_cert = true;

        char sn[64] = { 0 };
        discovery_get_subject_cn(pCert, sn, sizeof(sn));

        bool sn_matches = discovery_sn_matches(sn, expected_sn);
        bool has_key = discovery_has_cng_private_key(pCert);
        bool not_after_valid = (CompareFileTime(&pCert->pCertInfo->NotAfter, &now_ft) > 0);

        if (has_key && not_after_valid && sn_matches) {
            candidate_count++;
            if (pBestCandidate == NULL ||
                CompareFileTime(&pCert->pCertInfo->NotAfter, &best_candidate_not_after) > 0) {
                if (pBestCandidate) {
                    CertFreeCertificateContext(pBestCandidate);
                }
                pBestCandidate = CertDuplicateCertificateContext(pCert);
                best_candidate_not_after = pCert->pCertInfo->NotAfter;
            }
        } else {
            // Broken candidate
            bool replace_broken = false;
            if (pBestBroken == NULL) {
                replace_broken = true;
            } else if (sn_matches && !best_broken_sn_matched) {
                // Prioritize broken cert matching expected SN
                replace_broken = true;
            } else if (sn_matches == best_broken_sn_matched &&
                       CompareFileTime(&pCert->pCertInfo->NotAfter, &best_broken_not_after) > 0) {
                replace_broken = true;
            }

            if (replace_broken) {
                if (pBestBroken) {
                    CertFreeCertificateContext(pBestBroken);
                }
                pBestBroken = CertDuplicateCertificateContext(pCert);
                best_broken_not_after = pCert->pCertInfo->NotAfter;
                best_broken_sn_matched = sn_matches;
                best_broken_has_key = has_key;
            }
        }
    }

    if (candidate_count > 0 && pBestCandidate != NULL) {
        if (pBestBroken) {
            CertFreeCertificateContext(pBestBroken);
            pBestBroken = NULL;
        }

        ULARGE_INTEGER ul_not_after;
        ul_not_after.LowPart = pBestCandidate->pCertInfo->NotAfter.dwLowDateTime;
        ul_not_after.HighPart = pBestCandidate->pCertInfo->NotAfter.dwHighDateTime;

        int days_left = 0;
        if (ul_not_after.QuadPart > ul_now.QuadPart) {
            days_left = (int)((ul_not_after.QuadPart - ul_now.QuadPart) / 864000000000ULL);
        }

        cert_state state = (days_left > 30) ? CERT_VALID : CERT_EXPIRING;

        if (out) {
            discovery_get_thumbprint_hex(pBestCandidate, out->thumbprint_hex, sizeof(out->thumbprint_hex));
            discovery_get_subject_cn(pBestCandidate, out->sn, sizeof(out->sn));
            discovery_filetime_to_str(&pBestCandidate->pCertInfo->NotAfter, out->not_after_utc, sizeof(out->not_after_utc));
            discovery_is_issuer_leo4(pBestCandidate, out->issuer_cn, sizeof(out->issuer_cn));
            out->has_private_key = true;
            out->days_left = days_left;
            out->cert_duplicates = candidate_count - 1;
        }

        CertFreeCertificateContext(pBestCandidate);
        return state;
    }

    if (pBestCandidate) {
        CertFreeCertificateContext(pBestCandidate);
        pBestCandidate = NULL;
    }

    if (found_issuer_cert) {
        if (out && pBestBroken) {
            discovery_get_thumbprint_hex(pBestBroken, out->thumbprint_hex, sizeof(out->thumbprint_hex));
            discovery_get_subject_cn(pBestBroken, out->sn, sizeof(out->sn));
            discovery_filetime_to_str(&pBestBroken->pCertInfo->NotAfter, out->not_after_utc, sizeof(out->not_after_utc));
            discovery_is_issuer_leo4(pBestBroken, out->issuer_cn, sizeof(out->issuer_cn));
            out->has_private_key = best_broken_has_key;

            ULARGE_INTEGER ul_not_after;
            ul_not_after.LowPart = pBestBroken->pCertInfo->NotAfter.dwLowDateTime;
            ul_not_after.HighPart = pBestBroken->pCertInfo->NotAfter.dwHighDateTime;
            if (ul_not_after.QuadPart > ul_now.QuadPart) {
                out->days_left = (int)((ul_not_after.QuadPart - ul_now.QuadPart) / 864000000000ULL);
            } else {
                out->days_left = 0;
            }
            out->cert_duplicates = 0;
        }
        if (pBestBroken) {
            CertFreeCertificateContext(pBestBroken);
            pBestBroken = NULL;
        }
        return CERT_BROKEN;
    }

    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return CERT_ABSENT;
}

cert_state cert_discover(const wchar_t *expected_sn, cert_info *out) {
    HCERTSTORE hStore = CertOpenStore(
        CERT_STORE_PROV_SYSTEM_W,
        0,
        0,
        CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG,
        L"MY"
    );
    if (!hStore) {
        if (out) {
            memset(out, 0, sizeof(*out));
        }
        return CERT_STORE_ERROR;
    }

    cert_state st = cert_discover_in_store(hStore, expected_sn, out);
    CertCloseStore(hStore, 0);
    return st;
}

const char* cert_state_to_str(cert_state state) {
    switch (state) {
        case CERT_VALID: return "valid";
        case CERT_EXPIRING: return "expiring";
        case CERT_BROKEN: return "broken";
        case CERT_ABSENT: return "absent";
        default: return "error";
    }
}
