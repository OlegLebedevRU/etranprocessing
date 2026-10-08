#pragma once
#include "release.h"
#include "journal.h"
#define L4_BOOTSTRAP_SERVICES 4u
typedef struct {
    const wchar_t* service;
    const wchar_t* account;
    const wchar_t* arguments;
    DWORD start_type;
} L4BootstrapProfile;
typedef struct {
    L4Layout layout;
    wchar_t services[L4_BOOTSTRAP_SERVICES][32];
    wchar_t commands[L4_BOOTSTRAP_SERVICES][2048];
    DWORD start_types[L4_BOOTSTRAP_SERVICES];
    ULONGLONG sizes[L4_BOOTSTRAP_SERVICES];
    BYTE sha256[L4_BOOTSTRAP_SERVICES][32];
} L4BootstrapPlan;
/* Read-only: complete authenticated inventory, explicit account/start/args;
 * all four services must be absent. An existing service is never adopted here.
 * LocalSystem only is the presently verified fresh-install profile. No create/start. */
bool l4_bootstrap_plan(const L4Layout* layout,const L4BootstrapProfile* profiles,unsigned profile_count,
                       const L4ReleaseFile* files,unsigned count,L4BootstrapPlan* plan);

/* Typed validation/serialization; original full-inventory verification remains
 * caller responsibility. Save preflights absence again under the deployment lock. */
bool l4_bootstrap_validate(const L4BootstrapPlan* plan);
bool l4_bootstrap_save(L4Journal* journal,const L4BootstrapPlan* plan,ULONGLONG* sequence);
bool l4_bootstrap_load(L4Journal* journal,ULONGLONG sequence,L4BootstrapPlan* plan);
/* Historical terminal state only, never a fresh health/readiness claim.
 * committed includes strict activation commit OR typed local deployment commit;
 * local_terminal distinguishes them. Both terminal outcomes forbid abort. */
bool l4_bootstrap_terminal(L4Journal* journal,ULONGLONG sequence,bool* committed,bool* aborted);
/* Local deployment is durable installation, not transport READY. No probe or
 * barrier is bypassed: all services must be owned and STOPPED at this commit. */
bool l4_bootstrap_local_commit(L4Journal* journal,ULONGLONG sequence,DWORD timeout_ms);
bool l4_bootstrap_local_terminal(L4Journal* journal,ULONGLONG sequence,bool* deployed);
/* Postcommit best-effort starts; returns diagnostic failure without undoing the
 * local deployment. Only exact owned SCM fingerprints can be started. */
bool l4_bootstrap_local_start(L4Journal* journal,ULONGLONG sequence,DWORD timeout_ms);
/* Registration only: provisional DEMAND_START, stopped, LocalSystem, own process.
 * UUID + plan sequence is atomically bound through SCM display name. No start,
 * account migration, PATH or readiness claim. Retry verifies the exact fingerprint. */
bool l4_bootstrap_register(L4Journal* journal,ULONGLONG sequence);
/* Reverse-order rollback. Only matching STOPPED services can be deleted.
 * Services started by this plan additionally require managed STOP_DONE evidence.
 * Confirm SCM absence within one total timeout; a marked deletion is not success.
 * Foreign/running services refuse, while other owned stopped services are cleaned. */
bool l4_bootstrap_rollback(L4Journal* journal,ULONGLONG sequence,DWORD timeout_ms);

/* Probes must perform fresh application-level checks, bounded by the remaining
 * timeout. The barrier must obtain REQ/RSP and fresh EVT/EVA through the existing
 * transport; never connect a second client with the production MQTT client ID.
 * No default success implementation. All callbacks and budgets are mandatory. */
typedef bool (*L4BootstrapProbe)(const L4BootstrapPlan* plan,unsigned index,DWORD remaining_ms,void* context);
typedef bool (*L4BootstrapBarrier)(const L4BootstrapPlan* plan,DWORD remaining_ms,void* context);
typedef struct {
    L4BootstrapProbe probe;
    L4BootstrapBarrier barrier;
    void* context;
    DWORD service_ms[L4_BOOTSTRAP_SERVICES]; /* Mosquitto must be 300000 ms. */
    DWORD barrier_ms;
} L4BootstrapChecks;
/* Fresh-install activation only: already registered matching provisional services.
 * Proxy -> broker -> console -> barrier -> supervisor -> final barrier.
 * Running/pending recovery requires this plan's prior durable start intent.
 * Every call repeats probes/barriers; previous READY records are never evidence.
 * No AUTO_START commit, automatic rollback, stop/kill or live CLI entry here.
 * Failure leaves owned manual services for explicit managed stop/recovery.
 * Poll/probe deadlines include SCM call time, but cannot interrupt synchronous
 * WinAPI/callback execution; an external watchdog remains required. */
bool l4_bootstrap_activate(L4Journal* journal,ULONGLONG sequence,const L4BootstrapChecks* checks);

/* Finalize fresh installation only: pinned four original activation epochs,
 * exact owned SCM fingerprint, fresh probes/barrier before and after selecting
 * the plan's AUTO/DEMAND start types. One aggregate budget, journaled per-service
 * intent/readback. Interrupted commit may resume only the same recorded epochs;
 * historical READY/COMMIT records never replace fresh checks. No service start,
 * stop, legacy adoption, signed admission or live CLI permission. Completed commit
 * forbids bootstrap abort; partial commit may be reverted by managed abort. */
bool l4_bootstrap_commit(L4Journal* journal,ULONGLONG sequence,
    const L4BootstrapChecks* checks,DWORD timeout_ms);

/* Fresh bootstrap abort: durably forbid activation, stop in reverse order and
 * confirm both STOPPED and exit of the recorded PID + creation-time identity,
 * then delete via registration rollback. One aggregate 1..300000ms polling budget.
 * Fail-fast during stop: never dismantle communications under a live supervisor.
 * No forced termination, foreign process/service adoption or restart. Retry opens
 * the same journal; missing identity for a started STOPPED service fails closed.
 * Synchronous Windows calls cannot be interrupted by this polling budget. */
bool l4_bootstrap_abort(L4Journal* journal,ULONGLONG sequence,DWORD timeout_ms);
