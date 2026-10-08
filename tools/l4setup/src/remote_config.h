#pragma once
#include "remote_worker_plan.h"
/* Original owner journal lock + held authenticated source + prepared signed target.
 * Fields: supervisor JSON, standard broker config, broker ACL, nine fixed CLI pointers. Saves proposals only;
 * existing custom/changed bytes refuse, current safe SD is preserved verbatim.
 * No caller terminal identity, apply, migration, ACL grant or SCM mutation. */
bool setup_remote_config_prepare(L4Journal* journal,SetupInstalledSource* source,
    const SetupManifest* target,SetupRemoteConfigProposals* proposals);
