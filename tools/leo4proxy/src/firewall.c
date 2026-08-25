/**
 * @file firewall.c
 * @brief Windows Defender Firewall rule automation and UAC elevation helpers for Leo4Proxy.
 */

#include "firewall.h"
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

bool firewall_is_elevated(void) {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }
    TOKEN_ELEVATION elevation;
    DWORD cbSize = sizeof(TOKEN_ELEVATION);
    bool isElevated = false;
    if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
        isElevated = (elevation.TokenIsElevated != 0);
    }
    CloseHandle(hToken);
    return isElevated;
}

bool firewall_elevate_self(int argc, char* argv[]) {
    if (firewall_is_elevated()) {
        return true; // Already elevated
    }

    WCHAR szExe[MAX_PATH];
    if (GetModuleFileNameW(NULL, szExe, MAX_PATH) == 0) {
        return false;
    }

    // Build argument string
    WCHAR szArgs[2048] = { 0 };
    int offset = 0;
    for (int i = 1; i < argc; i++) {
        WCHAR szArgW[256];
        MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, szArgW, 256);
        offset += swprintf_s(szArgs + offset, (sizeof(szArgs)/sizeof(WCHAR)) - offset, L"\"%s\" ", szArgW);
    }

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = szExe;
    sei.lpParameters = (szArgs[0] != L'\0') ? szArgs : NULL;
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_DEFAULT;

    if (ShellExecuteExW(&sei)) {
        return true;
    }
    return false;
}

static bool run_netsh_cmd(const WCHAR* args) {
    WCHAR cmdLine[1024];
    swprintf_s(cmdLine, sizeof(cmdLine)/sizeof(WCHAR), L"netsh.exe advfirewall firewall %s", args);

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return false;
    }

    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (exitCode == 0);
}

bool firewall_ensure_rules(const ProxyConfig* config, const char* exePath) {
    if (!config || !config->firewall_auto) return true;

    if (!firewall_is_elevated()) {
        if (config->verbose) {
            printf("[FIREWALL] Note: Process is not elevated; firewall rules cannot be updated automatically.\n");
        }
        return false;
    }

    WCHAR szExeW[MAX_PATH] = { 0 };
    if (exePath && exePath[0] != '\0') {
        MultiByteToWideChar(CP_UTF8, 0, exePath, -1, szExeW, MAX_PATH);
    } else {
        GetModuleFileNameW(NULL, szExeW, MAX_PATH);
    }

    WCHAR cmd[1024];

    // 1. Inbound HTTPS
    run_netsh_cmd(L"delete rule name=\"Leo4Proxy HTTPS\"");
    swprintf_s(cmd, sizeof(cmd)/sizeof(WCHAR),
        L"add rule name=\"Leo4Proxy HTTPS\" dir=in action=allow protocol=TCP localport=%d program=\"%s\" enable=yes",
        config->reverse_local_port, szExeW);
    run_netsh_cmd(cmd);

    // 2. Inbound HTTP (Redirector)
    run_netsh_cmd(L"delete rule name=\"Leo4Proxy HTTP\"");
    swprintf_s(cmd, sizeof(cmd)/sizeof(WCHAR),
        L"add rule name=\"Leo4Proxy HTTP\" dir=in action=allow protocol=TCP localport=80 program=\"%s\" enable=yes",
        szExeW);
    run_netsh_cmd(cmd);

    // 3. Inbound mDNS
    run_netsh_cmd(L"delete rule name=\"Leo4Proxy mDNS\"");
    swprintf_s(cmd, sizeof(cmd)/sizeof(WCHAR),
        L"add rule name=\"Leo4Proxy mDNS\" dir=in action=allow protocol=UDP localport=5353 program=\"%s\" enable=yes",
        szExeW);
    run_netsh_cmd(cmd);

    // 4. Inbound LLMNR
    run_netsh_cmd(L"delete rule name=\"Leo4Proxy LLMNR\"");
    swprintf_s(cmd, sizeof(cmd)/sizeof(WCHAR),
        L"add rule name=\"Leo4Proxy LLMNR\" dir=in action=allow protocol=UDP localport=5355 program=\"%s\" enable=yes",
        szExeW);
    run_netsh_cmd(cmd);

    if (config->verbose) {
        printf("[FIREWALL] Windows Defender Firewall rules configured (HTTPS :%d, HTTP :80, mDNS :5353, LLMNR :5355)\n",
               config->reverse_local_port);
    }

    return true;
}

bool firewall_remove_rules(void) {
    if (!firewall_is_elevated()) return false;
    run_netsh_cmd(L"delete rule name=\"Leo4Proxy HTTPS\"");
    run_netsh_cmd(L"delete rule name=\"Leo4Proxy HTTP\"");
    run_netsh_cmd(L"delete rule name=\"Leo4Proxy mDNS\"");
    run_netsh_cmd(L"delete rule name=\"Leo4Proxy LLMNR\"");
    return true;
}
