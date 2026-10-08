# Remote preparation → typed future-worker plan — 2026-10-08

## Task intake
- Scope: native l4setup only, producer owned remote_preparation → existing
  signed operation planner63/10/64 → future worker accept69/reload64.
- Source trust belongs to installed_source and existing strict owner-signed
  operation admission. No RPC/metadata source/profile override or new trust key.
- Follow repo-intake-and-routing/native-windows-tool-change and l4update card;
  review worker_handoff, worker_start, communication/recovery typed contracts.
- No live services/network/API/installed changes, signing, rollout or engine wiring.
  Native meaningful fixtures x86/x64 plus unified build; no PB/MB scope/tests.

## Useful missing composition implemented
- `remote_worker_plan.c/h`: receives original locked journal, authenticated
  installed source, completed owned remote_preparation, and typed config refs
  `{supervisor, broker, acl}`. No guessed defaults or generic config path inputs.
- Preparation now records original UUID and exposes read-only matches(j,p).
  It refuses a different original operation or unowned/poisoned journal.
- Worker plan binds original92/93/request/controller PID+birth and installer
  hash, preparation60/62 references, source root/version/arch and current exact
  runtime profile. Existing all-hop signed packages are reloaded offline.
- Exactly three existing journal20 proposals are required. Full config_verify
  runs before path checks; fixed paths are l4superv.json,
  mosquitto\mosquitto.conf, mosquitto\acl.conf. Duplicate/stale/wrong refs refuse.
- Existing setup_update_plan_operation saves/reloads signed source63 and all-hop
  switch10/operation64, preserving fixed LocalSystem original command/start args.
  Fresh source verification and first-hop old image/hash/size/start/service binding
  prevent replacing the admitted source with another SCM inventory.
- One100..600000 monotonic total budget; cooperative cancellation/late timeout
  returns no result and preserves already durable progress for owner recovery.
- Result owns SetupOperationPlan metadata/cache/admission pins and COPIES
  L4BootstrapPlan/request/UUID/64 sequence. It stores no installed-source or journal
  pointer. Result source getter is historical binding, not a fresh process proof.
  Installed-source itself still must not be used after journal transfer.
- Terminal94, worker/apply/marker records, wrong ordering and no-op routes refuse.
  No task/Job creation, window publication, barrier success or stop/apply authority.
  Worker/recovery templates with actual Job/epochs/deadlines are deliberately not
  fabricated: those require subsequent fresh channels, owned Job and armed recovery.
- main engine remains NULL; no accepted remote no-op or live stop path enabled.

## Validation
- Worker-plan composition392/0 both x86/x64 (/MT /W4 /WX): source/profile/epoch/
  config/terminal/order drifts, cancellation/time expiry, exact retry model and
  copied snapshot surviving invalidated source/journal. Transport/SCM/admission
  are explicit models; actual controller birth used. No live acceptance claim.
- Preparation174/0 botharch, including protected original UUID accessor mismatch.
- Root's result helper fixture218/0 botharch integrated: actual journal+codec;
  host/route modeled, no delivery guarantee. Sources/run_tests integrated.
- Final unified `tools/l4setup/build.cmd all` exit0, x86/x64/default /MT, no
  warnings, after latest root result218 change. Focused final rerun392/174/218
  checks, zero failures EACH architecture. No build/test sessions remain.
  Per root review, no broader full-suite rerun required solely for this step.

## Remaining boundary
- root record94 can finalize failed/canceled preparation or planning history:
  proposal20/source63/switch10/plan64 are permitted only AFTER completed62.
  Worker68/69/window/configapply21 and all service/apply intent records still
  forbid this terminal authority. Proposal shapes are never stop/success proof.
  No success outcome or delivery guarantee is created by record94.
- Source acquisition for63 uses existing fixed Registry version and strict signed
  source admission. No live source Registry request was performed in this task.
- Future controller must create config proposals through approved component
  adapters, bind helper receipt/config snapshots, create actual confined worker,
  capture original process epochs/channels, then prepare/arm independent recovery
  before transfer68/accept69 and signed clear/window gates. Current plan alone is
  not READY/commit/recovery arm. Success/updater/post-switch outcomes unsupported.
