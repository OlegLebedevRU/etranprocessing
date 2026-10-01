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
static HFONT force_font;
static bool force_checked;
static bool status_error;

static void set_status(HWND dialog, const wchar_t* text, bool error) {
    status_error = error;
    SetDlgItemTextW(dialog, IDC_STATUS, text);
    InvalidateRect(GetDlgItem(dialog, IDC_STATUS), NULL, TRUE);
}

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
            status_error = false;
            last_result = 31; // Closing without enrollment is cancellation, never success.
            force_checked = false;
            HDC screen_dc = GetDC(dialog);
            int font_height = screen_dc ? MulDiv(16, GetDeviceCaps(screen_dc, LOGPIXELSY), 72) : 24;
            if (screen_dc) ReleaseDC(dialog, screen_dc);
            force_font = CreateFontW(-font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            if (force_font) SendDlgItemMessageW(dialog, IDC_FORCE, WM_SETFONT,
                                                (WPARAM)force_font, TRUE);
            SendDlgItemMessageW(dialog, IDC_PIN_EDIT, EM_LIMITTEXT, 16, 0);
            refresh_certificates(dialog);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDC_FORCE:
                    if (!busy) {
                        force_checked = !force_checked;
                        InvalidateRect(GetDlgItem(dialog, IDC_FORCE), NULL, TRUE);
                    }
                    return TRUE;
                case IDC_REFRESH:
                    if (!busy) refresh_certificates(dialog);
                    return TRUE;
                case IDC_INSTALL: {
                    if (busy) return TRUE;
                    wchar_t pin_w[32] = { 0 };
                    GetDlgItemTextW(dialog, IDC_PIN_EDIT, pin_w, 32);
                    if (wcslen(pin_w) != 6) {
                        set_status(dialog, L"Enter a six-character PIN.", true);
                        SecureZeroMemory(pin_w, sizeof(pin_w));
                        return TRUE;
                    }
                    bool force = force_checked;
                    if (!refresh_certificates(dialog)) {
                        set_status(dialog, L"Certificate store cannot be checked.", true);
                        SecureZeroMemory(pin_w, sizeof(pin_w));
                        return TRUE;
                    }
                    cert_info info = { 0 };
                    cert_state state = cert_discover(NULL, &info);
                    if (state >= CERT_STORE_ERROR) {
                        set_status(dialog, L"Certificate store cannot be checked.", true);
                        SecureZeroMemory(pin_w, sizeof(pin_w));
                        return TRUE;
                    }
                    if (!force && long_lived_cert_found) {
                        set_status(dialog,
                            L"Existing certificate has more than 30 days left. Replacement is blocked.", true);
                        SecureZeroMemory(pin_w, sizeof(pin_w));
                        return TRUE;
                    }
                    if (MessageBoxW(dialog,
                            L"After the new certificate and private key are verified, previous terminal certificates (iot.leo4.ru and legacy certsrv) will be removed from Machine MY and MY of all Windows profiles. Continue?",
                            L"Confirm certificate replacement", MB_YESNO | MB_ICONWARNING) != IDYES) {
                        SecureZeroMemory(pin_w, sizeof(pin_w));
                        return TRUE;
                    }
                    pending_force = force;
                    WideCharToMultiByte(CP_UTF8, 0, pin_w, -1, pending_pin, sizeof(pending_pin), NULL, NULL);
                    SecureZeroMemory(pin_w, sizeof(pin_w));
                    SetDlgItemTextW(dialog, IDC_PIN_EDIT, L"");
                    EnableWindow(GetDlgItem(dialog, IDC_INSTALL), FALSE);
                    EnableWindow(GetDlgItem(dialog, IDCANCEL), FALSE);
                    set_status(dialog, L"Issuing and verifying certificate...", false);
                    busy = true;
                    HANDLE worker = CreateThread(NULL, 0, enroll_worker, dialog, 0, NULL);
                    if (worker) CloseHandle(worker);
                    else {
                        busy = false;
                        SecureZeroMemory(pending_pin, sizeof(pending_pin));
                        EnableWindow(GetDlgItem(dialog, IDC_INSTALL), TRUE);
                        EnableWindow(GetDlgItem(dialog, IDCANCEL), TRUE);
                        set_status(dialog, L"Cannot start enrollment worker.", true);
                    }
                    return TRUE;
                }
                case IDCANCEL:
                    if (!busy) EndDialog(dialog, last_result);
                    return TRUE;
            }
            break;
        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* item = (const DRAWITEMSTRUCT*)lparam;
            if (!item || item->CtlID != IDC_FORCE) break;
            FillRect(item->hDC, &item->rcItem, GetSysColorBrush(COLOR_3DFACE));
            int side = item->rcItem.bottom - item->rcItem.top - 6;
            if (side > 30) side = 30;
            RECT box = { item->rcItem.left + 3,
                         item->rcItem.top + (item->rcItem.bottom - item->rcItem.top - side) / 2,
                         item->rcItem.left + 3 + side,
                         item->rcItem.top + (item->rcItem.bottom - item->rcItem.top + side) / 2 };
            DrawFrameControl(item->hDC, &box, DFC_BUTTON,
                             DFCS_BUTTONCHECK | (force_checked ? DFCS_CHECKED : 0));
            RECT label = item->rcItem;
            label.left = box.right + 9;
            HFONT old_font = force_font ? (HFONT)SelectObject(item->hDC, force_font) : NULL;
            SetBkMode(item->hDC, TRANSPARENT);
            DrawTextW(item->hDC, L"Force reissue", -1, &label, DT_VCENTER | DT_SINGLELINE);
            if (old_font) SelectObject(item->hDC, old_font);
            if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
            return TRUE;
        }
        case WM_CTLCOLORSTATIC: {
            if (GetDlgCtrlID((HWND)lparam) != IDC_STATUS) break;
            HIGHCONTRASTW contrast = { sizeof(contrast) };
            SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
            SetTextColor((HDC)wparam, (contrast.dwFlags & HCF_HIGHCONTRASTON)
                ? GetSysColor(COLOR_WINDOWTEXT) : status_error ? RGB(170, 32, 32) : RGB(30, 70, 110));
            SetBkMode((HDC)wparam, TRANSPARENT);
            return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
        }
        case WM_ENROLL_DONE:
            busy = false;
            last_result = (int)wparam;
            refresh_certificates(dialog);
            EnableWindow(GetDlgItem(dialog, IDC_INSTALL), TRUE);
            EnableWindow(GetDlgItem(dialog, IDCANCEL), TRUE);
            if (last_result == 0) {
                set_status(dialog, L"Certificate installed. Services must switch to the new identity.", false);
            } else {
                wchar_t result[256];
                const wchar_t* action = L"Review certificate store and retry safely.";
                if (last_result == 2 || last_result == 4) action = L"Check PIN and CA connectivity before retrying.";
                else if (last_result == 3) action = L"Check CNG key store permissions.";
                else if (last_result == 5) action = L"A new certificate may already be installed. Review profile cleanup errors before using another PIN.";
                else if (last_result == 20) action = L"Administrator access is required.";
                swprintf_s(result, sizeof(result) / sizeof(result[0]),
                    L"Enrollment failed (code %d). %ls", last_result, action);
                set_status(dialog, result, true);
            }
            return TRUE;
        case WM_CLOSE:
            if (!busy) EndDialog(dialog, last_result);
            return TRUE;
        case WM_DESTROY:
            if (force_font) {
                DeleteObject(force_font);
                force_font = NULL;
            }
            return TRUE;
    }
    return FALSE;
}

int l4pin_show_gui(void) {
    return (int)DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_PIN_GUI),
                                NULL, gui_proc, 0);
}
