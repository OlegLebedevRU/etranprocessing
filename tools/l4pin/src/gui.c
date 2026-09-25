#include "gui.h"
#include "cert_discovery.h"
#include "cert_store.h"
#include "../res/resource.h"
#include <windows.h>
#include <wincrypt.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define WM_ENROLL_DONE (WM_APP + 10)

static char pending_pin[32];
static bool pending_force;
static bool busy;
static int last_result;

static bool long_lived_cert_found;

static bool refresh_certificates(HWND dialog) {
    HWND list = GetDlgItem(dialog, IDC_CERT_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    long_lived_cert_found = false;
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
        CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG, L"MY");
    if (!store) {
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"Cannot read LocalMachine\\MY");
        return false;
    }
    FILETIME now = { 0 };
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER threshold = { 0 };
    threshold.LowPart = now.dwLowDateTime;
    threshold.HighPart = now.dwHighDateTime;
    threshold.QuadPart += 30ULL * 864000000000ULL;
    FILETIME limit = { threshold.LowPart, threshold.HighPart };
    int count = 0;
    PCCERT_CONTEXT cert = NULL;
    while ((cert = CertEnumCertificatesInStore(store, cert)) != NULL) {
        if (!cert_is_leo4_issuer(cert)) continue;
        if (CompareFileTime(&cert->pCertInfo->NotAfter, &limit) > 0) long_lived_cert_found = true;
        wchar_t subject[128] = { 0 };
        wchar_t row[360] = { 0 };
        SYSTEMTIME expiry = { 0 };
        char thumbprint[41] = { 0 };
        cert_get_thumbprint_hex(cert, thumbprint, sizeof(thumbprint));
        CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0, szOID_COMMON_NAME,
            subject, sizeof(subject) / sizeof(subject[0]));
        FileTimeToSystemTime(&cert->pCertInfo->NotAfter, &expiry);
        swprintf_s(row, sizeof(row) / sizeof(row[0]),
            L"CN=%ls    valid until %04u-%02u-%02u %02u:%02u UTC    SHA1=%hs",
            subject, expiry.wYear, expiry.wMonth, expiry.wDay, expiry.wHour, expiry.wMinute,
            thumbprint[0] ? thumbprint : "unavailable");
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)row);
        count++;
    }
    CertCloseStore(store, 0);
    if (!count) SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"No terminal certificates found");
    return true;
}

static DWORD WINAPI enroll_worker(void* param) {
    HWND dialog = (HWND)param;
    char* args[4] = { "l4pin", "--pin", pending_pin, "--force" };
    int result = l4pin_run_cli(pending_force ? 4 : 3, args);
    SecureZeroMemory(pending_pin, sizeof(pending_pin));
    PostMessageW(dialog, WM_ENROLL_DONE, (WPARAM)result, 0);
    return 0;
}

static INT_PTR CALLBACK gui_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    UNREFERENCED_PARAMETER(lparam);
    switch (message) {
        case WM_INITDIALOG:
            busy = false;
            last_result = 0;
            CheckDlgButton(dialog, IDC_DELETE_OLD, BST_CHECKED);
            SendDlgItemMessageW(dialog, IDC_PIN_EDIT, EM_LIMITTEXT, 16, 0);
            refresh_certificates(dialog);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDC_REFRESH:
                    if (!busy) refresh_certificates(dialog);
                    return TRUE;
                case IDC_INSTALL: {
                    if (busy) return TRUE;
                    wchar_t pin_w[32] = { 0 };
                    GetDlgItemTextW(dialog, IDC_PIN_EDIT, pin_w, 32);
                    if (wcslen(pin_w) != 6) {
                        SetDlgItemTextW(dialog, IDC_STATUS, L"Enter a six-character PIN.");
                        SecureZeroMemory(pin_w, sizeof(pin_w));
                        return TRUE;
                    }
                    bool delete_old = IsDlgButtonChecked(dialog, IDC_DELETE_OLD) == BST_CHECKED;
                    bool force = IsDlgButtonChecked(dialog, IDC_FORCE) == BST_CHECKED;
                    if (!refresh_certificates(dialog)) {
                        SetDlgItemTextW(dialog, IDC_STATUS, L"Certificate store cannot be checked.");
                        SecureZeroMemory(pin_w, sizeof(pin_w));
                        return TRUE;
                    }
                    cert_info info = { 0 };
                    cert_state state = cert_discover(NULL, &info);
                    if (state >= CERT_STORE_ERROR) {
                        SetDlgItemTextW(dialog, IDC_STATUS, L"Certificate store cannot be checked.");
                        SecureZeroMemory(pin_w, sizeof(pin_w));
                        return TRUE;
                    }
                    if (!delete_old && !force && long_lived_cert_found) {
                        SetDlgItemTextW(dialog, IDC_STATUS,
                            L"Existing certificate has more than 30 days left. Replacement is blocked.");
                        SecureZeroMemory(pin_w, sizeof(pin_w));
                        return TRUE;
                    }
                    if (MessageBoxW(dialog,
                            L"After the new certificate and key are verified, matching old certificates will be removed. This also applies when 'Delete old' is unchecked and replacement is allowed. Continue?",
                            L"Confirm certificate replacement", MB_YESNO | MB_ICONWARNING) != IDYES) {
                        SecureZeroMemory(pin_w, sizeof(pin_w));
                        return TRUE;
                    }
                    pending_force = force || delete_old;
                    WideCharToMultiByte(CP_UTF8, 0, pin_w, -1, pending_pin, sizeof(pending_pin), NULL, NULL);
                    SecureZeroMemory(pin_w, sizeof(pin_w));
                    SetDlgItemTextW(dialog, IDC_PIN_EDIT, L"");
                    EnableWindow(GetDlgItem(dialog, IDC_INSTALL), FALSE);
                    EnableWindow(GetDlgItem(dialog, IDCANCEL), FALSE);
                    SetDlgItemTextW(dialog, IDC_STATUS, L"Issuing and verifying certificate...");
                    busy = true;
                    HANDLE worker = CreateThread(NULL, 0, enroll_worker, dialog, 0, NULL);
                    if (worker) CloseHandle(worker);
                    else {
                        busy = false;
                        SecureZeroMemory(pending_pin, sizeof(pending_pin));
                        EnableWindow(GetDlgItem(dialog, IDC_INSTALL), TRUE);
                        EnableWindow(GetDlgItem(dialog, IDCANCEL), TRUE);
                        SetDlgItemTextW(dialog, IDC_STATUS, L"Cannot start enrollment worker.");
                    }
                    return TRUE;
                }
                case IDCANCEL:
                    if (!busy) EndDialog(dialog, last_result);
                    return TRUE;
            }
            break;
        case WM_ENROLL_DONE:
            busy = false;
            last_result = (int)wparam;
            refresh_certificates(dialog);
            EnableWindow(GetDlgItem(dialog, IDC_INSTALL), TRUE);
            EnableWindow(GetDlgItem(dialog, IDCANCEL), TRUE);
            if (last_result == 0) {
                SetDlgItemTextW(dialog, IDC_STATUS, L"Certificate installed and list refreshed.");
            } else {
                wchar_t result[256];
                const wchar_t* action = L"Review certificate store and retry safely.";
                if (last_result == 2 || last_result == 4) action = L"Check PIN and CA connectivity before retrying.";
                else if (last_result == 3) action = L"Check CNG key store permissions.";
                else if (last_result == 20) action = L"Administrator access is required.";
                swprintf_s(result, sizeof(result) / sizeof(result[0]),
                    L"Enrollment failed (code %d). %ls", last_result, action);
                SetDlgItemTextW(dialog, IDC_STATUS, result);
            }
            return TRUE;
        case WM_CLOSE:
            if (!busy) EndDialog(dialog, last_result);
            return TRUE;
    }
    return FALSE;
}

int l4pin_show_gui(void) {
    return (int)DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_PIN_GUI),
                                NULL, gui_proc, 0);
}
