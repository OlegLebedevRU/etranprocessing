#pragma once
#include <windows.h>
#include <stdbool.h>
#include "drainage.h"
#include "cert_phase.h"
#include "smoke.h"
#include "preflight.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char installer_version[32];
    char installed_version[32];
    char os[128];
    char target_arch[16];
    wchar_t dest[MAX_PATH];
    char status[32]; // "ready", "activation_required", "degraded", "failed", "cancelled"
    int exit_code;
    char phase[32]; // "check", "prepare", "stop", "update", "start", "verify", "finish"
    char error_reason[128];
    char rollback[32]; // "none", "restored", "failed"
    bool ca_root_installed;
    bool firewall_configured;
    bool payload_deployed;
    bool services_registered;
    bool reboot_recommended;
    bool requires_intervention;
    wchar_t log_path[MAX_PATH];

    char service_leo4proxy[32];
    char service_mosquitto[32];
    char service_l4con[32];
    char service_l4superv[32];
    DWORD service_start_attempts[4];

    CertPhaseResult cert;
    DrainageResult drainage;
    SmokeProbesResult probes;

    char warnings[MAX_WARNINGS_COUNT][64];
    int warnings_count;
} InstallSummaryData;

void summary_add_warning(InstallSummaryData* data, const char* warn);

/**
 * Atomically write install_summary.json (UTF-8 without BOM) via temporary file.
 */
bool summary_write_json(const InstallSummaryData* data, const wchar_t* dest_dir);

/**
 * Safely patch state.json in-place: update or insert "installed_version" and "installer_summary_path"
 * while preserving all other keys, comments and formatting.
 * Written atomically via temporary file.
 */
bool state_patch_version(const wchar_t* dest_dir, const char* version, const wchar_t* summary_path);

#ifdef __cplusplus
}
#endif
