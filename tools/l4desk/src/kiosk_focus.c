#include "kiosk_focus.h"
#include <string.h>
#include <stdint.h>
#include <tlhelp32.h>

static wchar_t s_kiosk_process[MAX_PATH] = { 0 };
static HWND s_cached_kiosk_hwnd = NULL;
static DWORD s_cached_kiosk_pid = 0;
static uint64_t s_last_enum_tick = 0;
#define ENUM_RATE_LIMIT_MS 500

typedef struct {
    const wchar_t *target_name;
    HWND result_hwnd;
    DWORD result_pid;
} kiosk_enum_ctx_t;

static BOOL CALLBACK enum_windows_cb(HWND hwnd, LPARAM lParam) {
    kiosk_enum_ctx_t *ctx = (kiosk_enum_ctx_t *)lParam;
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return TRUE;

    wchar_t img_path[MAX_PATH] = { 0 };
    DWORD img_len = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, img_path, &img_len)) {
        const wchar_t *file_name = wcsrchr(img_path, L'\\');
        file_name = file_name ? (file_name + 1) : img_path;
        if (_wcsicmp(file_name, ctx->target_name) == 0) {
            ctx->result_hwnd = hwnd;
            ctx->result_pid = pid;
            CloseHandle(hProc);
            return FALSE; /* stop enumeration */
        }
    }
    CloseHandle(hProc);
    return TRUE;
}

static bool discover_kiosk_window(void) {
    if (s_kiosk_process[0] == L'\0') return false;

    kiosk_enum_ctx_t ctx;
    ctx.target_name = s_kiosk_process;
    ctx.result_hwnd = NULL;
    ctx.result_pid = 0;

    EnumWindows(enum_windows_cb, (LPARAM)&ctx);

    if (ctx.result_hwnd) {
        s_cached_kiosk_hwnd = ctx.result_hwnd;
        s_cached_kiosk_pid = ctx.result_pid;
        return true;
    }
    return false;
}

bool kiosk_focus_init(const wchar_t *kiosk_process) {
    s_cached_kiosk_hwnd = NULL;
    s_cached_kiosk_pid = 0;
    s_last_enum_tick = 0;

    if (kiosk_process && kiosk_process[0] != L'\0') {
        wcsncpy_s(s_kiosk_process, MAX_PATH, kiosk_process, _TRUNCATE);
        /* Set foreground lock timeout to 0 for agent refocus capability */
        SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, (LPVOID)0,
                             SPIF_SENDWININICHANGE | SPIF_UPDATEINIFILE);
        return true;
    }
    s_kiosk_process[0] = L'\0';
    return true;
}

void kiosk_focus_destroy(void) {
    s_cached_kiosk_hwnd = NULL;
    s_cached_kiosk_pid = 0;
    s_kiosk_process[0] = L'\0';
}

bool kiosk_is_process_running(void) {
    if (s_kiosk_process[0] == L'\0') return false;

    /* If we have a cached HWND, fast check */
    if (s_cached_kiosk_hwnd && IsWindow(s_cached_kiosk_hwnd)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(s_cached_kiosk_hwnd, &pid);
        if (pid == s_cached_kiosk_pid && pid != 0) return true;
        /* HWND exists but PID changed — invalidate */
        s_cached_kiosk_hwnd = NULL;
        s_cached_kiosk_pid = 0;
    }

    /* Rate-limited full enumeration */
    uint64_t now = GetTickCount64();
    if (now - s_last_enum_tick < ENUM_RATE_LIMIT_MS) return false;
    s_last_enum_tick = now;

    return discover_kiosk_window();
}

bool kiosk_is_window_focused(void) {
    if (!s_cached_kiosk_hwnd || !IsWindow(s_cached_kiosk_hwnd)) return false;
    return (GetForegroundWindow() == s_cached_kiosk_hwnd);
}

bool kiosk_focus_force(HWND hKiosk) {
    if (!hKiosk || !IsWindow(hKiosk)) return false;

    HWND hForeground = GetForegroundWindow();
    if (hForeground == hKiosk) return true;

    /* Restore if minimized */
    if (IsIconic(hKiosk)) {
        ShowWindow(hKiosk, SW_RESTORE);
    }

    DWORD curThread = GetCurrentThreadId();
    DWORD fgThread = hForeground ? GetWindowThreadProcessId(hForeground, NULL) : 0;
    DWORD kioskThread = GetWindowThreadProcessId(hKiosk, NULL);

    /* 1. Attach input queues */
    if (fgThread && fgThread != curThread) {
        AttachThreadInput(curThread, fgThread, TRUE);
    }
    if (kioskThread && kioskThread != curThread) {
        AttachThreadInput(curThread, kioskThread, TRUE);
    }

    /* 2. ASFW bypass via synthetic Alt press/release */
    keybd_event(VK_MENU, 0, 0, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);

    /* 3. Force foreground */
    BringWindowToTop(hKiosk);
    SetForegroundWindow(hKiosk);
    SetActiveWindow(hKiosk);
    SetFocus(hKiosk);

    /* 4. Detach input queues */
    if (kioskThread && kioskThread != curThread) {
        AttachThreadInput(curThread, kioskThread, FALSE);
    }
    if (fgThread && fgThread != curThread) {
        AttachThreadInput(curThread, fgThread, FALSE);
    }

    /* 5. Verify */
    return (GetForegroundWindow() == hKiosk);
}

HWND kiosk_get_cached_hwnd(void) {
    return s_cached_kiosk_hwnd;
}

l4d_kiosk_focus_mode_t kiosk_get_active_mode(void) {
    if (s_kiosk_process[0] == L'\0') return L4D_KIOSK_FOCUS_MODE_GENERIC;
    if (kiosk_is_process_running()) return L4D_KIOSK_FOCUS_MODE_KIOSK;
    return L4D_KIOSK_FOCUS_MODE_FALLBACK;
}

void kiosk_get_active_input_mode(char *out, size_t max_len) {
    if (!out || max_len == 0) return;
    l4d_kiosk_focus_mode_t mode = kiosk_get_active_mode();
    switch (mode) {
    case L4D_KIOSK_FOCUS_MODE_KIOSK:    strcpy_s(out, max_len, "kiosk"); break;
    case L4D_KIOSK_FOCUS_MODE_FALLBACK: strcpy_s(out, max_len, "generic_fallback"); break;
    default:                            strcpy_s(out, max_len, "generic"); break;
    }
}

void kiosk_get_foreground_process(char *out, size_t max_len) {
    if (!out || max_len == 0) return;
    out[0] = '\0';

    HWND hFg = GetForegroundWindow();
    if (!hFg || !IsWindow(hFg)) {
        strcpy_s(out, max_len, "unknown");
        return;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hFg, &pid);
    if (pid == 0) {
        strcpy_s(out, max_len, "unknown");
        return;
    }

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) {
        strcpy_s(out, max_len, "unknown");
        return;
    }

    wchar_t img_path[MAX_PATH] = { 0 };
    DWORD img_len = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, img_path, &img_len)) {
        const wchar_t *file_name = wcsrchr(img_path, L'\\');
        file_name = file_name ? (file_name + 1) : img_path;
        WideCharToMultiByte(CP_UTF8, 0, file_name, -1, out, (int)max_len, NULL, NULL);
    } else {
        strcpy_s(out, max_len, "unknown");
    }
    CloseHandle(hProc);
}
