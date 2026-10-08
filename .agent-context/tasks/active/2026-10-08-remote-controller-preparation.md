# Remote controller preparation — 2026-10-08

## Task intake
- Native l4setup controller only; producer original7031 request92 and hostACK93,
  trusted installed-source adapter → signed Registry route60 → all-hop61/62.
- Source authority remains installed_source, compiled metadata owner and strict
  Authenticode admission. No RPC/argv/catalog input can select source or profile.
- Scope excludes live services, network/API, installed state, signing/publication,
  frozen rollback helper, runtime RPC enablement and forward apply.
- Use repo-intake-and-routing and native-windows-tool-change; l4update card and
  remote-host foundation handoff. Validate native /MT x86/x64 and meaningful
  modeled composition + actual CNG/journal signed fixtures.

## Implementation
- `remote_prepare.c/h`: current controller PID/birth and original92/93 binding;
  authenticated source version/root digest/arch/installer hash; signed original
  Leo4Proxy SCM command supplies normal HTTP port via l4_proxy_signal_source.
- Production profile is exclusively l4_platform_current: actual NT build/native
  machine/client class. Bundle arch is independent (WOW64 x86 is not Win7).
  Exact profile binding is checked on saved60 and returned all-hop pins.
- Only one original60 selection. Retry loads60 offline and rechecks requested
  version/latest, source hash/version, arch, owner trust and runtime profile.
  It never reselects latest or redownloads the catalog for an existing route.
- Refuse malformed/duplicate60 and operation history beyond prep; updater target
  unsupported. No adoption of another controller process epoch.
- One monotonic deadline100..600000 includes initial/source/offline checks and
  transport. Remaining transport budget is passed down, never renewed.
- update_metadata controlled preparation checks cancellation/deadline before and
  after transport/admission, between restored hops, before61 and62. Existing
  timeout API remains a wrapper; strict trust and signed package checks preserved.
- Result owns held all-hop cache/admission pins after offline load and repeated
  source verification. It is package preparation, not readiness or permission
  to stop/apply. SCM/marker/config remain unchanged. Cancellation applies only
  to preparation and does not rewrite/erase durable original records.
- remote_host locked receipt load is read-only; main still NULL engine, so no
  fake accepted/no-op remote operation. Actual forward executor still needed.

## Evidence and limitations
- Remote preparation composition171 checks/0 failures, bothx86/x64. Source,
  SCM, signature and transport are modeled explicitly; actual controller process
  birth is checked. Tests cover first selection/retry no reselection, drift,
  untrusted route, wrong request/arch/profile, duplicate/history progression,
  wrong ACKPID/birth/request/hash, cancellation and budget expiry after work.
- Metadata signed-fixture composition1225/0 botharch: actual ephemeral Python
  signatures/CNG/journal, modeled transport/trust. New cancellation asserts no
  package/completion record before or after canceled metadata transport. Late
  cancellation/time expiry just AFTER flushed62 returns false with zero output,
  preserves durable completion and permits offline load/idempotent retry.
- Synchronous Win32/CryptoAPI calls cannot be interrupted by checkpoints; time
  overrun refuses success afterward. No claim of hard call-level time bounds.
- Final `tools/l4setup/build.cmd all` exit0, x86/x64/default /MT, no warnings.
  Focused final rerun: preparation171/0, metadata1225/0, host104/0,
  platform profile PASS, on both architectures. Tests are integrated into
  run_tests.cmd; preparation and platform sources into build.cmd.
  Agent build/test processes finished; temporary ignored runner scripts removed.
- Actual live SYSTEM host/installed-source/Registry preparation was NOT run.
  Runtime profiles need owner-signed matching catalog edges; a runtime profile
  label is not compatibility evidence. No new edges invented by this stage.
- Installed-source object borrows the original journal. Its getters/verify must
  never be used after worker transfer closes that journal; copy needed typed
  source plan before handoff. Preparation itself retains original ownership.

## Next owner steps
- Wire forward engine only once actual executor/recovery/communication gates are
  implemented; preflight must remain read-only before93. Publish real signed
  runtime-profile transition evidence through release workflow.
- Full setup gate after this agent's obj-free signal; remote live acceptance
  remains separate from fixture and binary-build success.
