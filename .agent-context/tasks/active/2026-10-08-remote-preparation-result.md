# Remote pre-worker terminal result — 2026-10-08

## Task intake
- Authorized scope: tools native l4setup/common and l4con result codec; second of
  the two requested implementation steps. No installed changes or live MQTT/API.
- Owner: admitted SYSTEM controller holding original7031 journal/deployment lock.
- Producer → consumer: original92/93 and optional signed route60 → flushed94 →
  authenticated journal reader → existing event76/tag449 serializer.
- Invariants: original task UUID; preparation/planning only; no false success,
  no delivery UUID in journal, no outbox/retry queue or frozen-helper change.
- Verification: actual isolated journal and canonical codec, modeled host/route;
  original bindings/drift/terminal immutability, both native architectures.

## Implementation
- Common remote_result codec owns bounded strings/time/error fields. Fixed200-byte
  canonical schema rejects nonzero reserved bytes, malformed/nil UUID, malformed
  versions, unsupported target/outcome, zero error and invalid/reversed UTC times.
  Only suite failed/cancelled is supported; cancelled requires ERROR_CANCELLED.
  Decoding valid shape does not authenticate provenance or authorize action.
- Setup save derives UUID/request/start from original92 and previous version
  from original93. Optional owner-trusted route supplies resolved target and must
  match request/source version/bundle architecture. A corrupt/untrusted route
  refuses rather than making up a resolved version.
- First append requires current original controller PID/birth and native self
  ownership proof. Record94 is flushed once. Exact retry retains finish time;
  a changed error conflicts. Read-only result load does not require a live host.
- Preparation and immutable config/source/switch/operation proposal records
  are permitted; any worker handoff, window, mutation intent or later record
  refuses this result path. Proposal payloads grant no stop/success authority.
  Any record following94 refuses; further prepare/worker-plan paths reject94.
- Con adapter converts the authenticated typed outcome to the existing76/449
  object. Fresh distinct delivery correlation is supplied at serialization.
  It performs no network publication and claims no delivery.

## Evidence
- Isolated result fixture218 checks/0 failures on each x86/x64, /MT /W4 /WX.
  Journal/header/record hashing, append/flush/reopen and codec are actual; native
  controller ownership and owner-signed route adapter are explicitly modeled.
  Includes shape-valid forged operation/time/request/previous/resolved refusal,
  exact retry/reopen, cancellation, reserved-byte corruption, advanced-history
  refusal and planning-only completion versus mutation-intent refusal.
- Con unified build x86/x64/default COMPLETE, including event adapter fixtures
  and existing build gates. No client/presence/transport behavior was changed.
- Setup sources and fixture integrated into normal build/run_tests by the worker
  plan step. Final combined setup build evidence is in that handoff.

## Remaining
- Wire authentic live status reading and bounded event publication into the real
  controller/Con flow; do not accept caller-authored outcomes from user event IPC.
- Post-worker outcome authority and success need the actual apply/rollback,
  final communication/suite proof and final installed-source receipt.
- No terminal update or IoT/Registry deployment performed. Main engine remains
  disabled until forward executor and recovery/communication gates are connected.
