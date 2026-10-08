# L4Update7032 / result76 reporting handoff

## Task intake

Owner: Con existing extra_service MQTT client, common private status/result reader
owned by root, IoT own server repository. No MB/PB or installed terminal binary or
service changes. App1-only server release was authorized and completed below.
User already chose extra_service and accepted event76/tag449 and
stateless orphan transport. Root owns7031 SYSTEM host/admission integration.

## Implemented

- Actual mqtt_client7032 dispatch enqueues the addressed original operation ID;
  duplicate delivery does not enqueue twice. Reporter is a separate read-only
  thread with8 pending slots; snapshot work does not occupy MQTT receive loop.
-7032 returns authenticated recorded history, original UUID, requested/resolved/
  previous version, recorded phase, error and plan sequence. History never asserts
  actor ownership, readiness or stop authority. New trusted route metadata exposes
  resolved version before terminal. Missing/changing/refused snapshots return
  typed404/503/409 without inventing an operation/result.
- Existing MQTT client publishes event76 proactively even without7032. Protected
  directory cursor resumes across bounded scans (4 journal reads/256 entries per
  cycle). Reporter pauses while the fresh link probe is active. No CONNECT/new
  MQTT client ID, topic or user-event IPC outcome submission was introduced.
- Keep newest64 delivery identities in memory, with an eviction timestamp floor
  that prevents old history replay after eviction. One successful socket publish
  per process session; at most2 send attempts using the same separate UUID/event
  ID,5s spacing. No permanent outbox, EVA/PUBACK gate or absolute delivery claim.
- Original pre-switch94 result publishes failed/cancelled. Original authenticated
  launch failure95 always publishes recovery_required, with underlying_result,
  cleanup_error, start_stage and plan_sequence inside the single449 object. It
  does not release a marker or assert rollback. Unknown/apply history still refuses
  until the authoritative executor/final codec exists. User-event schema remains
  restricted to900–999 and cannot submit76 through that IPC.
- Shutdown waits for the reporting thread before freeing MQTT callback context.
  On unconfirmed teardown the own agent exits instead of freeing live context.

## Files

`tools/l4con/src/mqtt_client.c`, `update_reporting.[ch]`, `update_status.[ch]`,
`update_event.[ch]`; tests `test_update_reporting.c`, `test_update_event.c`,
`test_rpc_runtime.[c/cmd]`; Con build links to strict common95/recovery/communication
decoders. FM/token files are owned by the separate ACL agent and were not edited.

## Evidence

- Unified Con `build.cmd all`: x86/x64/default COMPLETE, exit0;
  `tools/l4con/obj/reporting-unified-build-final2.log`.
- Event/status and async reporting fixtures pass both architectures, including
  recovery_required95, queue saturation/cancel, retry identity/bounds, newest64
  history eviction, proactive scan without7032 and clean thread teardown. Private
  snapshot/pin/publisher identity is modeled in the reporter fixture.
- `tests/test_rpc_runtime.cmd` passes both architectures;
  `tools/l4con/obj/reporting-rpc-runtime-final.log`. Actual registered7032 routing,
  duplicate delivery and event76 QoS1/nonretained frame through isolated local TCP
  during update were exercised. No MQTT CONNECT or real broker was used.
- ACL agent performs a final unified Con build after its independent fm_user
  token selector addition. Earlier Con evidence above is scoped to these changes.

## IoT readiness

Initial read-only production source audit proved old app1 revision6209faf lacked
the orphan module/dispatch, new capability allowlist and76 billing exclusion. Local
release source: isolated clean worktree, exact12 files, commit
`2adf14f2686d68290ea1427e93c567239d8e11ca`, PR
<https://github.com/OlegLebedevRU/iot-rpc-rest-app/pull/105>.
Clean full gate575 passed/7 skipped/4 warnings; changed7 Python ruff/black and
candidate12-file secret scan pass. Full tests generate schema/fixture outputs;
only owned test outputs were restored before the clean release commit. Preexisting
dirty schemas remain outside the commit. No model/Alembic delta versus runtime
baseline. PR105 was reviewed and accepted by root, then merged normally as
`5f80d6b0ead1c5c8cba54d67603656cdc71b899c`. The normal dedicated-builder script
passed549 Linux tests/33 platform skips and published the immutable image. The
existing byte-identical production deployer changed only app1; independent health,
revision/image and unchanged neighbor checks passed. Full release evidence:
[IoT release handoff](2026-10-08-l4update-iot-release.md). Real orphan/76 terminal
acceptance remains; installed773 actors were not changed by this server release.

## Remaining boundary

7031 now has an asynchronous native adapter with a compile-time closed default;
it returns explicit controller_engine_unavailable501 until root enables the real
durable SYSTEM admission/worker boundary. Updater target remains explicit501.
7030/7033 still return unsupported. Method advertisement alone does not prove them
ready. The public supervisor mode3 stop gate remains closed pending full actual
watch/recovery acceptance. This packet is source/local evidence, not a successful
update on773. No test services/tasks or duplicate MQTT client were created.
