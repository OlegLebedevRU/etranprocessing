#pragma once
#include <windows.h>
#include <stdbool.h>

/**
 * Generate a Standby (Neutral / No-Bridge) mosquitto.conf in base_path/mosquitto/mosquitto.conf.
 */
bool mosquitto_conf_generate_standby(const wchar_t* base_path, int port);

/**
 * Generate an Active Bridge mosquitto.conf for the specified device serial number (sn).
 * If custom_tmpl_path exists, substitutes variables from template. Otherwise uses built-in template.
 */
bool mosquitto_conf_generate_active(const wchar_t* base_path,
                                    int port,
                                    const char* sn,
                                    const wchar_t* custom_tmpl_path);

/**
 * Check if mosquitto.conf exists and is currently in Standby mode (No Bridge).
 */
bool mosquitto_conf_is_standby(const wchar_t* base_path);

/**
 * Inspect existing mosquitto.conf and check if it is active with the given sn.
 * Returns true if active with matching sn, false if standby or mismatched sn or missing.
 */
bool mosquitto_conf_is_active_with_sn(const wchar_t* base_path, const char* sn);
