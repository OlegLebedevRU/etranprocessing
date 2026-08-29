#pragma once
#include <windows.h>
#include <stdbool.h>

#define SVC_NAME_LEO4PROXY L"Leo4Proxy"
#define SVC_NAME_MOSQUITTO L"Mosquitto"
#define SVC_NAME_L4CON     L"L4Con"
#define SVC_NAME_L4SUPERV  L"L4Superv"

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
