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

typedef enum {
    SVC_STATUS_PENDING,
    SVC_STATUS_STARTING,
    SVC_STATUS_RUNNING,
    SVC_STATUS_STOPPING,
    SVC_STATUS_STOPPED,
    SVC_STATUS_CHECKING,
    SVC_STATUS_READY,
    SVC_STATUS_FAILED
} ServiceLifecycleStatus;

typedef void (*ServiceLifecycleCallback)(
    const wchar_t* svc_name,
    ServiceLifecycleStatus status,
    DWORD elapsed_sec,
    const char* notice,
    void* user_data
);

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
 * Start a single service with query-first logic, wait hints, 30s notice, and 120s max timeout.
 * If service is already RUNNING, verifies without restarting.
 */
bool services_start_single_service(
    SC_HANDLE hSCM,
    const wchar_t* svc_name,
    DWORD timeout_sec,
    ServiceLifecycleCallback cb,
    void* user_data
);

/**
 * Stop a single service with 30s notice and 120s max timeout.
 * If stopping L4Superv, signals Global\L4Desk_Stop_<SN> event if SN is provided.
 */
bool services_stop_single_service(
    SC_HANDLE hSCM,
    const wchar_t* svc_name,
    const char* sn,
    DWORD timeout_sec,
    ServiceLifecycleCallback cb,
    void* user_data
);

/**
 * Start all 4 services in strict order:
 * Leo4Proxy -> mosquitto -> L4Con -> L4Superv.
 * Overall budget <= 480s.
 */
bool services_start_all_in_order(
    ServiceLifecycleCallback cb,
    void* user_data
);

/**
 * Stop all 4 services in reverse order:
 * L4Superv -> L4Con -> mosquitto -> Leo4Proxy.
 */
bool services_stop_all_in_order(
    const char* sn,
    ServiceLifecycleCallback cb,
    void* user_data
);

/**
 * Query current SCM status of a service.
 */
DWORD services_query_status(const wchar_t* svc_name);

/**
 * Send user-defined control code (e.g. 128) to L4Superv service.
 */
bool services_control_l4superv(DWORD control_code);

#ifdef __cplusplus
}
#endif
