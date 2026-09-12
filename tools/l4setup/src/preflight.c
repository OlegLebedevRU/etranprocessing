#include "preflight.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef LONG (WINAPI *RtlGetVersionFunc)(PRTL_OSVERSIONINFOW);

static bool run_silent_cmd(const wchar_t* cmd_line) {
    if (!cmd_line) return false;

    wchar_t cmd_buf[1024];
    wcsncpy_s(cmd_buf, 1024, cmd_line, _TRUNCATE);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessW(NULL, cmd_buf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return false;
    }

    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (exit_code == 0);
}

bool preflight_check(PreflightInfo* out_info) {
    if (!out_info) return false;
    memset(out_info, 0, sizeof(PreflightInfo));

    // 1. Get exact OS version via RtlGetVersion
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFunc fnRtlGetVersion = hNtdll ? (RtlGetVersionFunc)GetProcAddress(hNtdll, "RtlGetVersion") : NULL;

    RTL_OSVERSIONINFOW rovi;
    memset(&rovi, 0, sizeof(rovi));
    rovi.dwOSVersionInfoSize = sizeof(rovi);

    if (fnRtlGetVersion && fnRtlGetVersion(&rovi) == 0) {
        out_info->major = rovi.dwMajorVersion;
        out_info->minor = rovi.dwMinorVersion;
        out_info->build = rovi.dwBuildNumber;
    } else {
        // Fallback
        OSVERSIONINFOW ovi;
        memset(&ovi, 0, sizeof(ovi));
        ovi.dwOSVersionInfoSize = sizeof(ovi);
#pragma warning(suppress : 4996)
        if (GetVersionExW(&ovi)) {
            out_info->major = ovi.dwMajorVersion;
            out_info->minor = ovi.dwMinorVersion;
            out_info->build = ovi.dwBuildNumber;
        }
    }

    // Supported OS: Windows 7 SP1+ (>= 6.1)
    if (out_info->major > 6 || (out_info->major == 6 && out_info->minor >= 1)) {
        out_info->is_supported_os = true;
    } else {
        out_info->is_supported_os = false;
    }

    if (out_info->major == 6 && out_info->minor == 1) {
        out_info->is_win7 = true;
    }

    // 2. Determine target architecture
    BOOL is_wow64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &is_wow64);
    if (is_wow64) {
        strcpy_s(out_info->target_arch, sizeof(out_info->target_arch), "x64");
    } else {
        SYSTEM_INFO si;
        GetNativeSystemInfo(&si);
        if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
            strcpy_s(out_info->target_arch, sizeof(out_info->target_arch), "x64");
        } else {
            strcpy_s(out_info->target_arch, sizeof(out_info->target_arch), "x86");
        }
    }

    // 3. Format OS display name from Registry ProductName
    char product_name[128] = "Windows";
    HKEY hKey = NULL;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwType = REG_SZ;
        DWORD dwSize = sizeof(product_name) - 1;
        if (RegQueryValueExA(hKey, "ProductName", NULL, &dwType, (LPBYTE)product_name, &dwSize) == ERROR_SUCCESS) {
            product_name[dwSize] = '\0';
        }
        RegCloseKey(hKey);
    }

    snprintf(out_info->os_display_name, sizeof(out_info->os_display_name),
             "%s (%u.%u.%u) %s",
             product_name, out_info->major, out_info->minor, out_info->build, out_info->target_arch);

    // 4. Check UCRT
    HMODULE hUcrt = LoadLibraryExW(L"ucrtbase.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUcrt) {
        out_info->ucrtbase_present = true;
        FreeLibrary(hUcrt);
    } else {
        out_info->ucrtbase_present = false;
    }

    return true;
}

bool preflight_configure_win7_tls12(void) {
    HKEY hKey = NULL;
    const wchar_t* subkey = L"SYSTEM\\CurrentControlSet\\Control\\SecurityProviders\\SCHANNEL\\Protocols\\TLS 1.2\\Client";
    LONG res = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        subkey,
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        NULL,
        &hKey,
        NULL
    );

    if (res != ERROR_SUCCESS) {
        log_warn("Failed to create TLS 1.2 registry key (error %ld)", res);
        return false;
    }

    DWORD dwDisabledByDefault = 0;
    DWORD dwEnabled = 1;
    RegSetValueExW(hKey, L"DisabledByDefault", 0, REG_DWORD, (const BYTE*)&dwDisabledByDefault, sizeof(DWORD));
    RegSetValueExW(hKey, L"Enabled", 0, REG_DWORD, (const BYTE*)&dwEnabled, sizeof(DWORD));
    RegCloseKey(hKey);

    log_info("Configured Windows 7 Schannel TLS 1.2 Client registry settings (DisabledByDefault=0, Enabled=1)");
    return true;
}

bool preflight_setup_firewall(const wchar_t* dest_dir) {
    if (!dest_dir) return false;

    log_info("Setting up Windows Firewall rules (prefix: L4Tools-*)...");

    // Ports
    struct {
        const wchar_t* name;
        int port;
    } ports[] = {
        { L"L4Tools-Mosquitto-Port", 1883 },
        { L"L4Tools-Leo4Proxy-Port",  18443 },
        { L"L4Tools-Leo4Proxy-Mqtt",  18883 }
    };

    wchar_t cmd[1024];
    for (int i = 0; i < (int)(sizeof(ports)/sizeof(ports[0])); i++) {
        // Delete existing rule first for idempotency
        swprintf_s(cmd, 1024, L"netsh.exe advfirewall firewall delete rule name=\"%ls\"", ports[i].name);
        run_silent_cmd(cmd);

        // Add allow rule
        swprintf_s(cmd, 1024, L"netsh.exe advfirewall firewall add rule name=\"%ls\" dir=in action=allow protocol=TCP localport=%d profile=any",
                   ports[i].name, ports[i].port);
        run_silent_cmd(cmd);
    }

    // Binary rules
    struct {
        const wchar_t* name;
        const wchar_t* rel_path;
    } bins[] = {
        { L"L4Tools-Leo4Proxy", L"leo4proxy\\leo4proxy.exe" },
        { L"L4Tools-Mosquitto", L"mosquitto\\mosquitto.exe" },
        { L"L4Tools-L4Con",     L"l4con\\l4con.exe" },
        { L"L4Tools-L4Superv",  L"l4superv\\l4superv.exe" },
        { L"L4Tools-L4Desk",    L"l4desk\\l4desk.exe" }
    };

    for (int i = 0; i < (int)(sizeof(bins)/sizeof(bins[0])); i++) {
        wchar_t full_exe[MAX_PATH];
        swprintf_s(full_exe, MAX_PATH, L"%ls\\%ls", dest_dir, bins[i].rel_path);

        swprintf_s(cmd, 1024, L"netsh.exe advfirewall firewall delete rule name=\"%ls\"", bins[i].name);
        run_silent_cmd(cmd);

        swprintf_s(cmd, 1024, L"netsh.exe advfirewall firewall add rule name=\"%ls\" dir=in action=allow program=\"%ls\" enable=yes profile=any",
                   bins[i].name, full_exe);
        run_silent_cmd(cmd);
    }

    log_info("Windows Firewall rules configured successfully");
    return true;
}
