#include "kiosk_lifecycle.h"
#include <string.h>
#include <stdio.h>

static wchar_t s_process_name[MAX_PATH] = { 0 };
static wchar_t s_launch_path[MAX_PATH] = { 0 };
static wchar_t s_cmd_args[MAX_PATH] = { 0 };

static HANDLE s_hProcess = NULL;
static DWORD s_pid = 0;
static HWND s_hwnd = NULL;
static l4d_kiosk_status_t s_status = L4D_KIOSK_STATUS_STOPPED;
static uint64_t s_start_tick = 0;

typedef struct {
    DWORD target_pid;
    HWND result_hwnd;
} find_hwnd_ctx_t;

static BOOL CALLBACK find_main_window_cb(HWND hwnd, LPARAM lParam) {
    find_hwnd_ctx_t *ctx = (find_hwnd_ctx_t *)lParam;
    if (!IsWindowVisible(hwnd)) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx->target_pid) {
        ctx->result_hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

static bool is_path_allowed(const wchar_t *exe_path) {
    if (!exe_path || exe_path[0] == L'\0') return false;
    if (s_process_name[0] == L'\0') return false;

    const wchar_t *file_name = wcsrchr(exe_path, L'\\');
    file_name = file_name ? (file_name + 1) : exe_path;
    return (_wcsicmp(file_name, s_process_name) == 0);
}

static void clear_state(void) {
    if (s_hProcess) { CloseHandle(s_hProcess); s_hProcess = NULL; }
    s_pid = 0;
    s_hwnd = NULL;
    s_status = L4D_KIOSK_STATUS_STOPPED;
    s_start_tick = 0;
}

bool kiosk_lifecycle_init(const wchar_t *process_name, const wchar_t *launch_path, const wchar_t *cmd_args) {
    clear_state();

    if (!process_name || process_name[0] == L'\0') return false;

    wcsncpy_s(s_process_name, MAX_PATH, process_name, _TRUNCATE);
    if (launch_path && launch_path[0] != L'\0') {
        wcsncpy_s(s_launch_path, MAX_PATH, launch_path, _TRUNCATE);
    } else {
        s_launch_path[0] = L'\0';
    }
    if (cmd_args && cmd_args[0] != L'\0') {
        wcsncpy_s(s_cmd_args, MAX_PATH, cmd_args, _TRUNCATE);
    } else {
        s_cmd_args[0] = L'\0';
    }

    return true;
}

bool kiosk_lifecycle_start(DWORD *out_pid, DWORD *out_error) {
    if (out_pid) *out_pid = 0;
    if (out_error) *out_error = 0;

    if (s_process_name[0] == L'\0') {
        if (out_error) *out_error = ERROR_INVALID_PARAMETER;
        return false;
    }

    /* Check if already running */
    if (s_hProcess && WaitForSingleObject(s_hProcess, 0) == WAIT_TIMEOUT) {
        if (out_pid) *out_pid = s_pid;
        return true;
    }
    clear_state();

    /* Check session interactivity */
    DWORD session_id = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id) || session_id == 0) {
        if (out_error) *out_error = ERROR_NOT_SUPPORTED;
        return false;
    }

    /* Build command line */
    wchar_t cmdline[1024];
    if (s_launch_path[0] != L'\0') {
        if (s_cmd_args[0] != L'\0') {
            _snwprintf_s(cmdline, sizeof(cmdline)/sizeof(wchar_t), _TRUNCATE,
                         L"\"%s\" %s", s_launch_path, s_cmd_args);
        } else {
            _snwprintf_s(cmdline, sizeof(cmdline)/sizeof(wchar_t), _TRUNCATE,
                         L"\"%s\"", s_launch_path);
        }
    } else {
        /* Use process name as-is */
        wcsncpy_s(cmdline, sizeof(cmdline)/sizeof(wchar_t), s_process_name, _TRUNCATE);
    }

    /* Security: verify the executable is the trusted kiosk process */
    if (s_launch_path[0] != L'\0' && !is_path_allowed(s_launch_path)) {
        if (out_error) *out_error = ERROR_ACCESS_DENIED;
        return false;
    }

    s_status = L4D_KIOSK_STATUS_STARTING;

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;

    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessW(s_launch_path[0] ? s_launch_path : NULL,
                             cmdline, NULL, NULL, FALSE,
                             CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT,
                             NULL, NULL, &si, &pi);

    if (!ok) {
        if (out_error) *out_error = GetLastError();
        s_status = L4D_KIOSK_STATUS_STOPPED;
        return false;
    }

    CloseHandle(pi.hThread);
    s_hProcess = pi.hProcess;
    s_pid = pi.dwProcessId;
    s_start_tick = GetTickCount64();

    /* Wait briefly for window to appear */
    WaitForInputIdle(pi.hProcess, 5000);

    /* Discover HWND */
    find_hwnd_ctx_t ctx = { s_pid, NULL };
    EnumWindows(find_main_window_cb, (LPARAM)&ctx);
    s_hwnd = ctx.result_hwnd;

    s_status = L4D_KIOSK_STATUS_RUNNING;

    if (out_pid) *out_pid = s_pid;
    return true;
}

bool kiosk_lifecycle_stop(uint32_t timeout_ms, bool force_terminate, DWORD *out_error) {
    if (out_error) *out_error = 0;

    if (!s_hProcess || WaitForSingleObject(s_hProcess, 0) == WAIT_OBJECT_0) {
        clear_state();
        return true;
    }

    s_status = L4D_KIOSK_STATUS_STOPPING;

    /* Phase 1: Graceful WM_CLOSE */
    if (s_hwnd && IsWindow(s_hwnd)) {
        PostMessage(s_hwnd, WM_CLOSE, 0, 0);
    }

    DWORD wait_ms = (timeout_ms > 0) ? timeout_ms : 3000;
    DWORD result = WaitForSingleObject(s_hProcess, wait_ms);

    if (result == WAIT_OBJECT_0) {
        clear_state();
        return true;
    }

    /* Phase 2: Check if hung */
    if (s_hwnd && IsWindow(s_hwnd) && IsHungAppWindow(s_hwnd)) {
        s_status = L4D_KIOSK_STATUS_UNRESPONSIVE;
    }

    /* Phase 3: Force terminate if allowed */
    if (force_terminate) {
        if (!TerminateProcess(s_hProcess, 1)) {
            if (out_error) *out_error = GetLastError();
        }
        WaitForSingleObject(s_hProcess, 2000);
        clear_state();
        return true;
    }

    if (out_error) *out_error = ERROR_TIMEOUT;
    return false;
}

bool kiosk_lifecycle_restart(uint32_t stop_timeout_ms, DWORD *out_pid, DWORD *out_error) {
    if (out_pid) *out_pid = 0;
    if (out_error) *out_error = 0;

    if (!kiosk_lifecycle_stop(stop_timeout_ms, true, out_error)) {
        return false;
    }

    return kiosk_lifecycle_start(out_pid, out_error);
}

bool kiosk_lifecycle_get_status(l4d_kiosk_info_t *out_info) {
    if (!out_info) return false;
    memset(out_info, 0, sizeof(*out_info));

    /* Check if process still alive */
    if (s_hProcess && WaitForSingleObject(s_hProcess, 0) == WAIT_OBJECT_0) {
        clear_state();
    }

    out_info->status = s_status;
    out_info->pid = s_pid;
    out_info->hwnd = s_hwnd;
    wcsncpy_s(out_info->process_name, MAX_PATH, s_process_name, _TRUNCATE);

    if (s_status == L4D_KIOSK_STATUS_RUNNING && s_start_tick > 0) {
        out_info->uptime_sec = (GetTickCount64() - s_start_tick) / 1000;
    }

    if (s_hwnd && IsWindow(s_hwnd)) {
        out_info->is_hung = IsHungAppWindow(s_hwnd);
        if (out_info->is_hung) {
            out_info->status = L4D_KIOSK_STATUS_UNRESPONSIVE;
        }
    }

    return true;
}
