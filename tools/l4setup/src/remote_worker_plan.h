#pragma once
#include "remote_prepare.h"
#define SETUP_REMOTE_LAUNCHERS 9u
typedef struct {ULONGLONG supervisor,broker,acl;ULONGLONG launchers[SETUP_REMOTE_LAUNCHERS];} SetupRemoteConfigProposals;
typedef struct SetupRemoteWorkerPlan SetupRemoteWorkerPlan;
/* Build/reload signed operation64 for a FUTURE worker; no worker spawn, marker,
 * recovery arm, readiness, stop or apply. Configs are three existing original
 * journal proposals for fixed paths plus all nine launcher pointers, never
 * defaults/arguments from RPC.
 * Source borrows j and is used ONLY while j remains owned/open. Result instead
 * owns independent operation pins and copies typed source/request/UUID, retaining
 * no installed-source or journal pointer; survives later journal transfer.
 * Copied source is historical binding, NOT fresh SCM/process authority.
 * One100..600000 monotonic budget and cooperative preparation cancellation.
 * Late failure preserves already flushed63/10/64 for exact offline retry. */
bool setup_remote_worker_plan_prepare(L4Journal* journal,SetupInstalledSource* source,
    const SetupRemotePreparation* preparation,const SetupRemoteConfigProposals* configs,
    DWORD timeout,const volatile LONG* cancelled,SetupRemoteWorkerPlan** result);
void setup_remote_worker_plan_free(SetupRemoteWorkerPlan* plan);
ULONGLONG setup_remote_worker_plan_sequence(const SetupRemoteWorkerPlan* plan);
const SetupOperationPlan* setup_remote_worker_plan_operation(const SetupRemoteWorkerPlan* plan);
const L4BootstrapPlan* setup_remote_worker_plan_source(const SetupRemoteWorkerPlan* plan);
const L4RemoteRequest* setup_remote_worker_plan_request(const SetupRemoteWorkerPlan* plan);
const wchar_t* setup_remote_worker_plan_uuid(const SetupRemoteWorkerPlan* plan);
const char* setup_remote_worker_plan_arch(const SetupRemoteWorkerPlan* plan);
