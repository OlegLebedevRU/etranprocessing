# Controller worker launch composition — 2026-10-08

## Scope and authority

Second of two authorized l4update steps, shared working tree, no commit or live
deployment. Owner: l4setup controller launch composition and native build gates.
Producer → consumer: authenticated original92/93 + signed operation64 and
supervisor template → exact independently owned worker Job + recovery66/task67
→ immutable communication70 → original-journal transfer68. The worker consumes
69 using the separate strict entry/helper binding described in
[the first-step packet](2026-10-08-worker-helper-binding.md).

No RPC parameters, downloaded commands, user token fallback or caller-selected
engine. The actual shared `setup_worker_compiled_engine()` must have both real
callbacks BEFORE descriptor admission, helper receipt or Job creation. It still
returns NULL in production; main remains closed. No helper source/binary change,
MQTT client, backend, API, service stop/apply, signing or publication.

## Implemented

- `tools/l4setup/src/remote_worker_start.c/h`: owned descriptor with fixed
  authenticated source installer path, original UUID, source version and bundle
  architecture. Child argv is exactly `--update-worker --source-version ...
  --operation ... --arch ...`; directory is its fixed versioned PF setup parent.
- Parent original ACK93 PID/birth, request92, installer size/hash, owner-root
  identity, strict publisher signature and SYSTEM controller SCM identity are
  freshly checked. Source inventory verification and controller epoch are
  repeated after expensive trust checks. Helper receipt, self/ancestor fence and
  supervisor/operation pins remain held until explicit owner cleanup.
- Environment contains only sorted PATH/SystemRoot/WINDIR from native Windows
  directory facts and fixed PF launchers. It never copies an inherited block.
- Typed policy preflights native communication budget, separated deadlines,
  overhead exactly 2000..60000, recovery and cleanup bounds. Explicit monotonic
  checkpoints and cancellation occur before publication-sensitive stages.
- Trusted composition uses `l4_worker_start`, exact started-template verification
  before communication, `setup_update_prepare_communication`, exact verification
  after70, and only then `l4_worker_transfer`. Success consumes the journal;
  source verification is never invoked after handoff. No borrowed source/journal
  or worker-plan pointer is retained in the descriptor.
- Any failed startup/late recheck cancels only the owned Job and reports cleanup
  failure separately. Original journal remains owned on transfer failure. Armed
  recovery/task and immutable records are preserved; no task deletion, marker
  clearing, false READY or terminal update success. Once startup was attempted,
  the descriptor cannot retry with a new PID.
- Root supplied narrow `setup_supervisor_template_verify_started`: exact66
  template/actual worker, exact67 canonical XML plus fresh scheduler audit,
  iff selected phase one70 with held file/journal/signed64/config binding; strict
  order and no68/window/apply. Original pre-start verification remains strict.
  Root also supplied read-only communication journal verification.
- Build source and focused fixture integrated into existing native scripts.

## Verified

Final `tools/l4setup/build.cmd all`: exit0, /MT x86+x64. Default installer is
byte-identical to x86. Final focused native gates on BOTH architectures:

| Gate | Passed | Failures | Evidence boundary |
|---|---:|---:|---|
| Controller composition | 197 | 0 | Source, signature, receipt, task/start callbacks modeled; native process epoch/environment facts |
| Worker entry/helper binding | 526 | 0 | Actual file/ACL/CNG/journal; admission/SCM/publisher modeled |
| Supervisor template | 1085 | 0 | Actual process/config SD/codec; SCM/token/signatures modeled |
| Worker journal handoff | 202 | 0 | Actual separate child/Job/locks/ticket/receipt; task audit modeled |
| Communication immutable plan | 2514 | 0 | Actual files/ACL/pins/epochs; source signatures modeled |

The handoff child also checks the new exact helper size/hash admission fields;
the aggregate parent harness still reports202. No broader full suite repeated:
these are the changed composition gates and required unified build.

Composition covers missing/incomplete compiled engine before effects, stale
source/root/publisher/ACK epoch, template failure, distinct pre/post70 failures,
late cancellation/deadline, original journal retention, transfer/cleanup errors,
fixed command/directory and minimal environment, invalid task overhead and
insufficient separated deadline reserve.

## Evidence and cleanup

Durable ignored local evidence under
`tools/dist/.release/evidence/worker-launch-20261008/`:
`build-all.log`, `focused-x86.log`, `focused-x64.log`, `result.json`.
All final commands exit0. Frozen helper x86/x64 hashes equal the approved values
in the preceding bootstrap packet. No secrets read or included in evidence.
All build/test sessions exited; own three temporary runner scripts removed.
Focused ignored binaries/objects retained for diagnosis; no unrelated cleanup.

## Remaining boundary

No actual signed installed-source/strict receipt/Task Scheduler/transport launch
composition exercised against the terminal, and no real forward apply engine
exists. Current production capability check refuses before Job/receipt admission.
Real signed helper first-seal, full executor with recovery/result authority,
watchdog acceptance, real channel barriers and installed-source integration remain
separate gates. Original result94 is pre-start/pre-stop authority and cannot be
used to invent a terminal outcome after recovery66/task67 or communication70.
Successful handoff is technical journal ownership, not update success/readiness.
Caller must retain descriptor/Job until its genuine terminal decision; explicit
close is owned-Job cancellation, not rollback completion.

Parent owns shared component/index/main-flow updates; link this packet there.
