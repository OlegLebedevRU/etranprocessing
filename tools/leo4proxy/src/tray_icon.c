#include "tray_icon.h"
#include "../res/resource.h"
#include <windows.h>
#include <shellapi.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>

#define WM_TRAYICON (WM_USER + 100)
#define TRAY_WINDOW_CLASS L"Leo4ProxyTrayWindowClass"

static TrayIconContext* g_trayCtx = NULL;
static NOTIFYICONDATAW g_nid;

static void utf8_to_w(const char* utf8, WCHAR* out_w, size_t out_w_count) {
    if (!out_w || out_w_count == 0) return;
    out_w[0] = L'\0';
    if (!utf8 || utf8[0] == '\0') return;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out_w, (int)out_w_count);
}

static void copy_to_clipboard(HWND hWnd, const char* text) {
    if (!text || text[0] == '\0') return;
    if (!OpenClipboard(hWnd)) return;
    EmptyClipboard();

    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wlen > 0) {
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(WCHAR));
        if (hMem) {
            WCHAR* pMem = (WCHAR*)GlobalLock(hMem);
            if (pMem) {
                MultiByteToWideChar(CP_UTF8, 0, text, -1, pMem, wlen);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
    }
    CloseClipboard();
}

void tray_icon_show_info_dialog(TrayIconContext* ctx) {
    if (!ctx || !ctx->certDetails) return;

    WCHAR szTitle[256];
    swprintf_s(szTitle, sizeof(szTitle)/sizeof(WCHAR), L"Leo4Proxy v%hs - Information", LEO4_PROXY_VERSION);

    WCHAR szSn[128], szUrn[128], szEmail[128], szIssuer[256], szThumb[64], szNotBefore[64], szNotAfter[64];
    WCHAR szMqttRemote[256], szHttpRemote[256], szMqttLocal[64], szHttpLocal[64];

    utf8_to_w(ctx->certDetails->sn, szSn, sizeof(szSn)/sizeof(WCHAR));
    utf8_to_w(ctx->certDetails->urn, szUrn, sizeof(szUrn)/sizeof(WCHAR));
    utf8_to_w(ctx->certDetails->email, szEmail, sizeof(szEmail)/sizeof(WCHAR));
    utf8_to_w(ctx->certDetails->issuer, szIssuer, sizeof(szIssuer)/sizeof(WCHAR));
    utf8_to_w(ctx->certDetails->thumbprint, szThumb, sizeof(szThumb)/sizeof(WCHAR));
    utf8_to_w(ctx->certDetails->not_before, szNotBefore, sizeof(szNotBefore)/sizeof(WCHAR));
    utf8_to_w(ctx->certDetails->not_after, szNotAfter, sizeof(szNotAfter)/sizeof(WCHAR));

    utf8_to_w(ctx->config->mqtt_remote_host, szMqttRemote, sizeof(szMqttRemote)/sizeof(WCHAR));
    utf8_to_w(ctx->config->http_remote_host, szHttpRemote, sizeof(szHttpRemote)/sizeof(WCHAR));
    utf8_to_w(ctx->config->mqtt_local_host, szMqttLocal, sizeof(szMqttLocal)/sizeof(WCHAR));
    utf8_to_w(ctx->config->http_local_host, szHttpLocal, sizeof(szHttpLocal)/sizeof(WCHAR));

    WCHAR szMessage[2048];
    swprintf_s(szMessage, sizeof(szMessage)/sizeof(WCHAR),
        L"==========================================================\n"
        L" Leo4Proxy v%hs - SChannel mTLS Proxy\n"
        L" Status: %s\n"
        L"==========================================================\n\n"
        L"--- Device & Certificate ---\n"
        L"Device SN (CN):  %s\n"
        L"Client ID:       %s\n"
        L"SAN URN:         %s\n"
        L"Email:           %s\n"
        L"Thumbprint:      %s\n"
        L"Issuer:          %s\n"
        L"Valid From:      %s\n"
        L"Valid To:        %s\n"
        L"Private Key:     %s\n\n"
        L"--- Active Proxy Endpoints ---\n"
        L"MQTT Proxy:      http://%s:%d -> %s:%d (mTLS)\n"
        L"HTTP Proxy:      http://%s:%d -> https://%s:%d (mTLS)\n"
        L"Local Info API:  http://%s:%d/_leo4/info\n\n"
        L"Right-click the tray icon for quick actions and controls.",
        LEO4_PROXY_VERSION,
        (ctx->currentState == TRAY_STATE_RUNNING) ? L"RUNNING (Active)" : L"STOPPED (Paused)",
        szSn,
        szSn,
        szUrn[0] ? szUrn : L"(none)",
        szEmail,
        szThumb,
        szIssuer,
        szNotBefore,
        szNotAfter,
        ctx->certDetails->has_private_key ? L"YES (CNG KSP)" : L"NO",
        szMqttLocal, ctx->config->mqtt_local_port, szMqttRemote, ctx->config->mqtt_remote_port,
        szHttpLocal, ctx->config->http_local_port, szHttpRemote, ctx->config->http_remote_port,
        szHttpLocal, ctx->config->http_local_port
    );

    MessageBoxW(ctx->hWnd, szMessage, szTitle, MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
}

static void show_context_menu(HWND hWnd) {
    if (!g_trayCtx) return;

    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    WCHAR szSn[128];
    utf8_to_w(g_trayCtx->certDetails->sn, szSn, sizeof(szSn)/sizeof(WCHAR));

    // Header info line
    WCHAR szHeader[256];
    if (g_trayCtx->currentState == TRAY_STATE_RUNNING) {
        swprintf_s(szHeader, sizeof(szHeader)/sizeof(WCHAR), L"[RUNNING] Leo4Proxy (%s)", szSn);
    } else {
        swprintf_s(szHeader, sizeof(szHeader)/sizeof(WCHAR), L"[STOPPED] Leo4Proxy (%s)", szSn);
    }
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING | MF_DISABLED | MF_GRAYED, IDM_TRAY_HEADER, szHeader);
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

    // Start / Stop Selector
    if (g_trayCtx->currentState == TRAY_STATE_RUNNING) {
        InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_STOP, L"Stop Proxies");
    } else {
        InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_START, L"Start Proxies");
    }
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_RESTART, L"Restart Proxies");

    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

    // Information Block
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_INFO, L"Information & Certificate...");
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_BROWSER, L"Open /_leo4/info in Browser");
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_COPY_SN, L"Copy Device SN to Clipboard");

    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

    // Console visibility toggle
    HWND hConsole = GetConsoleWindow();
    if (hConsole) {
        BOOL isVisible = IsWindowVisible(hConsole);
        InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_CONSOLE,
                    isVisible ? L"Hide Console Window" : L"Show Console Window");
        InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    }

    // Exit
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_EXIT, L"Exit");

    // Must set foreground window before TrackPopupMenu
    SetForegroundWindow(hWnd);
    TrackPopupMenuEx(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, hWnd, NULL);
    PostMessage(hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

static LRESULT CALLBACK tray_window_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                show_context_menu(hWnd);
                return 0;
            } else if (lParam == WM_LBUTTONDBLCLK) {
                tray_icon_show_info_dialog(g_trayCtx);
                return 0;
            }
            break;

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (!g_trayCtx) break;

            switch (wmId) {
                case IDM_TRAY_START:
                case IDM_TRAY_STOP:
                case IDM_TRAY_RESTART:
                case IDM_TRAY_EXIT:
                    if (g_trayCtx->onAction) {
                        g_trayCtx->onAction(wmId, g_trayCtx->userData);
                    }
                    break;

                case IDM_TRAY_INFO:
                    tray_icon_show_info_dialog(g_trayCtx);
                    break;

                case IDM_TRAY_BROWSER: {
                    WCHAR szUrl[256];
                    swprintf_s(szUrl, sizeof(szUrl)/sizeof(WCHAR), L"http://%hs:%d/_leo4/info",
                               g_trayCtx->config->http_local_host, g_trayCtx->config->http_local_port);
                    ShellExecuteW(NULL, L"open", szUrl, NULL, NULL, SW_SHOWNORMAL);
                    break;
                }

                case IDM_TRAY_COPY_SN:
                    copy_to_clipboard(hWnd, g_trayCtx->certDetails->sn);
                    break;

                case IDM_TRAY_CONSOLE: {
                    HWND hConsole = GetConsoleWindow();
                    if (hConsole) {
                        if (IsWindowVisible(hConsole)) {
                            ShowWindow(hConsole, SW_HIDE);
                        } else {
                            ShowWindow(hConsole, SW_SHOW);
                            SetForegroundWindow(hConsole);
                        }
                    }
                    break;
                }
            }
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static HICON load_state_icon(TrayState state) {
    HINSTANCE hInstance = GetModuleHandle(NULL);
    HICON hIcon = NULL;

    if (state == TRAY_STATE_RUNNING) {
        hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_TRAY_ACTIVE), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    } else {
        hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_TRAY_STOPPED), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    }

    if (!hIcon) {
        hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    }
    if (!hIcon) {
        hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    return hIcon;
}

void tray_icon_set_state(TrayIconContext* ctx, TrayState state) {
    if (!ctx) return;
    ctx->currentState = state;

    HICON hOldIcon = g_nid.hIcon;
    g_nid.hIcon = load_state_icon(state);

    WCHAR szSn[128];
    utf8_to_w(ctx->certDetails->sn, szSn, sizeof(szSn)/sizeof(WCHAR));

    if (state == TRAY_STATE_RUNNING) {
        swprintf_s(g_nid.szTip, sizeof(g_nid.szTip)/sizeof(WCHAR),
                   L"Leo4Proxy - RUNNING\nSN: %s\nMQTT: %d | HTTP: %d",
                   szSn, ctx->config->mqtt_local_port, ctx->config->http_local_port);
    } else {
        swprintf_s(g_nid.szTip, sizeof(g_nid.szTip)/sizeof(WCHAR),
                   L"Leo4Proxy - STOPPED\nSN: %s", szSn);
    }

    Shell_NotifyIconW(NIM_MODIFY, &g_nid);

    if (hOldIcon && hOldIcon != g_nid.hIcon) {
        DestroyIcon(hOldIcon);
    }
}

static unsigned __stdcall tray_thread_proc(void* param) {
    TrayIconContext* ctx = (TrayIconContext*)param;
    HINSTANCE hInstance = GetModuleHandle(NULL);

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = tray_window_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = TRAY_WINDOW_CLASS;

    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        0,
        TRAY_WINDOW_CLASS,
        L"Leo4ProxyTrayWindow",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) {
        ctx->isRunning = false;
        return 1;
    }

    ctx->hWnd = hWnd;

    memset(&g_nid, 0, sizeof(NOTIFYICONDATAW));
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = load_state_icon(ctx->currentState);

    WCHAR szSn[128];
    utf8_to_w(ctx->certDetails->sn, szSn, sizeof(szSn)/sizeof(WCHAR));

    swprintf_s(g_nid.szTip, sizeof(g_nid.szTip)/sizeof(WCHAR),
               L"Leo4Proxy - RUNNING\nSN: %s\nMQTT: %d | HTTP: %d",
               szSn, ctx->config->mqtt_local_port, ctx->config->http_local_port);

    Shell_NotifyIconW(NIM_ADD, &g_nid);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0 && ctx->isRunning) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_nid.hIcon) {
        DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = NULL;
    }

    DestroyWindow(hWnd);
    UnregisterClassW(TRAY_WINDOW_CLASS, hInstance);
    ctx->isRunning = false;
    return 0;
}

bool tray_icon_start(TrayIconContext* ctx, const ProxyConfig* config, const CertDetails* certDetails, TrayActionCallback onAction, void* userData) {
    if (!ctx || !config || !certDetails) return false;
    memset(ctx, 0, sizeof(TrayIconContext));

    ctx->config = config;
    ctx->certDetails = certDetails;
    ctx->currentState = TRAY_STATE_RUNNING;
    ctx->onAction = onAction;
    ctx->userData = userData;
    ctx->isRunning = true;

    g_trayCtx = ctx;

    ctx->hThread = (HANDLE)_beginthreadex(NULL, 0, tray_thread_proc, ctx, 0, NULL);
    if (!ctx->hThread) {
        ctx->isRunning = false;
        return false;
    }

    return true;
}

void tray_icon_stop(TrayIconContext* ctx) {
    if (!ctx || !ctx->isRunning) return;
    ctx->isRunning = false;

    if (ctx->hWnd) {
        PostMessage(ctx->hWnd, WM_CLOSE, 0, 0);
    }

    if (ctx->hThread) {
        WaitForSingleObject(ctx->hThread, 2000);
        CloseHandle(ctx->hThread);
        ctx->hThread = NULL;
    }

    g_trayCtx = NULL;
}
