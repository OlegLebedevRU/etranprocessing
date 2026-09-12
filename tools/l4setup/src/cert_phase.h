#pragma once
#include <windows.h>
#include <stdbool.h>
#include "../../l4pin/src/cert_discovery.h"
#include "cli.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_WARNINGS_COUNT 16

typedef struct {
    cert_state state;
    bool reused;
    bool reissued;
    char thumbprint[64];
    char sn[64];
    char not_after[64];
    int days_left;
    int exit_code;
    char status[32]; // "ready", "standby_waiting_pin", "ready_for_online", "ready_with_warnings", "failed"

    char warnings[MAX_WARNINGS_COUNT][64];
    int warnings_count;
} CertPhaseResult;

void cert_phase_add_warning(CertPhaseResult* res, const char* warn);

/**
 * Execute Phase 3: Certificate Discovery and PIN provisioning.
 * 
 * Returns true if installation should continue to smoke tests (exit_code 0 or 10 or 11).
 * Returns false if a fatal error occurred (exit_code 25, 26, etc.).
 */
bool cert_phase_execute(
    const wchar_t* dest_dir,
    CliOptions* cli_opts,
    HINSTANCE hInstance,
    CertPhaseResult* out_result
);

#ifdef __cplusplus
}
#endif
