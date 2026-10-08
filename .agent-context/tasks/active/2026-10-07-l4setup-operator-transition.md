# L4 Tools operator UAC transition — 2026-10-07

## Task intake / owner / scope

Owner: tools suite installation. User asked for a local manual procedure with UAC
instead of an engineer visit, then explicitly rejected keeping legacy files or
settings. Scope: prepare the operator wrapper; no actual cutover on terminal773.
Producer: reviewed PowerShell script → existing signed fresh installer → SYSTEM
host/SCM/owned bootstrap. No MQTT client/backend/protocol or native source change.
Preserve exact unrelated system PATH, certificate, no duplicate bridge ID, no
forced kill/foreign SCM adoption. Do not weaken native admission/communication
barriers, modify/rebuild the sealed1.13.5 kit or implement legacy migration.

## Implemented

- `tools/l4setup/Invoke-L4FreshTransition.ps1`: read-only by default; explicit
  Apply launches visible native Windows PowerShell5.1 with UAC as needed. Inputs
  are quoted literals encoded for PowerShell, no policy bypass. Independent
  reviewed installer SHA256 and actual Authenticode validation before execution.
- `FreshTransition.psm1`: held admission files; fresh-verify before any old stop;
  exact original System/AUTO/own-process images/hash/PID+creation-time checks;
  reject foreign dependents. Sequential bounded supervisor/con/broker/proxy
  stop, actual original process exit, SCM deletion/absence, free old ports.
- Remove only reviewed old PATH entries, keep raw unrelated/empty/placeholder
  entries and type; original/candidate privately recorded, drift/readback refusal.
  No old SCM registry/config archive or automatic old-service restoration.
- Invoke unchanged fresh-install with recorded UUID. Delete fixed C:\l4tools
  only after native success, refuse links/descendants still used by a process.
  Errors retain operation stage and remaining files, no automatic adoption/resume.
- Updated fresh-install runbook; operator script is outside the signed kit and
  is not itself Authenticode-signed. Windows PowerShell5.1 required.

## Verification

- [x] Actual Windows PowerShell5.1 parser and32 guard/fingerprint tests passed.
- [x] Actual signed1.13.5 kit inputs held read-only with modeled failing native
  verifier: refused before record creation/SCM/PATH/legacy cleanup; no host launch.
- [x] Real read-only plan on terminal773: four original services, eight PATH
  entries, pinned installer hash and Authenticode Valid. No UAC/actual install.
- [x] Post-check audit: original four live service PIDs/configurations, native
  machine PATH, global broker environment,237 task identities, frozen helper
  and published1.13.2 artifacts unchanged. Scoped scan277 files (including psm1)
  has only reviewed masking/regex/synthetic matches; new-file whitespace passes.
- One initial guard test failed due PowerShell and/or precedence; explicit
  parentheses corrected the rejection condition, final suite passed.
- [x] Operator actually approved UAC; SYSTEM fresh-verify succeeded with UUID
  e48d443f-7fc4-4ff1-8d72-807cfe3c3bfb, mode1/error0. Subsequent script failed
  before retirement because clean Windows PowerShell had not loaded the
  System.ServiceProcess assembly. Explicit module-import Add-Type now loads it
  before verification; actual query-only ServiceController/DependentServices
  test in Windows PowerShell -NoProfile and guard/refusal tests passed.
- [ ] Successful retirement/cutover, stop failure/crash/interruption,
  SYSTEM install/IoT barriers and recursive legacy deletion remain unexecuted.
- N/A: native x86/x64 rebuild/signing or backend checks (script/docs only).

## Limits / next step

This is a manual local transition, not production RPC7031/mode3 admission.
Native verify does not establish current IoT availability; operator checks that
before Apply. No serialization against concurrent administrative writers; no
forced cleanup or crash resume. If new install fails, old service restoration
is deliberately not automated; inspect recorded UUID/native status/recovery.
If cleanup fails after native success, stage distinguishes that from failed install.
The sealed1.13.5 hash/source/expiry remain the installation-readiness handoff's.
No live service/file/PATH mutation or new cleanup attempt was authorized/executed.
