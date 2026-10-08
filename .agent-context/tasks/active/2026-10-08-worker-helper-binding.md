# Worker immutable helper binding — 2026-10-08

## Task intake
- First of two authorized native l4update steps; working tree, no deployment.
- Owner: common worker ticket admission and l4setup worker dispatch/executor.
- Producer → consumer: task-audited ticket68 helper identity → typed admission →
  strict owner-signed immutable PF receipt → future worker executor.
- Keep original RPC7031 operation UUID, serialized68/69 unchanged, frozen helper
  code/bin unchanged, no fresh-local offline exception in remote admission.
- No MQTT client change or MB/PB/shared/IoT change; native tests only.

## Implemented
- L4WorkerAdmission copies exact helper size/SHA256 from the validated original
  ticket. Its native typed struct changes; journal/wire schema does not change.
- Worker entry loads the strict immutable receipt, holds helper/document/signature
  and ancestry fences through preflight/execute, and compares its helper to the
  admitted task identity before preflight and after the final worker recheck.
- Compile-time callbacks receive a borrowed const helper valid only while entry
  retains the receipt. All failure paths release receipt; executor may consume
  journal ownership without invalidating these independent file pins.
- A shared setup_worker_compiled_engine() accessor controls child dispatch and
  future controller launch capability. It currently returns NULL; main uses it.
  There is no caller/CLI/environment flag enabling a missing forward executor.

## Evidence
- Focused worker entry526/0 each x86/x64, /MT /W4 /WX, Win7 target. Actual temp
  files/ACL/ADS/hardlink/streamed CNG hash/journal, modeled token/admission/SCM/
  owner signature and helper receipt. Added missing receipt, mismatched identity,
  absent getter, second-admission helper drift, late load and lifetime checks.
- Production accessor NULL is asserted; fixture executor returns failure,
  never a fake successful update. Existing signature/deadline/parent checks remain.
- Final setup unified build x86/x64/default passed; default equals x86. Actual
  child handoff202/0 per architecture includes new helper size/hash assertions
  in the child; scheduler audit remains modeled. Controller composition197/0,
  started supervisor proof1085/0 and actual communication storage2514/0 each.
- Started verification requires exact66/template/owned worker and67/task XML plus
  fresh audit. Phase after communication additionally requires one exact70,
  immutable file/journal/signed64 references/config/epoch/deadline-reserve binding.
  Pre-start verification still rejects66; both refuse68/window/apply/duplicates.
- New communication journal verification is read-only: no missing-file creation
  or partial-plan adoption. Actual protected fixture covers valid binding,
  external old-config drift/restoration and duplicate70 refusal.
- Evidence: `tools/dist/.release/evidence/worker-launch-20261008/result.json` and
  build/focused logs. See [controller launch](2026-10-08-controller-worker-launch.md)
  for exact modeled orchestration scope and missing terminal-result authority.
- Cleanup: own entry-helper scratch runners/executables/objects removed after
  completion; ordinary native outputs and durable evidence retained.

## Remaining
- Real initial signed frozen baseline and SYSTEM helper installation are pending.
- Controller startup/communication recovery composition, forward apply/watchdog,
  bounded result publication and real signed failure acceptance are still required
  before enabling RPC7031. No live service/task/signing/publication performed.
