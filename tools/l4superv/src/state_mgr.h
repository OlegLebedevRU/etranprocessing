#pragma once
#include <windows.h>
#include <stdbool.h>
#include <time.h>
#include <stdint.h>

#define STATE_MAX_UNKNOWN_KEYS 64

typedef struct {
    char installed_path[MAX_PATH];
    DWORD runtime_pid;
    char runtime_exe[MAX_PATH];
    char config_path[MAX_PATH];
    char log_path[MAX_PATH];
    bool path_match;
    char status[32]; // "RUNNING", "STOPPED", "NOT_INSTALLED", "MISMATCH"
} L4ServiceState;

typedef struct {
    char key[64];
    char* raw_value;
} L4UnknownKey;

typedef struct {
    char status[32];            // "standby", "active", "error"
    char sn[64];                // Device serial number
    char thumbprint[64];        // Certificate thumbprint (hex)
    char not_after[64];         // Certificate expiration date
    char hw_fingerprint[128];   // Hardware fingerprint
    char installer_base_path[MAX_PATH];
    char installed_version[64]; // SemVer string
    char installer_summary_path[MAX_PATH];
    char last_cert_state[32];   // "valid", "expiring", "broken", "absent", etc.
    L4ServiceState svc_mosquitto;
    L4ServiceState svc_leo4proxy;
    L4ServiceState svc_l4con;
    L4ServiceState svc_l4superv;
    time_t last_check;
    time_t updated_at;
    L4UnknownKey unknown_keys[STATE_MAX_UNKNOWN_KEYS];
    int num_unknown_keys;
} L4State;

/**
 * Initialize state structure with empty/standby values.
 */
void state_init(L4State* state);

/**
 * Free any dynamically allocated memory inside state (e.g. unknown keys).
 */
void state_cleanup(L4State* state);

/**
 * Populate runtime service statuses into state.
 */
void state_update_services(const wchar_t* base_path, L4State* state);

/**
 * Load state from state.json in base_path.
 */
bool state_load(const wchar_t* base_path, L4State* out_state);

/**
 * Save state to state.json in base_path.
 */
bool state_save(const wchar_t* base_path, const L4State* state);
