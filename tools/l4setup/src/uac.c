#include "uac.h"
#include <shellapi.h>

bool uac_is_elevated(void) {
    bool is_elevated = false;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD cbSize = sizeof(elevation);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
            is_elevated = (elevation.TokenIsElevated != 0);
        }
        CloseHandle(hToken);
    }
    return is_elevated;
}

const wchar_t* uac_get_arguments(const wchar_t* full_cmd_line) {
    if (!full_cmd_line) return L"";

    const wchar_t* p = full_cmd_line;
    // Skip leading whitespaces
    while (*p == L' ' || *p == L'\t') p++;

    if (*p == L'"') {
        p++; // Skip opening quote
        while (*p && *p != L'"') p++;
        if (*p == L'"') p++; // Skip closing quote
    } else {
        while (*p && *p != L' ' && *p != L'\t') p++;
    }

    // Skip whitespace between exe and args
    while (*p == L' ' || *p == L'\t') p++;

    return p;
}

DWORD uac_relaunch_elevated(const wchar_t* cmd_line_args, bool* user_cancelled) {
    if (user_cancelled) *user_cancelled = false;

    wchar_t exe_path[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);

    SHELLEXECUTEINFOW sei;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exe_path;
    sei.lpParameters = cmd_line_args;
    sei.nShow = SW_NORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            if (user_cancelled) *user_cancelled = true;
        }
        return 20; // Exit code 20 for UAC decline / elevation failure
    }

    if (!sei.hProcess) {
        return 0;
    }

    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(sei.hProcess, &exit_code);
    CloseHandle(sei.hProcess);

    return exit_code;
}
