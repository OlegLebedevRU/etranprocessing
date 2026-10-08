# Actual supervisor recovery-template prerequisite — 2026-10-08

## Task intake
- Native l4setup, producer remote_worker_plan64 → typed L4WorkerStart.recovery.
  Root owns simultaneous authenticated remote-status reading/Con consumer.
- Review worker_start/handoff/communication/recovery contracts; preserve strict
  signatures, original operation, borrowed-source lifetime and stop gates.
- No actual worker entry/payload exists in l4setup; no authenticated bootstrap
  helper receipt is currently supplied by local fresh bundle/release pipeline.
  Therefore do NOT spawn a dummy worker or imply launch/handoff readiness.
- Implement actual missing production binding, no generic engine/adapter family.
- No live installed service/config/state/task/Job, network/API, helper/signing or
  publication changes. Native /MT x86/x64 with meaningful bounded fixtures.

## Implemented
- `supervisor_template.c/h`: original locked journal + owned worker plan64,
  explicit trusted armed/deadline/recovery budget → owned L4RecoveryPlan template.
- Reload existing signed operation64 (source + all-hop inventory/pins and current
  original SCM). Exact operation-member20 `l4superv.json` proposal supplies bounded
  old/new config bytes and SD. Full config_verify precedes extraction; no defaults,
  path/command override, downloaded key, helper hash guess or new trust boundary.
- Capture exact existing L4Superv own-process RUNNING SCM path/start/LocalSystem;
  hold actual process handle/birth, verify executable image + primary SYSTEM
  session0 token and repeat SCM/PID/birth/aliveness after identity reads.
- Hold source/target signature pins, immutable config-copy bytes/SD and supervisor
  handle. Caller retains original journal while preparing/verifying. Template
  stores no installed-source pointer and never source_verify after journal transfer.
- Original UUID/host/journal/history, explicit clear generation and UTC deadline
  rechecked. Refuse94/66/68/69/window/apply/unknown histories. Repeat clear/time
  after synchronous signature/process work so late completion cannot succeed.
- Worker PID/birth remain ZERO because no worker exists; direct recovery codec
  refuses publication. Future worker_start locally creates real confined worker
  and supplies its actual epoch. No task/helper/Job creation, journal append,
  barrier success, watchdog arm, READY or stop/apply permission occurs here.
- Recheck template immediately before future worker_start AND after startup,
  before transfer; failure must cancel only owned Job. Copied recovery bytes must
  stay pinned while used. Do not reuse template after transfer/window publication.
- Worker-plan architecture now an owned copied field/getter, no borrowed source.
- Root's read-only authenticated status source/fixture integrated into setup
  build/run_tests; reader.c is included by fixture, not linked twice.

## Validation
- Supervisor-template348/0 BOTH x86/x64 /W4 /WX: real held process birth and
  actual Windows private config SD/recovery codec; SCM/token/source-signature are
  explicit models, not SYSTEM/live installation proof. Checks terminal/advanced
  history, bad path/record bounds/schema, config/source drift, own-process/account,
  pid/image/birth/session/primary token, generation/window/cancel/time expiry.
  Zero-worker template refuses encode; copy bound to actual owned test process
  encodes/decodes with real recovery codec. No dummy child or service created.
- Focused regression botharch: worker-plan392/0; authenticated status209/0;
  request durable/exact-retry PASS; host104/0; preparation174/0; result218/0.
- Final unified setup `build.cmd all` exit0, x86/x64/default /MT, no warnings,
  includes supervisor_template + root remote_status source. All focused fixtures
  and test registrations integrated in run_tests.cmd. No new broad gate
  needed solely for this composition; root separately validates signed route and
  Con adapter/native gates. Live SYSTEM supervisor-template acceptance not run.

## Remaining prerequisites
- Actual worker executable/entry with ticket68/receipt69 startup, independent
  source/operation64 recheck and forward executor are still absent/disabled.
- Obtain owner-authenticated frozen helper bootstrap receipt and verify actual
  protected PF/recovery binary/ACL through existing task adapter; no ad hoc trust.
- Native fresh channels/process capture, candidate check, actual Job and task
  arming plus independent communication watchdog remain separate gates.
- main engine NULL. This prerequisite cannot justify accepting RPC7031, stopping
  services, advertising update success, or claiming helper/communication ready.
