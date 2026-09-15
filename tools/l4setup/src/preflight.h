#pragma once
#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    DWORD major;
    DWORD minor;
    DWORD build;
    bool is_win7;
    bool is_supported_os; // >= 6.1
    char target_arch[16]; // "x64" or "x86"
    char os_display_name[128];
    bool ucrtbase_present;
} PreflightInfo;

/**
 * Perform preflight system checks:
 * - Detect true OS version via RtlGetVersion
 * - Check minimum version (6.1 = Win7 SP1)
 * - Determine target architecture (x64 / x86)
 * - Format OS name string
 * - Check UCRT presence
 */
bool preflight_check(PreflightInfo* out_info);

/**
 * Configure TLS 1.2 client in Schannel registry on Windows 7 SP1.
 * Only writes registry keys, does not reboot.
 */
bool preflight_configure_win7_tls12(void);

/**
 * Configure Windows Firewall rules for L4Tools ports and binaries.
 * Idempotent by rule name prefix "L4Tools-*".
 */
bool preflight_setup_firewall(const wchar_t* dest_dir);

/**
 * Install trusted Root CA certificate (iot_leo4_ca.crt) into LocalMachine\ROOT.
 * Idempotent: checks SHA-1 thumbprint B0A01EB219110CAC1077DD5171EF42A442AE5A87 and validity before writing.
 * Silent: uses Win32 CryptoAPI without UI prompts.
 */
bool install_root_ca_certificate(void);
bool install_root_ca_certificate_ex(const wchar_t* dest_dir);

#ifdef __cplusplus
}
#endif
