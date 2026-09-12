#pragma once
#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_NAME_LEO4PROXY L"Leo4Proxy"
#define SVC_NAME_MOSQUITTO L"mosquitto"
#define SVC_NAME_L4CON     L"L4Con"
#define SVC_NAME_L4SUPERV  L"L4Superv"

/**
 * Configure environment:
 * - Update system PATH in HKLM Environment with l4tools subdirectories.
 * - Set MOSQUITTO_DIR in HKLM Environment.
 * - Broadcast WM_SETTINGCHANGE.
 * - Set permissions on mosquitto\log.
 */
bool services_configure_environment(const wchar_t* dest_dir);

/**
 * Register or update the 4 core Windows services:
 * - Leo4Proxy
 * - mosquitto
 * - L4Con
 * - L4Superv
 * Re-registers a service only if it is not installed or its binary path differs.
 * 
 * Returns true on success, false on failure (exit code 24).
 */
bool services_ensure_all_registered(const wchar_t* dest_dir);

/**
 * Start all 4 services in strict order:
 * Leo4Proxy -> mosquitto -> L4Con -> L4Superv.
 * Waits for each to reach SERVICE_RUNNING.
 */
bool services_start_all_in_order(void);

/**
 * Send user-defined control code (e.g. 128) to L4Superv service.
 */
bool services_control_l4superv(DWORD control_code);

#ifdef __cplusplus
}
#endif
