#pragma once
#include <windows.h>
#include <stdbool.h>
#include "remote_policy.h"
/* Fixed native ceilings; no argv/environment/RPC override. */
#define SETUP_ACCEPTANCE_INNER_WAIT_MS (SETUP_REMOTE_PREPARE_MS+SETUP_REMOTE_LAUNCH_MS+SETUP_REMOTE_CONTROLLER_WAIT_MS+300000u)
#define SETUP_ACCEPTANCE_OUTER_WAIT_MS (SETUP_ACCEPTANCE_INNER_WAIT_MS+SETUP_REMOTE_LAUNCH_MS)
bool setup_acceptance_entry(int argc,wchar_t** argv,DWORD* result);
