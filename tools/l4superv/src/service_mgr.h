#pragma once
#include <windows.h>
#include <stdbool.h>
#include "state_mgr.h"

#define SVC_NAME_LEO4PROXY L"Leo4Proxy"
#define SVC_NAME_MOSQUITTO L"mosquitto"
#define SVC_NAME_L4CON     L"L4Con"
#define SVC_NAME_L4SUPERV  L"L4Superv"

/**
 * Set permissive read/write permissions on directory (Full Control for Everyone, Users, System, Admin).
 */
bool svc_set_dir_permissions(const wchar_t* dir_path);

/**
 * Retrieve registered binary path of a service from Windows SCM.
 */
bool svc_get_binary_path(const wchar_t* svc_name, wchar_t* out_bin_path, size_t out_size);

/**
 * Retrieve full filesystem path of the executable for a given PID.
 */
bool svc_get_process_image(DWORD pid, wchar_t* out_exe_path, size_t out_size);

/**
 * Inspect a service and populate its runtime state.
 */
bool svc_inspect(const wchar_t* svc_name,
                 const wchar_t* expected_base_path,
                 const wchar_t* relative_bin,
                 const wchar_t* relative_conf,
                 const wchar_t* relative_log,
                 L4ServiceState* out_state);

/**
 * Check if a service exists with a path outside expected_base_path, and if so, stop and remove it.
 */
bool svc_cleanup_foreign(const wchar_t* svc_name, const wchar_t* expected_base_path);

/**
 * Install a service in Windows Service Control Manager.
 */
bool svc_install(const wchar_t* svc_name,
                 const wchar_t* display_name,
                 const wchar_t* bin_path,
                 DWORD start_type,
                 const wchar_t* description,
                 const wchar_t* dependencies);

/**
 * Uninstall a service from Windows SCM.
 */
bool svc_uninstall(const wchar_t* svc_name);

/**
 * Start a service.
 */
bool svc_start(const wchar_t* svc_name);

/**
 * Stop a service.
 */
bool svc_stop(const wchar_t* svc_name);

/**
 * Stop a service and terminate its process if it does not exit within timeout.
 */
bool svc_stop_and_kill(const wchar_t* svc_name);

/**
 * Restart a service (stop then start).
 */
bool svc_restart(const wchar_t* svc_name);

/**
 * Get service state (e.g. SERVICE_RUNNING, SERVICE_STOPPED) and process ID.
 */
bool svc_get_status(const wchar_t* svc_name, DWORD* out_state, DWORD* out_pid);

/**
 * Check if a service is currently running.
 */
bool svc_is_running(const wchar_t* svc_name);

/**
 * Check if a service exists in SCM.
 */
bool svc_exists(const wchar_t* svc_name);

/**
 * Ensure all 4 services are installed in SCM with correct paths and started.
 */
bool svc_ensure_all_installed_and_running(const wchar_t* base_path);
