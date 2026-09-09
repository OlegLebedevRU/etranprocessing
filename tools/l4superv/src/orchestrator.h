#pragma once
#include "config.h"
#include "state_mgr.h"
#include <stdbool.h>

/**
 * Execute a single orchestration step (poll proxy, check HW, manage configs and services).
 * Returns true if step completed successfully.
 */
bool orchestrator_step(const L4SupervConfig* cfg, L4State* state, bool* p_action_taken);

/**
 * Main orchestration loop for l4superv (polling + watchdog + state transitions).
 */
void orchestrator_run_loop(const L4SupervConfig* cfg, volatile bool* p_stop_flag);

/**
 * Query current runtime status of l4desk user session process.
 */
bool orchestrator_get_l4desk_status(DWORD* out_pid, DWORD* out_session);
