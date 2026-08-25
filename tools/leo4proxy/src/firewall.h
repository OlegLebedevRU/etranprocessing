/**
 * @file firewall.h
 * @brief Windows Defender Firewall rule automation and UAC elevation helpers for Leo4Proxy.
 */

#ifndef LEO4_FIREWALL_H
#define LEO4_FIREWALL_H

#include "config.h"
#include <stdbool.h>

/**
 * @brief Checks if the current process is running with Administrator / Elevated privileges.
 */
bool firewall_is_elevated(void);

/**
 * @brief Attempts to restart the current process with Administrator elevation via UAC prompt.
 * @param argc Command line argument count
 * @param argv Command line arguments
 * @return true if restart initiated (caller should exit), false if failed or cancelled.
 */
bool firewall_elevate_self(int argc, char* argv[]);

/**
 * @brief Ensures necessary inbound firewall rules are registered for reverse HTTPS, mDNS and LLMNR.
 * @param config Configuration containing ports and settings
 * @param exePath Path to leo4proxy.exe (or NULL to auto-detect current module path)
 * @return true if successful or not needed, false if elevation missing or command failed.
 */
bool firewall_ensure_rules(const ProxyConfig* config, const char* exePath);

/**
 * @brief Removes registered Leo4Proxy firewall rules (e.g. on service uninstall).
 */
bool firewall_remove_rules(void);

#endif /* LEO4_FIREWALL_H */
