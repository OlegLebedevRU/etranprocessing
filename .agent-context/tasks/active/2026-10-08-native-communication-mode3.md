# Native communication mode3 registration — 2026-10-08

## Intake

User authorized continuation toward real RPC7031 test readiness. Scope owner:
independent supervisor communication watch, native restart registration and fixed
read-only IPC recovery handler. Working tree only. Native intake/workflow applied;
no MQTT client, installed service, frozen helper, signing, publication, remote RPC
or forward executor activation. Setup/common production files not modified.

## Implemented

The independent monitor and native boot reconciliation were already wired into
the supervisor owner thread. Mode3 previously always returned NOT_SUPPORTED,
while `startup_checked` became true even after a failed marker read.

- `tools/l4superv/src/communication_watch.c`: private static native registration
  contains the actual protected boot permit, boot signal profile and synchronous
  native recovery executor callbacks. The owned watch retains its exact pointer;
  no public setter, downloaded policy, RPC flag or persisted tests-passed bit.
  Startup reconciliation executes this same registered native callback bundle.
- `startup_checked` now requires an actual valid startup snapshot. An active
  original-process plan must match its exact protected plan/epoch/generation.
  Missing/corrupt startup cannot subsequently auto-admit after the file is fixed.
  A restarted foreign original owner retains exclusive boot recovery ownership
  and cannot claim original-monitor mode3 readiness.
- Mode3 now invokes the existing native retained-owner proof: exact registered
  boot bundle, completed valid startup snapshot, live independent monitor and
  signal/profile pins, live watch thread, actual immutable plan/state/deadline,
  original supervisor epoch, actual worker/Job and WAIT decision, and fresh native
  SCM LocalSystem/AUTO_START/crash-only restart profile before and after proof.
  No plan discovery, monitor creation, deadline renewal, network request, marker
  write or service mutation occurs in the query.
- `communication_watch.h` describes technical protection semantics; orchestrator
  startup log reflects the real native reconciliation instead of the old closed
  admission placeholder. Its fixed local v2 recovery handler already calls this
  query; no IPC schema or RPC contract changed.
- `tests/test_communication_watch.c`: actual protected plan/state/decision and
  owner-thread fixtures now exercise technical query success and refusal for
  copied/foreign registration pointer, unvalidated startup, bad monitor proof,
  bad SCM profile, mismatched marker/epoch/worker, cancellation, boot ownership
  and shutdown. An actually corrupted marker followed by a later clear file
  cannot resurrect startup admission.

## Validation and evidence

`tools/l4superv/build.cmd all` completed exit0 with x86+x64/default artifacts;
default supervisor is byte-identical to x86. All existing unified native gates
passed, including boot/signals/watch and supervisor routing/ACL tests.

Related focused gates:

| Gate | x86 | x64 | Boundary |
|---|---:|---:|---|
| Communication watch | 446/0 | 446/0 | Actual protected plan/state/decision/owner thread; SYSTEM, signal/executor/SCM modeled |
| Existing communication runtime/monitor | 1951/0 | 1950/0 | Actual independent monitor thread/deadline/journals/configs/decisions; SCM/images/SYSTEM/signals modeled |

Runtime check count can vary with actual polling; both processes exited0.
The unchanged real isolated SCM self-crash/restart and drift-refusal gate from
the preceding session was not repeated. This packet does not claim actual signed
supervisor restart/channel restoration, Task Scheduler/helper acceptance or a
whole remote update. Production mode3 uses native runtime observations, never
model/test results as authority.

An initial new refusal test mistakenly used valid window2 as a corrupt marker:
two fixture assertions failed. Corrected test actually changes the marker's
first byte; latest x86/x64 focused and unified gates pass. Initial failed log
retained separately, no production guard was weakened to make it pass.

Durable ignored local evidence:
`tools/dist/.release/evidence/communication-mode3-20261008/` containing
`build-all.log`, `focused-x86.log` (initial failure), `focused-x86-final.log`,
`focused-x64.log`, `runtime-x86.log`, `runtime-x64.log`, `result.json`.
No credentials or secrets in evidence. Four own temporary runner scripts removed
after all sessions exited; ignored binaries/objects retained for diagnosis.

## Remaining readiness boundary

Mode3 success proves technical protection of exactly the proposed/current
window1 operation, not application/channel READY, update success or permission
to enable the remote executor. Signed installed-source baseline, initial signed
immutable helper, actual supervised fault/restart/channel recovery acceptance
and complete forward/result flow still must pass before RPC7031 test readiness.
The production forward/remote entry remains disabled by its compiled capability.

Existing pre-marker history discovery refuses if more than256 operation folders
or multiple eligible plans exist. Query never repairs/adopts/arms a plan to bypass
this cap. Check real operation retention before acceptance on the stand.

Parent owns shared l4update card/index/main-flow updates and should link this
packet. No independent deployment, installed SCM changes or helper modifications.
