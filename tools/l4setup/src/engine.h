#pragma once
#include <windows.h>
#include <stdbool.h>
#include "cli.h"
#include "summary.h"
#include "services.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SETUP_PHASE_CHECK = 0,
    SETUP_PHASE_PREPARE,
    SETUP_PHASE_STOP,
    SETUP_PHASE_UPDATE,
    SETUP_PHASE_START,
    SETUP_PHASE_VERIFY,
    SETUP_PHASE_FINISH
} SetupPhase;

typedef enum {
    OP_INSTALL,
    OP_UPGRADE,
    OP_REPAIR,
    OP_VERIFY
} SetupOperationType;

typedef struct SetupContext {
    CliOptions* opts;
    HINSTANCE hInstance;
    HWND hWndParent; // NULL if unattended
    bool is_interactive;

    // Detected during Phase 1 Check
    SetupOperationType op_type;
    char installed_version[32];
    char target_version[32];
    char os_name[128];
    char target_arch[16];
    char sn[64];
    bool downgrade_blocked;
    bool disk_space_ok;
    bool pending_reboot;

    // Cancellation and Retry state
    volatile bool cancel_requested;
    volatile bool retry_requested;

    // Callbacks
    void (*on_phase_change)(SetupPhase phase, const char* phase_name, const char* status_text, void* user_data);
    void (*on_service_status)(int service_idx, const wchar_t* svc_name, ServiceLifecycleStatus status, DWORD elapsed_sec, const char* notice, void* user_data);
    void (*on_notice)(const char* notice_text, void* user_data);
    void (*on_log_line)(const char* line, void* user_data);
    void (*on_pipeline_finish)(int exit_code, const char* final_status, void* user_data);
    void* user_data;

    // Results
    InstallSummaryData summary;
    int final_exit_code;
    HANDLE hMutex;
} SetupContext;

/**
 * Perform Phase 1: Check (preflight, version detection, single instance mutex, disk space, pending reboot).
 */
bool engine_phase_check(SetupContext* ctx);

/**
 * Run remaining phases (Prepare -> Stop -> Update -> Start -> Verify -> Finish).
 */
int engine_run_pipeline(SetupContext* ctx);

const char* engine_op_type_to_str(SetupOperationType op);
const char* engine_phase_to_str(SetupPhase phase);

#ifdef __cplusplus
}
#endif
