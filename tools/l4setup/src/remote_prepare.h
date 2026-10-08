#pragma once
#include "installed_source.h"
#include "update_metadata.h"
#include "../../l4common/remote_host.h"
typedef struct SetupRemotePreparation SetupRemotePreparation;
/* Controller execute stage only, after durable original92/93. profile comes
 * exclusively from l4_platform_current, never argv/RPC or
 * downloaded metadata. Authenticated source owns version/root hash/arch/SCM.
 * One total monotonic budget includes transport and offline checks. No config,
 * SCM, marker, worker or readiness mutation. Cooperative prep-only cancellation.
 * Result holds all package/admission pins; it is not permission to stop/apply. */
bool setup_remote_prepare(L4Journal* journal,SetupInstalledSource* source,
    DWORD timeout,const volatile LONG* cancelled,
    SetupRemotePreparation** result);
void setup_remote_preparation_free(SetupRemotePreparation* preparation);
const SetupPreparedPlan* setup_remote_preparation_packages(const SetupRemotePreparation* preparation);
ULONGLONG setup_remote_preparation_route_sequence(const SetupRemotePreparation* preparation);
ULONGLONG setup_remote_preparation_packages_sequence(const SetupRemotePreparation* preparation);
/* Original protected operation binding only; not fresh source/package authority. */
bool setup_remote_preparation_matches(L4Journal* journal,const SetupRemotePreparation* preparation);
