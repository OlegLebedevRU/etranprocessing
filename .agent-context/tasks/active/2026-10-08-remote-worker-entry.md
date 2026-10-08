# Remote worker entry — 2026-10-08

## Task intake
- Scope: native l4setup/common worker startup; no MQTT client change, service
  switch, installed release replacement, signing or publication.
- Producer → consumer: original controller ticket68/receipt69 and owner-signed
  operation64 → fixed SYSTEM worker entry and compile-time forward executor.
- Invariants: original7031 UUID, native KnownFolders, primary SYSTEM/session0,
  original parent epoch, Job/recovery-task admission, strict publisher signature,
  held source executable and signed package pins. No test/environment engine.
- Verification: x86/x64 static builds; actual file/ACL/hash/journal fixtures,
  admission/SCM/signature modeled explicitly; actual handoff fixture separately.

## Implemented
- Fixed CLI: `--update-worker --source-version <version> --operation <uuid>
  --arch x86|x64`; canonical version/lowercase nonnil UUID and unique exact flags.
  Only the native PF/setup/source-version/l4setup.exe path can select startup.
- NULL/incomplete compile-time executor refuses before receipt69. Main still
  passes NULL; this source change does not enable remote updating.
- Real startup uses the common worker admission/recheck APIs, then independently
  reloads signed64 and binds its source layout, route architecture and exact
  original supervisor switch to the admitted recovery ticket.
- Typed admission exposes its verified parent PID/creation time without changing
  the ticket serialization. Original ACK93 must bind that parent epoch/arch and
  the owner-signed source installer size/hash; no fabricated live journal view.
- Held PF ancestry/executable prevents replacement; single-link regular file,
  protected ACL, exact size, streamed SHA256 and strict publisher signature are
  required. Local offline exception is not used by the remote entry.
- Fixed30s startup budget, read-only preflight, repeated ticket/parent/plan binding
  and deadline check before the real executor. Native calls are not interruptible.
  Executor owns apply/recovery/result; entry releases only journal still owned.

## Evidence and boundaries
- Worker-entry fixture361 checks/0 per architecture with /MT /W4 /WX. Actual
  isolated protected files, ancestry fences, SHA256 and journal I/O; token,
  admission, owner signature, SCM and task proof are modeled. Covers malformed
  CLI, missing engine, token/session denial, parent epoch/hash drift, hardlink/ADS,
  hash/signature failure, deadline and preflight mutation/cancellation.
- Fixture executor deliberately returns failure; no fake successful update.
- Combined setup unified build passed without warnings. Actual common handoff
  fixture202/0 per architecture: separate child, Job, locks, ticket/receipt and
  verified parent PID/creation binding; scheduler audit modeled, no live tasks.
- First full x64 run stopped in the existing metadata stop-gate fixture with13
  failures: positive capture calls used a1s budget and returned ERROR_TIMEOUT.
  x86 full suite passed. Same unchanged x64 binary then passed1225/0 in isolation.
  Named fixture-only10s semantic budget now avoids masking ownership/epoch
  assertions under load; explicit deadline/cancel cases and production unchanged.
  Changed metadata fixture1225/0 on both architectures; remaining x64 recovery,
  handoff, communication and setup gates also passed. Original failed log retained;
  this is complete member coverage, not a claim of one uninterrupted x64 full pass.
- Final additional fresh preflight gate: unified rebuild clean; helper113/0,
  install profiles634/0 and fresh composition1137/0 each architecture. Python
  release suite80 passed; Ruff/format/Pyright passed. See
  [helper packet](2026-10-08-immutable-helper-bootstrap.md).
- Evidence: `tools/dist/.release/evidence/worker-bootstrap-20261008/result.json`
  and build/full-x86/x64-prefix-failure/remainder/final-preflight logs. Final
  evidence distinguishes real files/crypto/child processes from modeled
  SYSTEM/admission/SCM/task-audit/provider-success/network behavior.
- Installed1.13.6, four live services, frozen helper code/binaries and external
  IoT remain unchanged by this step. No actual worker/apply/MQTT E2E claim.
- Cleanup: own isolated worker-entry scratch runner/executables/objects removed;
  ordinary suite build outputs and final evidence retained. No live task created.

## Next
- Complete trusted baseline helper bootstrap/receipt and actual worker startup
  composition. Wire communication recovery/watch and real forward apply before
  enabling main. RPC7032/event76 publisher and post-worker installed-source
  authority remain pending; updater-only target is still unsupported.
