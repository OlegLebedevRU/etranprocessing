#include "preflight.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wincrypt.h>

#pragma comment(lib, "crypt32.lib")

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
    log_info("Setting up Windows Firewall rules for FFmpeg (L4Tools-FFmpeg, ports 5004/5005 UDP)...");

    // Ports
    struct {
        const wchar_t* name;
        int port;
        const wchar_t* proto;
    } ports[] = {
        { L"L4Tools-Mosquitto-Port", 1883,  L"TCP" },
        { L"L4Tools-Leo4Proxy-Port", 18443, L"TCP" },
        { L"L4Tools-Leo4Proxy-Mqtt", 18883, L"TCP" },
        { L"L4Tools-RTP-Port",        5004,  L"UDP" },
        { L"L4Tools-RTCP-Port",       5005,  L"UDP" }
    };

    wchar_t cmd[1024];
    for (int i = 0; i < (int)(sizeof(ports)/sizeof(ports[0])); i++) {
        // Delete existing rule first for idempotency
        swprintf_s(cmd, 1024, L"netsh.exe advfirewall firewall delete rule name=\"%ls\"", ports[i].name);
        run_silent_cmd(cmd);

        // Add allow rule
        swprintf_s(cmd, 1024, L"netsh.exe advfirewall firewall add rule name=\"%ls\" dir=in action=allow protocol=%ls localport=%d profile=any",
                   ports[i].name, ports[i].proto, ports[i].port);
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
        { L"L4Tools-L4Desk",    L"l4desk\\l4desk.exe" },
        { L"L4Tools-FFmpeg",    L"ffmpeg\\ffmpeg.exe" }
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

    log_info("Windows Firewall rules configured successfully (including FFmpeg and UDP 5004/5005)");
    return true;
}

static const BYTE IOT_LEO4_CA_THUMBPRINT[20] = {
    0xB0, 0xA0, 0x1E, 0xB2, 0x19, 0x11, 0x0C, 0xAC, 0x10, 0x77,
    0xDD, 0x51, 0x71, 0xEF, 0x42, 0xA4, 0x42, 0xAE, 0x5A, 0x87
};
static const char* IOT_LEO4_CA_THUMBPRINT_HEX = "B0A01EB219110CAC1077DD5171EF42A442AE5A87";

static const char* IOT_LEO4_CA_PEM =
    "-----BEGIN CERTIFICATE-----\n"
    "MIID3TCCAsWgAwIBAgIUU+hf9Mjnji7EW2+X6T+LQrEpt5gwDQYJKoZIhvcNAQEL\n"
    "BQAwfTELMAkGA1UEBhMCUlUxDzANBgNVBAgMBk1vc2NvdzEPMA0GA1UEBwwGTW9z\n"
    "Y293MQ0wCwYDVQQKDARMZW80MQswCQYDVQQLDAJJVDEUMBIGA1UEAwwLaW90Lmxl\n"
    "bzQucnUxGjAYBgkqhkiG9w0BCQEWC2lvdEBsZW80LnJ1MCAXDTI0MDEyNTEzNTAy\n"
    "NVoYDzIwNTQwMTE3MTM1MDI1WjB9MQswCQYDVQQGEwJSVTEPMA0GA1UECAwGTW9z\n"
    "Y293MQ8wDQYDVQQHDAZNb3Njb3cxDTALBgNVBAoMBExlbzQxCzAJBgNVBAsMAklU\n"
    "MRQwEgYDVQQDDAtpb3QubGVvNC5ydTEaMBgGCSqGSIb3DQEJARYLaW90QGxlbzQu\n"
    "cnUwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDsT/EDxcBouXpN1TpE\n"
    "ypHs0I8ypyDYwbAXezhWSNaUkOGXZ+pystZ6LUwsmJak2X+1P6X1QVjjoEFPCPUL\n"
    "yU4EBmgJcNm1b1Y1tR9oozbivLX5qHVkKIDX4fM7SA+rN0oMfyXnXa8hbvX34g8N\n"
    "8HpmhjbpdYBtY/Ys/EuhHABltu8KdtRhcopdq0q65+k47q+7daBeoeC63Jf5CUyh\n"
    "OxJlEFKdrmicxDqyh5FQH7V4uQAZBTx8rX3+G8a/og/08YRBAjtNAQaFI1zaYv/l\n"
    "Ic/g7WZaDskBJj2oi4GZixMTDkNVJygPd65278qjqhdd7oQH0w6p7rJjsd4CptCL\n"
    "aeu7AgMBAAGjUzBRMB0GA1UdDgQWBBRTy8kO9HRBPITIwok0kQ8BtLDo9zAfBgNV\n"
    "HSMEGDAWgBRTy8kO9HRBPITIwok0kQ8BtLDo9zAPBgNVHRMBAf8EBTADAQH/MA0G\n"
    "CSqGSIb3DQEBCwUAA4IBAQBlc4nASwtzSNJVx5N/TIR/bd5bP7Q398B2H5v+mTbw\n"
    "TYfs//6WJt/i7cMzLxRSQ7f2Aww55NXC4kbFBhD/jYLRUL7Fdl8Lv6xpCIzIBQze\n"
    "hzLGnASx1G++GcO6SxRqe6gj4cgo/9c5dzw2WSCNpUrHAjnb2jJX/mfo3EF0JzET\n"
    "8RWJHsct5pm5CJlhRknMdti9i13BRuEtnze3TfoiUUsIYJjzTrC56wSvjixulX9I\n"
    "qYOHwQ5vFZ6q42W4TCMSoplTVqVDaBabRCfo+k2UfUQ22NQoKSPcLHHP2m7QYp9T\n"
    "K4FInTIF8841aVPpt4ZnHf32T+o0NAUtjoG22nIsyHmF\n"
    "-----END CERTIFICATE-----\n";

static char* load_ca_pem_from_file_or_default(const wchar_t* dest_dir) {
    if (dest_dir && dest_dir[0]) {
        wchar_t ca_path[MAX_PATH];
        swprintf_s(ca_path, MAX_PATH, L"%ls\\crt\\iot_leo4_ca.crt", dest_dir);
        FILE* fp = NULL;
        if (_wfopen_s(&fp, ca_path, L"rb") == 0 && fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if (sz > 0 && sz < 65536) {
                char* buf = (char*)malloc(sz + 1);
                if (buf) {
                    size_t read_bytes = fread(buf, 1, sz, fp);
                    buf[read_bytes] = '\0';
                    fclose(fp);
                    return buf;
                }
            }
            fclose(fp);
        }
    }
    return NULL;
}

bool install_root_ca_certificate_ex(const wchar_t* dest_dir) {
    HCERTSTORE hRootStore = CertOpenStore(
        CERT_STORE_PROV_SYSTEM_W,
        0,
        0,
        CERT_SYSTEM_STORE_LOCAL_MACHINE,
        L"ROOT"
    );

    if (!hRootStore) {
        log_err("Failed to open LocalMachine\\ROOT certificate store (error 0x%08lX)", GetLastError());
        return false;
    }

    CRYPT_HASH_BLOB hashBlob;
    hashBlob.cbData = sizeof(IOT_LEO4_CA_THUMBPRINT);
    hashBlob.pbData = (BYTE*)IOT_LEO4_CA_THUMBPRINT;

    PCCERT_CONTEXT pExistingCert = CertFindCertificateInStore(
        hRootStore,
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        0,
        CERT_FIND_SHA1_HASH,
        &hashBlob,
        NULL
    );

    if (pExistingCert) {
        LONG timeValidity = CertVerifyTimeValidity(NULL, pExistingCert->pCertInfo);
        if (timeValidity == 0) {
            log_info("Root CA certificate (iot.leo4.ru) is already installed and valid.");
            CertFreeCertificateContext(pExistingCert);
            CertCloseStore(hRootStore, 0);
            return true;
        }
        log_warn("Root CA certificate found in LocalMachine\\ROOT but validity check returned %ld. Reinstalling...", timeValidity);
        CertFreeCertificateContext(pExistingCert);
    }

    char* file_pem = load_ca_pem_from_file_or_default(dest_dir);
    const char* pem_to_use = file_pem ? file_pem : IOT_LEO4_CA_PEM;

    DWORD cbDer = 0;
    if (!CryptStringToBinaryA(pem_to_use, 0, CRYPT_STRING_BASE64HEADER, NULL, &cbDer, NULL, NULL) || cbDer == 0) {
        log_err("Failed to decode Root CA PEM certificate (error 0x%08lX)", GetLastError());
        if (file_pem) free(file_pem);
        CertCloseStore(hRootStore, 0);
        return false;
    }

    BYTE* pbDer = (BYTE*)malloc(cbDer);
    if (!pbDer) {
        log_err("Out of memory allocating %lu bytes for Root CA certificate", cbDer);
        if (file_pem) free(file_pem);
        CertCloseStore(hRootStore, 0);
        return false;
    }

    if (!CryptStringToBinaryA(pem_to_use, 0, CRYPT_STRING_BASE64HEADER, pbDer, &cbDer, NULL, NULL)) {
        log_err("Failed to convert Root CA PEM to binary DER (error 0x%08lX)", GetLastError());
        free(pbDer);
        if (file_pem) free(file_pem);
        CertCloseStore(hRootStore, 0);
        return false;
    }

    if (file_pem) {
        free(file_pem);
        file_pem = NULL;
    }

    PCCERT_CONTEXT pCaCertContext = CertCreateCertificateContext(
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        pbDer,
        cbDer
    );
    free(pbDer);

    if (!pCaCertContext) {
        log_err("Failed to create certificate context for Root CA (error 0x%08lX)", GetLastError());
        CertCloseStore(hRootStore, 0);
        return false;
    }

    BOOL addRes = CertAddCertificateContextToStore(
        hRootStore,
        pCaCertContext,
        CERT_STORE_ADD_REPLACE_EXISTING,
        NULL
    );

    DWORD err = GetLastError();
    CertFreeCertificateContext(pCaCertContext);
    CertCloseStore(hRootStore, 0);

    if (!addRes) {
        log_err("Failed to add Root CA certificate to LocalMachine\\ROOT (error 0x%08lX)", err);
        return false;
    }

    log_info("Root CA certificate (iot.leo4.ru, thumbprint %s) installed successfully in LocalMachine\\ROOT.", IOT_LEO4_CA_THUMBPRINT_HEX);
    return true;
}

bool install_root_ca_certificate(void) {
    return install_root_ca_certificate_ex(NULL);
}
