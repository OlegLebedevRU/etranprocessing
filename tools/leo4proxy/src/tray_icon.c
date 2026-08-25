/**
 * @file tray_icon.c
 * @brief System Tray icon, context menu, controls and info dialog for Leo4Proxy.
 */

#include "tray_icon.h"
#include "discovery.h"
#include "firewall.h"
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

static HICON load_state_icon(TrayState state) {
    HINSTANCE hInst = GetModuleHandle(NULL);
    HICON hIcon = NULL;

    if (state == TRAY_STATE_RUNNING) {
        hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_TRAY_ACTIVE), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    } else {
        hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_TRAY_STOPPED), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    }

    if (!hIcon) {
        hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    }
    if (!hIcon) {
        hIcon = LoadIconW(NULL, (state == TRAY_STATE_RUNNING) ? IDI_APPLICATION : IDI_WARNING);
    }
    return hIcon;
}

static void update_tray_tooltip(TrayIconContext* ctx) {
    if (!ctx || !ctx->certDetails) return;

    WCHAR szSn[128];
    utf8_to_w(ctx->certDetails->sn, szSn, sizeof(szSn)/sizeof(WCHAR));

    WCHAR szHost[128];
    utf8_to_w(ctx->localHostname, szHost, sizeof(szHost)/sizeof(WCHAR));

    if (ctx->currentState == TRAY_STATE_RUNNING) {
        swprintf_s(g_nid.szTip, sizeof(g_nid.szTip)/sizeof(WCHAR),
                   L"Leo4Proxy - RUNNING (%s)\nhttps://%s\nReverse: %s | Forward: %s",
                   szSn, szHost,
                   ctx->isReverseProxyRunning ? L"ON" : L"OFF",
                   ctx->isForwardProxyRunning ? L"ON" : L"OFF");
    } else {
        swprintf_s(g_nid.szTip, sizeof(g_nid.szTip)/sizeof(WCHAR),
                   L"Leo4Proxy - STOPPED\nSN: %s", szSn);
    }

    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void tray_icon_set_state(TrayIconContext* ctx, TrayState state) {
    if (!ctx) return;
    ctx->currentState = state;

    HICON hOldIcon = g_nid.hIcon;
    g_nid.hIcon = load_state_icon(state);

    update_tray_tooltip(ctx);

    if (hOldIcon && hOldIcon != g_nid.hIcon) {
        DestroyIcon(hOldIcon);
    }
}

void tray_icon_set_reverse_state(TrayIconContext* ctx, bool isRunning) {
    if (!ctx) return;
    ctx->isReverseProxyRunning = isRunning;
    update_tray_tooltip(ctx);
}

void tray_icon_set_forward_state(TrayIconContext* ctx, bool isRunning) {
    if (!ctx) return;
    ctx->isForwardProxyRunning = isRunning;
    update_tray_tooltip(ctx);
}

void tray_icon_show_info_dialog(TrayIconContext* ctx) {
    if (!ctx || !ctx->certDetails) return;

    WCHAR szTitle[256];
    swprintf_s(szTitle, sizeof(szTitle)/sizeof(WCHAR), L"Leo4Proxy v%hs - Information & Status", LEO4_PROXY_VERSION);

    WCHAR szSn[128], szUrn[128], szEmail[128], szIssuer[256], szThumb[64], szNotBefore[64], szNotAfter[64];
    WCHAR szMqttRemote[256], szHttpRemote[256], szMqttLocal[64], szHttpLocal[64];
    WCHAR szLocalHostname[256], szLanIp[64], szSanDns[256];

    utf8_to_w(ctx->certDetails->sn, szSn, sizeof(szSn)/sizeof(WCHAR));
    utf8_to_w(ctx->certDetails->urn, szUrn, sizeof(szUrn)/sizeof(WCHAR));
    utf8_to_w(ctx->certDetails->email, szEmail, sizeof(szEmail)/sizeof(WCHAR));
    utf8_to_w(ctx->certDetails->issuer, szIssuer, sizeof(szIssuer)/sizeof(WCHAR));
    utf8_to_w(ctx->certDetails->thumbprint, szThumb, sizeof(szThumb)/sizeof(WCHAR));
    utf8_to_w(ctx->certDetails->not_before, szNotBefore, sizeof(szNotBefore)/sizeof(WCHAR));
    utf8_to_w(ctx->certDetails->not_after, szNotAfter, sizeof(szNotAfter)/sizeof(WCHAR));
    utf8_to_w(ctx->certDetails->san_dns, szSanDns, sizeof(szSanDns)/sizeof(WCHAR));
    utf8_to_w(ctx->localHostname, szLocalHostname, sizeof(szLocalHostname)/sizeof(WCHAR));
    utf8_to_w(ctx->lanIp, szLanIp, sizeof(szLanIp)/sizeof(WCHAR));

    utf8_to_w(ctx->config->mqtt_remote_host, szMqttRemote, sizeof(szMqttRemote)/sizeof(WCHAR));
    utf8_to_w(ctx->config->http_remote_host, szHttpRemote, sizeof(szHttpRemote)/sizeof(WCHAR));
    utf8_to_w(ctx->config->mqtt_local_host, szMqttLocal, sizeof(szMqttLocal)/sizeof(WCHAR));
    utf8_to_w(ctx->config->http_local_host, szHttpLocal, sizeof(szHttpLocal)/sizeof(WCHAR));

    WCHAR szMessage[3072];
    swprintf_s(szMessage, sizeof(szMessage)/sizeof(WCHAR),
        L"==========================================================\n"
        L" Leo4Proxy v%hs - SChannel TLS Proxy & LAN Gateway\n"
        L" Overall Status: %s\n"
        L"==========================================================\n\n"
        L"--- Device & Certificate ---\n"
        L"Device SN (CN):   %s\n"
        L"Local Hostname:   %s\n"
        L"Primary LAN IP:   %s\n"
        L"SAN DNS:          %s\n"
        L"SAN URN:          %s\n"
        L"Email:            %s\n"
        L"Thumbprint:       %s\n"
        L"Issuer:           %s\n"
        L"Valid To:         %s\n"
        L"Private Key:      %s\n\n"
        L"--- Reverse HTTPS Proxy (Inbound) ---\n"
        L"Status:           %s\n"
        L"Listening:        https://%hs:%d\n"
        L"Target Backend:   http://%hs:%d (Plain HTTP)\n"
        L"LAN Discovery:    mDNS (:5353) & LLMNR (:5355) ACTIVE\n"
        L"Firewall Rules:   %s\n\n"
        L"--- Forward mTLS Proxies (Outbound) ---\n"
        L"Status:           %s\n"
        L"MQTT Proxy:       http://%s:%d -> %s:%d (mTLS)\n"
        L"HTTP Proxy:       http://%s:%d -> https://%s:%d (mTLS)\n"
        L"Diagnostic API:   http://%s:%d/_leo4/info\n\n"
        L"Right-click the tray icon for quick actions and controls.",
        LEO4_PROXY_VERSION,
        (ctx->currentState == TRAY_STATE_RUNNING) ? L"RUNNING (Active)" : L"STOPPED (Paused)",
        szSn,
        szLocalHostname,
        szLanIp[0] ? szLanIp : L"127.0.0.1",
        szSanDns[0] ? szSanDns : L"(none)",
        szUrn[0] ? szUrn : L"(none)",
        szEmail,
        szThumb,
        szIssuer,
        szNotAfter,
        ctx->certDetails->has_private_key ? L"YES (CNG KSP)" : L"NO",
        ctx->isReverseProxyRunning ? L"RUNNING (Active)" : L"STOPPED (Disabled)",
        ctx->config->reverse_local_host, ctx->config->reverse_local_port,
        ctx->config->reverse_target_host, ctx->config->reverse_target_port,
        firewall_is_elevated() ? L"AUTOMATED (Active)" : L"Manual / Unprivileged",
        ctx->isForwardProxyRunning ? L"RUNNING (Active)" : L"STOPPED (Disabled)",
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

    WCHAR szLocalHost[256];
    utf8_to_w(g_trayCtx->localHostname, szLocalHost, sizeof(szLocalHost)/sizeof(WCHAR));

    // 1. Header info line
    WCHAR szHeader[256];
    if (g_trayCtx->currentState == TRAY_STATE_RUNNING) {
        swprintf_s(szHeader, sizeof(szHeader)/sizeof(WCHAR), L"[RUNNING] Leo4Proxy (%s)", szSn);
    } else {
        swprintf_s(szHeader, sizeof(szHeader)/sizeof(WCHAR), L"[STOPPED] Leo4Proxy (%s)", szSn);
    }
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING | MF_DISABLED | MF_GRAYED, IDM_TRAY_HEADER, szHeader);
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

    // 2. Quick LAN Actions
    WCHAR szOpenLocal[300], szCopyLocal[300];
    if (g_trayCtx->config->reverse_local_port == 443) {
        swprintf_s(szOpenLocal, sizeof(szOpenLocal)/sizeof(WCHAR), L"Open https://%s in Browser", szLocalHost);
        swprintf_s(szCopyLocal, sizeof(szCopyLocal)/sizeof(WCHAR), L"Copy https://%s to Clipboard", szLocalHost);
    } else {
        swprintf_s(szOpenLocal, sizeof(szOpenLocal)/sizeof(WCHAR), L"Open https://%s:%d in Browser", szLocalHost, g_trayCtx->config->reverse_local_port);
        swprintf_s(szCopyLocal, sizeof(szCopyLocal)/sizeof(WCHAR), L"Copy https://%s:%d to Clipboard", szLocalHost, g_trayCtx->config->reverse_local_port);
    }
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_OPEN_LOCAL_HTTPS, szOpenLocal);
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_COPY_LOCAL_URL, szCopyLocal);

    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

    // 3. Reverse Proxy Controls
    WCHAR szRevStatus[256], szRevToggle[128];
    if (g_trayCtx->isReverseProxyRunning) {
        swprintf_s(szRevStatus, sizeof(szRevStatus)/sizeof(WCHAR), L"Reverse HTTPS: RUNNING (:%d -> :%d)",
                   g_trayCtx->config->reverse_local_port, g_trayCtx->config->reverse_target_port);
        swprintf_s(szRevToggle, sizeof(szRevToggle)/sizeof(WCHAR), L"Stop Reverse Proxy");
    } else {
        swprintf_s(szRevStatus, sizeof(szRevStatus)/sizeof(WCHAR), L"Reverse HTTPS: STOPPED");
        swprintf_s(szRevToggle, sizeof(szRevToggle)/sizeof(WCHAR), L"Start Reverse Proxy");
    }
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING | MF_DISABLED | MF_GRAYED, 0, szRevStatus);
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_TOGGLE_REVERSE, szRevToggle);

    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

    // 4. Forward Proxies Controls
    WCHAR szFwdStatus[256], szFwdToggle[128];
    if (g_trayCtx->isForwardProxyRunning) {
        swprintf_s(szFwdStatus, sizeof(szFwdStatus)/sizeof(WCHAR), L"Forward Proxies: RUNNING (MQTT/HTTP)");
        swprintf_s(szFwdToggle, sizeof(szFwdToggle)/sizeof(WCHAR), L"Stop Forward Proxies");
    } else {
        swprintf_s(szFwdStatus, sizeof(szFwdStatus)/sizeof(WCHAR), L"Forward Proxies: STOPPED");
        swprintf_s(szFwdToggle, sizeof(szFwdToggle)/sizeof(WCHAR), L"Start Forward Proxies");
    }
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING | MF_DISABLED | MF_GRAYED, 0, szFwdStatus);
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_TOGGLE_FORWARD, szFwdToggle);

    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

    // 5. Global Start / Stop / Restart
    if (g_trayCtx->currentState == TRAY_STATE_RUNNING) {
        InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_STOP_ALL, L"Stop All Proxies");
    } else {
        InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_START_ALL, L"Start All Proxies");
    }
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_RESTART_ALL, L"Restart All Proxies");

    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

    // 6. Information & Utilities
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_INFO, L"Information & Status...");
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_BROWSER, L"Open /_leo4/info in Browser");
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_COPY_SN, L"Copy Device SN to Clipboard");

    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

    // 7. Console visibility toggle
    HWND hConsole = GetConsoleWindow();
    if (hConsole) {
        BOOL isVisible = IsWindowVisible(hConsole);
        InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_CONSOLE,
                    isVisible ? L"Hide Console Window" : L"Show Console Window");
        InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    }

    // 8. Exit
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, IDM_TRAY_EXIT, L"Exit");

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
                case IDM_TRAY_START_ALL:
                case IDM_TRAY_STOP_ALL:
                case IDM_TRAY_RESTART_ALL:
                case IDM_TRAY_TOGGLE_REVERSE:
                case IDM_TRAY_TOGGLE_FORWARD:
                case IDM_TRAY_EXIT:
                    if (g_trayCtx->onAction) {
                        g_trayCtx->onAction(wmId, g_trayCtx->userData);
                    }
                    break;

                case IDM_TRAY_OPEN_LOCAL_HTTPS: {
                    WCHAR szUrl[300];
                    WCHAR szHost[256];
                    utf8_to_w(g_trayCtx->localHostname, szHost, sizeof(szHost)/sizeof(WCHAR));
                    if (g_trayCtx->config->reverse_local_port == 443) {
                        swprintf_s(szUrl, sizeof(szUrl)/sizeof(WCHAR), L"https://%s", szHost);
                    } else {
                        swprintf_s(szUrl, sizeof(szUrl)/sizeof(WCHAR), L"https://%s:%d", szHost, g_trayCtx->config->reverse_local_port);
                    }
                    ShellExecuteW(NULL, L"open", szUrl, NULL, NULL, SW_SHOWNORMAL);
                    break;
                }

                case IDM_TRAY_COPY_LOCAL_URL: {
                    char url[300];
                    if (g_trayCtx->config->reverse_local_port == 443) {
                        snprintf(url, sizeof(url), "https://%s", g_trayCtx->localHostname);
                    } else {
                        snprintf(url, sizeof(url), "https://%s:%d", g_trayCtx->localHostname, g_trayCtx->config->reverse_local_port);
                    }
                    copy_to_clipboard(hWnd, url);
                    break;
                }

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

    update_tray_tooltip(ctx);

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
    ctx->isReverseProxyRunning = (config->reverse_proxy_enabled != 0);
    ctx->isForwardProxyRunning = true;
    ctx->onAction = onAction;
    ctx->userData = userData;
    ctx->isRunning = true;

    if (config->custom_local_domain[0] != '\0') {
        strncpy_s(ctx->localHostname, sizeof(ctx->localHostname), config->custom_local_domain, _TRUNCATE);
    } else if (certDetails->local_hostname[0] != '\0') {
        strncpy_s(ctx->localHostname, sizeof(ctx->localHostname), certDetails->local_hostname, _TRUNCATE);
    } else {
        strncpy_s(ctx->localHostname, sizeof(ctx->localHostname), "leo4-device.local", _TRUNCATE);
    }

    discovery_get_lan_ip(ctx->lanIp, sizeof(ctx->lanIp), NULL);

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
