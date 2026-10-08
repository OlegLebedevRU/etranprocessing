# Con: outcome103 → RPC7032 / event76

## Intake
Native tools only. Existing extra_service client/presence unchanged. Root owns executor/103 producer; common status reader owns strict protected history and actual marker observation; Con serializes read-only status and publishes through its existing MQTT client. No MB/PB, IoT schema, DB, installed service or live MQTT changes.

## Implemented
-103 is separate from94 preparation and95 launch failure; mixed terminal claims refused. Con links pure `remote_outcome.c`.
-7032 exposes completion_result and completion_recorded separately from final result. SUCCESS/RESTORED remain pending while protected state clear is unconfirmed; state_clear_recorded distinguishes journal65 from state_cleared actual marker proof.
- Final SUCCESS maps succeeded; RESTORED maps restored preserving original nonzero error; REQUIRED maps recovery_required while marker may remain active.
- installed_version is target version only for cleared SUCCESS, previous version only for cleared RESTORED, otherwise null. Resolved version remains the attempted signed target and must not be presented as the installed version after restore.
-76/449 remains one schema1 opaque object. SUCCESS/RESTORED require actual outcome_cleared before allocating delivery slot or publishing. REQUIRED can publish while active. Common reader is responsible for protected clear authority; pure JSON serialization does not manufacture it.
- Same bounded existing-client sender: successful send once/process session, failed socket send at most two attempts with same delivery identity. No persistent outbox, broker connection or absolute delivery gate introduced.

## Validation
Focused event codec/status and reporter fixtures passx86/x64 before final unified build; expanded reporter covers pending65 intent without actualclear consuming no slot, actualclear success/restore, active recovery-required, old retry/history bounds and thread teardown. Final combined Conall/runtime gates passed after the common status module froze (see below).

## Controller composition fixture
New `tools/l4setup/tests/test_remote_controller.c` includes root controller source and models only its external dependencies. Covers closed/incomplete capability, owned preflight/helper/profile/window refusals, preparation/config/64/launch-policy failures, all12 config references before64, multihop refusal before worker, no94 once66/task/recovery is recorded or journal transferred, no borrowed source/j access after transfer, retaining launch pins through bounded wait and child exit as diagnostic only. No actual SCM/Job/helper/MQTT changes or E2E claim. Root owns adding it to Setup runner.

## Pending
Actual signed baseline nomination, watch/rollback fault acceptance and real7031 test readiness remain root gates; these modeled fixtures do not enable the compile-time admission gate.

## Final combined Con gate
- `tools/l4con/build.cmd all` PASS x86/x64/default after common status/outcome source freeze; no compiler warning/error matches. Log: `tools/l4con/obj/outcome103-unified-build.log`.
- `tools/l4con/tests/test_rpc_runtime.cmd` PASS both architectures; log `tools/l4con/obj/outcome103-rpc-runtime.log`. Local isolated TCP/IPC fixture proves registered7032/event76 framing, strict marked orphan consumer, update busy/drain and closed7031 behavior. No live broker/terminal request and no duplicate client.
- Real7031 compiled admission remains0; these gates do not certify actual signed forward/watchdog/rollback or fresh anchor creation.
