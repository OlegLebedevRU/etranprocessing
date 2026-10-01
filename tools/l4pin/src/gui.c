#include "gui.h"
#include "cert_discovery.h"
#include "cert_store.h"
#include "../res/resource.h"
#include <windows.h>
#include <wincrypt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WM_ENROLL_DONE (WM_APP + 10)

static char pending_pin[32];
static bool pending_force;
static bool busy;
static int last_result;
static HFONT force_font;
static HFONT normal_font;
static HFONT pin_font;
static HFONT identity_font;
static int ui_dpi;
static bool force_checked;
static bool status_error;

typedef struct {
    wchar_t identity[320];
    wchar_t subject[320];
    wchar_t details[320];
} CertRow;

static int px(int value) { return MulDiv(value, ui_dpi, 96); }

static bool terminal_number(const wchar_t* cn, wchar_t number[8]) {
    if (wcslen(cn) != 23 || wcsncmp(cn, L"a4b", 3) || cn[10] != L'c' || cn[16] != L'd') return false;
    for (int i = 3; i < 23; ++i) {
        if (i == 10 || i == 16) continue;
        if (cn[i] < L'0' || cn[i] > L'9') return false;
    }
    memcpy(number, cn + 3, 7 * sizeof(wchar_t));
    number[7] = L'\0';
    return true;
}

static void position_control(HWND dialog, int id, int x, int y, int width, int height) {
    MoveWindow(GetDlgItem(dialog, id), x, y, width, height, TRUE);
}

static void layout(HWND dialog) {
    RECT client;
    GetClientRect(dialog, &client);
    int margin = px(16), gap = px(8), width = client.right - 2 * margin;
    int footer = client.bottom - margin - px(44);
    int status = footer - gap - px(40);
    int note = status - gap - px(34);
    int force = note - gap - px(48);
    int pin = force - px(12) - px(52);
    int label = pin - gap - px(22);
    position_control(dialog, IDC_CERT_HEADING, margin, px(12), width, px(24));
    position_control(dialog, IDC_CERT_LIST, margin, px(44), width, label - gap - px(44));
    position_control(dialog, IDC_PIN_LABEL, margin, label, width, px(22));
    position_control(dialog, IDC_PIN_EDIT, margin, pin, px(240), px(52));
    position_control(dialog, IDC_REFRESH, client.right - margin - px(140), pin, px(140), px(44));
    position_control(dialog, IDC_FORCE, margin, force, width, px(48));
    position_control(dialog, IDC_CLEANUP_NOTE, margin, note, width, px(34));
    position_control(dialog, IDC_STATUS, margin, status, width, px(40));
    position_control(dialog, IDC_INSTALL, client.right - margin - px(304), footer, px(200), px(44));
    position_control(dialog, IDCANCEL, client.right - margin - px(96), footer, px(96), px(44));
}

static HFONT make_font(int points, int weight) {
    return CreateFontW(-MulDiv(points, ui_dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
}

static void initialize_layout(HWND dialog) {
    HDC dc = GetDC(dialog);
    ui_dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(dialog, dc);
    MONITORINFO monitor = { sizeof(monitor) };
    GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &monitor);
    int work_width = monitor.rcWork.right - monitor.rcWork.left;
    int work_height = monitor.rcWork.bottom - monitor.rcWork.top;
    // Compact scaling keeps the touch controls accessible on an 800x600 display.
    int fit_x = MulDiv(work_width - 24, 96, 760);
    int fit_y = MulDiv(work_height - 24, 96, 570);
    if (ui_dpi > fit_x) ui_dpi = fit_x;
    if (ui_dpi > fit_y) ui_dpi = fit_y;
    if (ui_dpi < 72) ui_dpi = 72;
    normal_font = make_font(11, FW_NORMAL);
    identity_font = make_font(13, FW_BOLD);
    force_font = make_font(16, FW_NORMAL);
    pin_font = make_font(26, FW_SEMIBOLD);
    for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SendMessageW(child, WM_SETFONT, (WPARAM)normal_font, TRUE);
    SendDlgItemMessageW(dialog, IDC_PIN_EDIT, WM_SETFONT, (WPARAM)pin_font, TRUE);
    SendDlgItemMessageW(dialog, IDC_FORCE, WM_SETFONT, (WPARAM)force_font, TRUE);
    SendDlgItemMessageW(dialog, IDC_CERT_LIST, LB_SETITEMHEIGHT, 0, px(76));
    int width = px(760), height = px(570);
    SetWindowPos(dialog, NULL, monitor.rcWork.left + (work_width - width) / 2,
        monitor.rcWork.top + (work_height - height) / 2, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    layout(dialog);
}

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
        wchar_t ou[128] = { 0 }, org[128] = { 0 }, number[8] = { 0 };
        SYSTEMTIME expiry = { 0 };
        char thumbprint[41] = { 0 };
        cert_get_thumbprint_hex(cert, thumbprint, sizeof(thumbprint));
        CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0, szOID_COMMON_NAME,
            subject, sizeof(subject) / sizeof(subject[0]));
        CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0, szOID_ORGANIZATIONAL_UNIT_NAME,
            ou, sizeof(ou) / sizeof(ou[0]));
        CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0, szOID_ORGANIZATION_NAME,
            org, sizeof(org) / sizeof(org[0]));
        FileTimeToSystemTime(&cert->pCertInfo->NotAfter, &expiry);
        CertRow* row = (CertRow*)calloc(1, sizeof(*row));
        if (!row) { CertFreeCertificateContext(cert); CertCloseStore(store, 0); return false; }
        if (terminal_number(subject, number))
            swprintf_s(row->identity, 320, L"Terminal %ls     O: %ls", number, org[0] ? org : L"(empty)");
        else
            swprintf_s(row->identity, 320, L"OU: %ls     O: %ls", ou[0] ? ou : L"(empty)", org[0] ? org : L"(empty)");
        swprintf_s(row->subject, 320, L"CN: %ls     OU: %ls", subject, ou[0] ? ou : L"(empty)");
        swprintf_s(row->details, 320, L"Until %04u-%02u-%02u %02u:%02u UTC   SHA1: %hs",
            expiry.wYear, expiry.wMonth, expiry.wDay, expiry.wHour, expiry.wMinute,
            thumbprint[0] ? thumbprint : "unavailable");
        LRESULT index = SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)row->identity);
        if (index == LB_ERR || index == LB_ERRSPACE) free(row);
        else SendMessageW(list, LB_SETITEMDATA, index, (LPARAM)row);
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
            initialize_layout(dialog);
            SendDlgItemMessageW(dialog, IDC_PIN_EDIT, EM_LIMITTEXT, 16, 0);
            refresh_certificates(dialog);
            return TRUE;
        case WM_SIZE:
            if (ui_dpi) layout(dialog);
            return TRUE;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* limits = (MINMAXINFO*)lparam;
            limits->ptMinTrackSize.x = px(620);
            limits->ptMinTrackSize.y = px(520);
            return TRUE;
        }
        case WM_DELETEITEM: {
            const DELETEITEMSTRUCT* item = (const DELETEITEMSTRUCT*)lparam;
            if (item->CtlID == IDC_CERT_LIST) { free((void*)item->itemData); return TRUE; }
            break;
        }
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
            if (!item) break;
            if (item->CtlID == IDC_CERT_LIST) {
                bool selected = (item->itemState & ODS_SELECTED) != 0;
                FillRect(item->hDC, &item->rcItem, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
                SetBkMode(item->hDC, TRANSPARENT);
                SetTextColor(item->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
                CertRow* row = item->itemID == (UINT)-1 ? NULL : (CertRow*)item->itemData;
                RECT line = item->rcItem;
                line.left += px(10); line.right -= px(10); line.top += px(4); line.bottom = line.top + px(24);
                HFONT old_font = (HFONT)SelectObject(item->hDC, identity_font);
                if (row) {
                    DrawTextW(item->hDC, row->identity, -1, &line, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                    SelectObject(item->hDC, normal_font);
                    line.top = line.bottom; line.bottom += px(22);
                    DrawTextW(item->hDC, row->subject, -1, &line, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                    line.top = line.bottom; line.bottom += px(22);
                    DrawTextW(item->hDC, row->details, -1, &line, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                } else if (item->itemID != (UINT)-1) {
                    wchar_t text[320] = {0};
                    SendMessageW(item->hwndItem, LB_GETTEXT, item->itemID, (LPARAM)text);
                    DrawTextW(item->hDC, text, -1, &line, DT_SINGLELINE | DT_END_ELLIPSIS);
                }
                SelectObject(item->hDC, old_font);
                if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
                return TRUE;
            }
            if (item->CtlID != IDC_FORCE) break;
            FillRect(item->hDC, &item->rcItem, GetSysColorBrush(COLOR_3DFACE));
            int side = item->rcItem.bottom - item->rcItem.top - px(8);
            RECT box = { item->rcItem.left + 3,
                         item->rcItem.top + (item->rcItem.bottom - item->rcItem.top - side) / 2,
                         item->rcItem.left + 3 + side,
                         item->rcItem.top + (item->rcItem.bottom - item->rcItem.top + side) / 2 };
            DrawFrameControl(item->hDC, &box, DFC_BUTTON,
                             DFCS_BUTTONCHECK | (force_checked ? DFCS_CHECKED : 0) |
                             ((item->itemState & ODS_DISABLED) ? DFCS_INACTIVE : 0));
            RECT label = item->rcItem;
            label.left = box.right + 9;
            HFONT old_font = force_font ? (HFONT)SelectObject(item->hDC, force_font) : NULL;
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, GetSysColor(COLOR_BTNTEXT));
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
            SendDlgItemMessageW(dialog, IDC_CERT_LIST, LB_RESETCONTENT, 0, 0);
            if (force_font) DeleteObject(force_font);
            if (normal_font) DeleteObject(normal_font);
            if (identity_font) DeleteObject(identity_font);
            if (pin_font) DeleteObject(pin_font);
            force_font = normal_font = identity_font = pin_font = NULL;
            ui_dpi = 0;
            return TRUE;
    }
    return FALSE;
}

int l4pin_show_gui(void) {
    return (int)DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_PIN_GUI),
                                NULL, gui_proc, 0);
}
