# Remote live status/result reader — 2026-10-08

## Task intake
- Native common/l4con scope, second of two requested implementation steps.
- Owner: admitted SYSTEM controller writes original private92/93/60/94; Con
  consumes an authenticated full-chain snapshot. No user-event IPC authority.
- Producer → consumer: private journal snapshot → recorded progress and bound
  pre-worker terminal outcome → pure event76/tag449 payload.
- Invariants: original operation UUID; no deployment.lock/append/repair/creation,
  no live mutable codec view, trusted owner route, no source/readiness/stop claim,
  no MQTT client change or installed/release/IoT deployment.
- Verification: actual journal/storage/identity/outcome codecs, actual signed
  catalog decoder fixtures, x86/x64 builds and isolated native SYSTEM reader.

## Implemented
- `remote_status.c/h` reads existing private full-chain snapshot only. Owns all
  returned values; records request/controller/route/package/plan progress without
  treating recorded progress as fresh readiness or cache admission.
- Request92 uses a canonical pure decoder shared with the existing locked loader.
  Host93 uses the existing receipt decoder through a read-only snapshot adapter;
  this is historical receipt binding, not current SCM/process ownership evidence.
- Original source layout derives from canonical source version in93, so the
  caller's newer layout version does not prevent reading an earlier operation.
  Roots/directory/original UUID/request/time/source/arch bindings still apply.
- Route60 has pure signed/trusted decoders shared with the locked loader. Reader
  recomputes the same owner-trusted route using original admission time, without
  a mutable journal view, global floor write, fresh network/latest resolution.
- Terminal94 must match original request/UUID/start/source and resolved target.
  Duplicate selection/completion, records after terminal and malformed identity,
  route or outcome refuse. Worker/window/apply history currently returns
  ERROR_NOT_SUPPORTED; its eventual terminal authority is still pending.
- Public observe uses existing primary-SYSTEM/canonical-KnownFolders live-reader
  gate. Incomplete/changing tail returns ERROR_IO_PENDING; no retry loop or repair.
- Con `update_status_event_json` observes the original journal and serializes only
  a bound terminal94 result. Pending operation returns ERROR_IO_PENDING with empty
  output. Delivery UUID remains separate; no publish/ACK/outbox/delivery claim.

## Evidence
- Status209 checks/0 failures on each x86/x64: actual protected journal/snapshot,
  canonical92/93/94 and live tail; route trust adapter explicitly modeled.
  Includes earlier source under newer caller layout, terminal binding forgery,
  duplicate/order/unsupported-worker refusal, stable snapshot during append,
  partial tail preservation and non-SYSTEM public observe denial.
- Signed catalog/route/floor271/0 on each architecture: actual ephemeral RSA/CNG
  and protected I/O. Pure decoder round-trip, bad schema/signature/truncation
  and fixture key rejection at production compiled-owner boundary verified.
- Actual isolated native SYSTEM reader:14/0 per architecture. Real KnownFolders,
  private journal and public observe gate; controller/terminal producer is modeled,
  no signed-controller/SCM/worker/MQTT acceptance claim. Fake1.13.9 caller layout
  reads an original1.13.6 fixture receipt; installed version was not changed.
  Evidence: tools/dist/.release/evidence/system-status-20261008.
  Both created operation journals deleted; shared deployment lock preserved.
  Own hidden scheduled task and private launch assets deleted after result0.
- Con unified build x86/x64/default COMPLETE and event adapter/build fixtures PASS.
- Final combined setup unified build exit0/no warnings. Relevant focused shared
  checks per arch: status209, requestPASS, host104; template348, worker-plan392,
  preparation174, result218. See supervisor-template prerequisite packet.
- Four installed services remain Running. No installed executable/helper/config
  change, signing, Registry publication or external IoT deployment performed.

## Next
- Actual worker entry and authenticated baseline helper installation/receipt are
  prerequisites to startup. Wire communication recovery/watch and full forward
  apply before enabling main's remote engine; it remains NULL.
- Connect status to RPC7032 and bounded publication using existing Con client,
  outside latency-sensitive MQTT callback work; this consumer helper alone does
  not enable a remote operation or send an event.
- Post-worker success/report authority requires final apply/rollback/communication
  proof and installed-source receipt; record94 never represents successful update.
