#pragma once
#include "remote_worker_start.h"
/* Internal ceilings, never RPC/argv/environment configuration. Preparation is
 * outside service outage. These are watchdog limits, not expected duration. */
#define SETUP_REMOTE_PREPARE_MS 600000u
#define SETUP_REMOTE_LAUNCH_MS 180000u
#define SETUP_REMOTE_COMMUNICATION_WINDOW_MS 3600000u
#define SETUP_REMOTE_SUPERVISOR_WINDOW_MS 7200000u
#define SETUP_REMOTE_RESTORE_RESERVE_MS 1800000u
#define SETUP_REMOTE_CONTROLLER_WAIT_MS 7800000u
#define SETUP_REMOTE_WORKER_STARTUP_MS 150000u
bool setup_remote_policy_at(ULONGLONG armed_utc,SetupRemoteWorkerPolicy* policy);
