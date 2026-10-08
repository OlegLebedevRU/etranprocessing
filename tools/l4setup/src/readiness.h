#pragma once
#include "../../l4common/bootstrap.h"
#include "../../l4common/access.h"
#include "../../l4common/update_state.h"
typedef struct {int proxy_port,broker_port;} L4Readiness;
/* Real callbacks, explicit caller budgets. No installation or service changes. */
bool setup_readiness_checks(L4Readiness* context,const DWORD service_ms[4],DWORD barrier_ms,L4BootstrapChecks* checks);
/* Live pre-stop CHECK ONLY, under original journal lock. Current source plan
 * must independently be owner-authenticated; this API is not stop permission.
 * Verifies old config snapshots, actor ACLs, current four service epochs,
 * application probes, mandatory fresh REQ/RSP+EVT/EVA and final local rechecks.
 * Total budget1..300000ms; individual probes clipped to it. No config/SCM writes,
 * no second MQTT client, no durable/cached success reusable after restart. */
bool setup_readiness_preflight(L4Journal* journal,const L4BootstrapPlan* source,
    const L4AccessActors* actors,const ULONGLONG* config_sequences,unsigned count,
    const L4BootstrapChecks* checks,DWORD timeout_ms);

/* In-memory original process epochs; never replace after active marker/restart. */
typedef struct {DWORD pid;FILETIME created;} L4ReadinessEpoch;
typedef struct {L4ReadinessEpoch services[4];} L4ReadinessSnapshot;
bool setup_readiness_capture(L4Journal* journal,const L4BootstrapPlan* source,
    const L4AccessActors* actors,const ULONGLONG* configs,unsigned count,
    const L4BootstrapChecks* checks,DWORD timeout,L4ReadinessSnapshot* snapshot);
/* CHECK ONLY after communication update-only marker, preserving original epochs.
 * Drain con/superv, then fresh existing link barrier and repeat local checks.
 * No superv ordinary health probe during window1; no stop/write/recovery here. */
bool setup_readiness_quiescence(L4Journal* journal,const L4BootstrapPlan* source,
    const L4AccessActors* actors,const ULONGLONG* configs,unsigned count,
    const L4BootstrapChecks* checks,const L4ReadinessSnapshot* original,
    const L4UpdateState* expected,DWORD timeout);

/* Fresh selected certificate from exact original proxy epoch/listener ownership.
 * Source is signed and independently verified by caller; no fresh metadata. */
bool setup_readiness_proxy_identity(const L4BootstrapPlan* source,const L4ReadinessSnapshot* original,DWORD timeout,char thumbprint[64]);
