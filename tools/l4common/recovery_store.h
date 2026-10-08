#pragma once
#include "recovery_plan.h"
#include "journal.h"
typedef struct L4RecoveryGuard L4RecoveryGuard;
/* Trusted producer under deployment.lock: source signatures/config/owned worker
 * Job (private UUID name, kill-on-close, no breakaway), original supervisor epoch
 * and quiescence must already be verified by controller. Actual worker handle binds epoch.
 * Immutable exact retry; never repairs/adopts a different existing plan. */
bool l4_recovery_prepare(L4Journal* journal,const L4RecoveryPlan* plan,HANDLE worker);
/* Independent reader/decision lock. Does NOT acquire deployment.lock. Fixed
 * original UUID directory; no creation of a missing plan/lock or portable CLI
 * override. Keep guard only for decision publication; RELEASE BEFORE stopping
 * worker or obtaining deployment.lock, preventing worker/helper deadlock. */
bool l4_recovery_open(const L4Layout* roots,const wchar_t* operation,DWORD timeout,L4RecoveryGuard** guard);
void l4_recovery_close(L4RecoveryGuard* guard);
/* Retains immutable plan/file/parent pins across rollback work. No state API
 * while released; reacquire only to save the verified result. */
void l4_recovery_release(L4RecoveryGuard* guard);
bool l4_recovery_relock(L4RecoveryGuard* guard,DWORD timeout);
const L4RecoveryPlan* l4_recovery_plan(const L4RecoveryGuard* guard);
bool l4_recovery_action(L4RecoveryGuard* guard,ULONGLONG now,bool boot,L4RecoveryAction* action);
bool l4_recovery_begin(L4RecoveryGuard* guard,ULONGLONG now,bool boot);
/* Storage arbitration only, NOT proof of restored SCM/config/health. Controller
 * verifies those before calling. COMMITTED is the owner's DONE arbitration:
 * either verified target completion or verified whole-old restoration. It is
 * not an installed-suite success receipt; target success requires separate102,
 * restored failure requires its own outcome. Never use DONE for an unconfirmed
 * barrier/partial restore. COMMITTED allowed only before deadline and
 * before STARTED; RESTORED/FAILED only after durable STARTED. Exact retry safe. */
bool l4_recovery_finish(L4RecoveryGuard* guard,L4RecoveryStatus status,DWORD error,ULONGLONG now);
