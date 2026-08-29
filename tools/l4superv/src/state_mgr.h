#pragma once
#include <windows.h>
#include <stdbool.h>
#include <time.h>

typedef struct {
    char   status[32];          // "standby", "active", "error"
    char   sn[64];              // Device serial number
    char   thumbprint[64];      // Certificate thumbprint (hex)
    char   not_after[64];       // Certificate expiration date
    char   hw_fingerprint[128]; // Hardware fingerprint
    time_t last_check;
    time_t updated_at;
} L4State;

/**
 * Initialize state structure with empty/standby values.
 */
void state_init(L4State* state);

/**
 * Load state from state.json in base_path.
 */
bool state_load(const wchar_t* base_path, L4State* out_state);

/**
 * Save state to state.json in base_path.
 */
bool state_save(const wchar_t* base_path, const L4State* state);
