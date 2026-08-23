/**
 * @file service_mgr.h
 * @brief Windows Service management and service dispatcher for Leo4Proxy.
 */

#ifndef LEO4_SERVICE_MGR_H
#define LEO4_SERVICE_MGR_H

#include "config.h"
#include <stdbool.h>

/**
 * @brief Installs or updates the Windows Service with the provided command line arguments.
 * Ensures the service is configured for auto-start and auto-recovery on failure.
 */
bool service_install_or_update(const ProxyConfig* config, int argc, char* argv[]);

/**
 * @brief Uninstalls and deletes the Windows Service.
 */
bool service_uninstall(void);

/**
 * @brief Starts the Windows Service via Service Control Manager.
 */
bool service_start(void);

/**
 * @brief Stops the Windows Service via Service Control Manager.
 */
bool service_stop(void);

/**
 * @brief Restarts the Windows Service.
 */
bool service_restart(void);

/**
 * @brief Queries and prints current Windows Service status.
 */
void service_query_status(void);

/**
 * @brief Runs the Windows Service dispatcher (invoked when launched by SCM with --service).
 */
bool service_run_dispatcher(const ProxyConfig* config);

#endif /* LEO4_SERVICE_MGR_H */
