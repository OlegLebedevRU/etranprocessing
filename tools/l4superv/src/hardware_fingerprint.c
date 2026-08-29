#include "hardware_fingerprint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wincrypt.h>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

bool hw_get_fingerprint(char* out_fp, size_t out_size) {
    if (!out_fp || out_size < 32) return false;
    out_fp[0] = '\0';

    char guid_str[64] = { 0 };
    HKEY hKey = NULL;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        DWORD dwType = REG_SZ;
        DWORD dwSize = sizeof(guid_str) - 1;
        if (RegQueryValueExA(hKey, "MachineGuid", NULL, &dwType, (LPBYTE)guid_str, &dwSize) == ERROR_SUCCESS) {
            guid_str[dwSize] = '\0';
        }
        RegCloseKey(hKey);
    }

    if (guid_str[0] == '\0') {
        // Fallback: try 32-bit registry view
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD dwType = REG_SZ;
            DWORD dwSize = sizeof(guid_str) - 1;
            if (RegQueryValueExA(hKey, "MachineGuid", NULL, &dwType, (LPBYTE)guid_str, &dwSize) == ERROR_SUCCESS) {
                guid_str[dwSize] = '\0';
            }
            RegCloseKey(hKey);
        }
    }

    DWORD vol_serial = 0;
    GetVolumeInformationW(L"C:\\", NULL, 0, &vol_serial, NULL, NULL, NULL, 0);

    if (guid_str[0] != '\0') {
        snprintf(out_fp, out_size, "%s:%08X", guid_str, vol_serial);
    } else {
        snprintf(out_fp, out_size, "VOL-%08X", vol_serial);
    }

    return true;
}

bool hw_clean_terminal_certificates(void) {
    HCERTSTORE hStore = CertOpenStore(
        CERT_STORE_PROV_SYSTEM_W,
        0,
        0,
        CERT_SYSTEM_STORE_LOCAL_MACHINE,
        L"MY"
    );

    if (!hStore) {
        return false;
    }

    PCCERT_CONTEXT pCert = NULL;
    int deleted_count = 0;

    // Loop through all certificates
    while ((pCert = CertEnumCertificatesInStore(hStore, pCert)) != NULL) {
        char subject[512] = { 0 };
        CertGetNameStringA(pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, subject, sizeof(subject));
        
        char email[256] = { 0 };
        CertGetNameStringA(pCert, CERT_NAME_EMAIL_TYPE, 0, NULL, email, sizeof(email));

        // Check if certificate belongs to terminal (*.terminal@leo4.ru or *.terminal@forpay.ru)
        bool is_terminal_cert = false;
        if (strstr(subject, "leo4.ru") || strstr(subject, "forpay.ru") ||
            strstr(email, "leo4.ru") || strstr(email, "forpay.ru")) {
            is_terminal_cert = true;
        }

        if (is_terminal_cert) {
            PCCERT_CONTEXT pDup = CertDuplicateCertificateContext(pCert);
            if (CertDeleteCertificateFromStore(pCert)) {
                deleted_count++;
                pCert = NULL; // CertDeleteCertificateFromStore frees pCert
            } else if (pDup) {
                CertFreeCertificateContext(pDup);
            }
        }
    }

    CertCloseStore(hStore, 0);
    return (deleted_count > 0);
}
