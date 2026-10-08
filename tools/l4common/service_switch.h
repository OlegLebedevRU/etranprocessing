#pragma once
#include "release.h"
/* Caller must persist this plan before apply. No passwords, pointers or handles.
 * Updating a legacy C:\l4tools service is deliberately unsupported. */
typedef struct {
    L4Layout layout;
    wchar_t service[32];
    L4ServiceInventory before;
    wchar_t after[2048];
    ULONGLONG size;
    BYTE sha256[32];
    ULONGLONG before_size;
    BYTE before_sha256[32];
} L4ServiceSwitch;
/* Read-only plan construction; entire target inventory must already be verified.
 * arguments=NULL preserves the exact old argument suffix. Explicit arguments
 * are prepared by the component adapter (e.g. ProgramData Mosquitto config). */
bool l4_service_switch_plan(const L4Layout* layout, const wchar_t* service,
                            const L4ServiceInventory* before, const wchar_t* arguments,
                            const L4ReleaseFile* files, unsigned count,
                            const L4ReleaseFile* before_files, unsigned before_count, L4ServiceSwitch* plan);
/* These APIs never stop/start/register a service or change its account/start mode.
 * Apply/rollback require STOPPED and expected account/path/start type. Rollback
 * is idempotent and refuses external changes. A false return after a write needs
 * caller recovery; never assume false means no SCM change happened. */
bool l4_service_switch_apply(const L4ServiceSwitch* plan);
bool l4_service_switch_rollback(const L4ServiceSwitch* plan);
