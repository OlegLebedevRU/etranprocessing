#pragma once
#include "../../l4common/journal.h"
typedef struct SetupInstallPath SetupInstallPath;
/* Fixed native HKLM Environment\Path only, primary SYSTEM/no impersonation.
 * Retains original value/type/existence and key, flushes plan before mutation.
 * Adds one stable launcher directory, preserves other entries and %variables%.
 * Caller serializes synchronous calls under the original deployment lock.
 * Context is original-process only; durable installer resume remains external.
 * Exact rechecks detect external edits; Windows registry has no value CAS, so
 * unrelated writers are not serialized by the suite deployment lock. */
bool setup_path_prepare(L4Journal* journal,SetupInstallPath** output);
/* Existing locked operation only. Validates saved bytes/candidate and complete
 * intent ordering before opening the fixed registry key. Never appends a plan. */
bool setup_path_load(L4Journal* journal,ULONGLONG sequence,SetupInstallPath** output);
ULONGLONG setup_path_sequence(const SetupInstallPath* path);
bool setup_path_apply(L4Journal* journal,SetupInstallPath* path);
bool setup_path_rollback(L4Journal* journal,SetupInstallPath* path);
void setup_path_free(SetupInstallPath* path);
