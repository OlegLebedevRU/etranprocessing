#pragma once
#include "root_manifest.h"
#include "../../l4common/route_plan.h"
#include "../../l4common/registry_http.h"
#include "../../l4common/package_cache.h"
#include "readiness.h"
#include "../../l4common/journal_reader.h"
/* Download via Leo4Proxy, admit at current LOCAL UTC after acquisition, durably
 * save immutable route before any stop. Original journal operation owns selection.
 * Caller must verify installed current identity; no service mutation here. */
bool setup_update_acquire_route(L4Journal* journal,WORD proxy_port,DWORD timeout_ms,
    const char* current,const BYTE current_sha256[32],const char* requested,const char* arch,const char* profile,ULONGLONG* sequence);
/* Saved route pins every hop; no fresh latest/catalog request on resume. */
bool setup_update_acquire_root(const L4RoutePlan* plan,unsigned step,WORD proxy_port,DWORD timeout_ms,SetupRootManifest** result);
bool setup_update_acquire_descriptor(const L4RoutePlan* plan,unsigned step,const SetupRootManifest* root,
    const L4Layout* layout,WORD proxy_port,DWORD timeout_ms,SetupManifest** result);
/* Complete one pinned package: descriptor trust -> bounded private ZIP -> full
 * inventory/Authenticode/publisher admission. Returned cache remains pinned.
 * Does not persist a full operation plan, change configs or stop services. */
bool setup_update_prepare_package(const L4RoutePlan* plan,unsigned step,const SetupRootManifest* root,
    const L4Layout* layout,WORD proxy_port,DWORD timeout_ms,L4CachedPackage** result);

#define L4_RECORD_PREPARED_PACKAGE 61u
#define L4_RECORD_PACKAGES_COMPLETE 62u
typedef struct SetupPreparedPlan SetupPreparedPlan;
/* Preparation worker: resumes the ORIGINAL saved route, revalidates completed
 * hops offline, acquires missing ones, flushes exact signed metadata/cache pins
 * per hop, then a single completion referencing every hop. timeout is the total
 * transport budget for this invocation (100..600000ms), outside service stop.
 * Idempotent completion makes no network request. Never config/SCM mutation. */
bool setup_update_prepare_route(L4Journal* journal,ULONGLONG route_sequence,
    WORD proxy_port,DWORD timeout_ms,ULONGLONG* prepared_sequence);
/* Controller's original monotonic deadline, cooperative cancellation only in
 * preparation. Checkpoints before/after synchronous transport/admission and
 * before durable records; cannot interrupt an in-flight Win32 call. */
bool setup_update_prepare_route_controlled(L4Journal* journal,ULONGLONG route_sequence,
    WORD proxy_port,ULONGLONG deadline,const volatile LONG* cancelled,ULONGLONG* prepared_sequence);
/* All-hop offline recheck and held cache pins. Missing/corrupt metadata/cache or
 * changed immutable release refuses. Completion is PACKAGE readiness only:
 * account/config/SCM/current-source/communication gates remain mandatory. */
bool setup_update_load_prepared(L4Journal* journal,ULONGLONG prepared_sequence,SetupPreparedPlan** result);
void setup_prepared_free(SetupPreparedPlan* plan);
const L4RoutePlan* setup_prepared_route(const SetupPreparedPlan* plan);
const SetupManifest* setup_prepared_manifest(const SetupPreparedPlan* plan,unsigned step);

#define L4_RECORD_INSTALLED_SOURCE 63u
#define L4_RECORD_OPERATION_PLAN 64u
typedef struct SetupOperationPlan SetupOperationPlan;
/* Configuration sequences are already prepared component-adapter proposals;
 * this API never invents/copies defaults. Original config must still match.
 * Acquires signed source metadata via fixed Registry version, checks installed
 * signed inventory, derives/saves all four switches per hop preserving arguments,
 * account and start mode. Writes one composite plan; never config/SCM apply.
 * Retry loads the same source/composite plan, no latest/source reselection. */
bool setup_update_plan_operation(L4Journal* journal,ULONGLONG packages_sequence,
    const ULONGLONG* configs,unsigned config_count,WORD proxy_port,DWORD timeout_ms,ULONGLONG* sequence);
/* Reload revalidates packages/source/configs/current original SCM and recomputes
 * every stored switch. Pre-stop only; not an apply-progress/recovery API. */
bool setup_update_load_operation(L4Journal* journal,ULONGLONG sequence,SetupOperationPlan** result);
/* Post-switch metadata authentication only. Original inventories and configs
 * come from protected immutable64/10/20, never from current SCM. Re-derives every
 * switch against signed source/target assets. No readiness or apply permission. */
bool setup_update_load_operation_pinned(L4Journal* journal,ULONGLONG sequence,SetupOperationPlan** result);
/* Immutable historical snapshot under its NEW owner's deployment lock. Signed
 * saved metadata/inventory/cache and exact64/10/20 only; no current SCM/adoption.
 * Result cannot pass setup_operation_binding or authorize live mutation. */
bool setup_update_load_operation_snapshot(const L4JournalReader* reader,const L4Layout* roots,
    ULONGLONG sequence,SetupOperationPlan** result);
ULONGLONG setup_operation_sequence(const SetupOperationPlan* plan);
ULONGLONG setup_operation_switch_reference(const SetupOperationPlan* plan,unsigned hop,unsigned service);
/* Same original protected journal/UUID and exact immutable64 bytes. */
bool setup_operation_binding(const SetupOperationPlan* plan,L4Journal* journal,ULONGLONG sequence);
void setup_operation_free(SetupOperationPlan* plan);
const SetupManifest* setup_operation_source(const SetupOperationPlan* plan);
/* Owned read-only metadata pins; callers retain operation for their lifetime. */
const SetupRootManifest* setup_operation_root(const SetupOperationPlan* plan);
const L4RoutePlan* setup_operation_route(const SetupOperationPlan* plan);
const SetupManifest* setup_operation_target(const SetupOperationPlan* plan,unsigned hop);
const SetupRootManifest* setup_operation_target_root(const SetupOperationPlan* plan,unsigned hop);
const ULONGLONG* setup_operation_configs(const SetupOperationPlan* plan,unsigned* count);
unsigned setup_operation_hops(const SetupOperationPlan* plan);
/* Held owner-authenticated immutable source/targets only. Does not rederive
 * switches from current SCM or verify old config state after an actual switch. */
bool setup_operation_verify_images(const SetupOperationPlan* plan);
const L4ServiceSwitch* setup_operation_switch(const SetupOperationPlan* plan,unsigned hop,unsigned service);
/* Owner-authenticated composite plan -> fresh current-state preflight. No-op
 * routes require no stop and refuse this gate. No cached connection success.
 * Candidate-port checks/helper/quiescence/apply still belong to outer worker. */
bool setup_update_operation_preflight(L4Journal* journal,ULONGLONG sequence,
    const L4AccessActors* actors,const L4BootstrapChecks* checks,DWORD timeout_ms);
/* Pre-stop candidate startup only: signed target pinned throughout an owned
 * kill-on-close child; distinct exclusive loopback dynamic ports; health and
 * listener PID checks. Original RUNNING Leo4Proxy LocalSystem/session0 token is
 * captured and rechecked; caller-token fallback is forbidden. Effective certificate
 * email/thumbprint/store come from saved original SCM args; exact explicit thumb
 * must match health. Other network/media argument coverage remains an outer gate.
 * No Mosquitto connection, durable success, service/config change or stop gate. */
bool setup_update_candidate_probe(L4Journal* journal,ULONGLONG sequence,unsigned hop,
    WORD http_port,WORD mqtt_port,DWORD timeout_ms);

/* Single-use, in-memory pre-stop controller gate. Capture verified operation and
 * original four process epochs with clear-state preflight. After watchdog is
 * armed and window1 atomically published by its owner, confirm reloads signed
 * plan, binds next generation/owner/plan, drains and repeats fresh checks.
 * No stop, marker publication/clear, rollback or durable READY record here.
 * Journal must stay open/locked; no reuse/adoption after failure or restart. */
typedef struct SetupStopGate SetupStopGate;
bool setup_update_capture_stop(L4Journal* journal,ULONGLONG sequence,const L4AccessActors* actors,
    const L4BootstrapChecks* checks,DWORD timeout,SetupStopGate** result);
bool setup_update_confirm_stop(L4Journal* journal,SetupStopGate* gate,const L4AccessActors* actors,
    const L4BootstrapChecks* checks,DWORD timeout);
void setup_update_stop_gate_free(SetupStopGate* gate);
/* Exact admitted SYSTEM worker only: fresh ticket/receipt/parent/Job/task recheck,
 * reload signed operation64 and capture a NEW local gate (never parent's gate).
 * Bind immutable supervisor recovery to first signed switch and original epoch.
 * Read-only, explicit clear marker, no window publication/stop permission.
 * Independent communication recovery is STILL required before window1/confirm. */
bool setup_update_worker_capture_stop(L4Journal* journal,ULONGLONG sequence,const L4AccessActors* actors,
    const L4BootstrapChecks* checks,DWORD timeout,SetupStopGate** result);

/* Read-only pre-publication query of exact next window1 state. Native supervisor
 * must already have armed independent recovery for this deadline/worker/source.
 * No marker write, stop permission or remembered ACK: confirm queries again
 * before and after draining. Old/unarmed supervisor refuses.
 * Runtime adapter/arming is not connected yet: production gate stays closed. */
bool setup_update_watch_ready(L4Journal* journal,SetupStopGate* gate,ULONGLONG deadline_utc,DWORD timeout);
#include "../../l4common/communication_plan.h"
#include "../../l4common/worker_job.h"
/* SYSTEM controller BEFORE ticket68/receipt69, original journal and live owned
 * Job retained. Load signed source/all-hop operation, capture fresh original
 * epochs/channels and bind immutable supervisor plan + first two switches/broker
 * snapshots. Explicit immutable armed/deadline/budgets; exact retries safe.
 * Saves communication plan only: no watchdog arm, marker or service mutation. */
bool setup_update_prepare_communication(L4Journal* journal,ULONGLONG sequence,L4WorkerJob* worker,
    const L4AccessActors* actors,const L4BootstrapChecks* checks,ULONGLONG armed_utc,
    ULONGLONG deadline_utc,const L4CommunicationBudget* budget,DWORD timeout);
