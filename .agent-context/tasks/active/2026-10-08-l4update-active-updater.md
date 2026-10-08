# L4Update: independent active updater nomination

## Task intake
- Scope: native tools common nomination, host ACK/CLI and Con linkage; no MB/PB or database changes.
- Owner: fresh SYSTEM installer creates fixed pointer after authoritative55/91; suite operations only consume it. Updater self-update remains separately closed.
- Producer → consumer: original fresh85/root/descriptor and committed40/55|91 → private `state/updater-active.json` → Con/common remote host → Setup controller/worker.
- Invariants: native KnownFolders, primary SYSTEM session0, no impersonation, no path or URL supplied by RPC, no scan/fallback/adoption, owner RSA before metadata interpretation, immutable original journal/root/image held handles, suite/updater identities independent. Existing55/91 semantics unchanged; live remote SFA check remains separate.
- Validation: unified x86/x64 static native build and pure/modeled fixtures. Actual fresh pointer creation under SYSTEM and remote lifecycle are not claimed by those fixtures.

## Implemented
- `active_updater.c/h`: strict five-field pointer codec(schema/version/arch/origin/root_sha256); typed nomination requires original committed fresh history, unique85/40, no92/102, config20 and PATH80 references, saved owner-signed root and descriptor, matching selected arch and exact protected held installer size/hash. No Authenticode relaxation: child pre-ACK retains strict signature/revocation checks.
- Initializer uses owned fresh journal directly after flush55/91, pins saved inputs and setup image, writes private fixed pointer through CREATE_NEW temporary and non-replacing WRITE_THROUGH rename. Exact retry accepted; conflict refused. A leftover temporary after crash is not adopted or deleted automatically.
- `journal_reader_open_fixed_immutable_reference`: fixed original UUID only, full chain validated, FILE_SHARE_READ only; no directory scan, repair, writer ownership or standalone deployment authority. Immutable codec view used only by existing read-only bootstrap decoders.
- Shared `bootstrap_history.c/h` contains existing recorded phase/terminal semantics and pure plan validation. No SCM action added; frozen helper unchanged.
- Remote host retains typed active updater. Host executable comes from updater version, source_version remains current suite. New controller CLI carries both versions.
- ACK93 v2 is144 bytes(schema2): original offsets preserved, suite version72[32], arch104[8], updater version112[32]. Historical112-byte/schema1 decoder retained for observations; live process proof and Con acceptance requirev2.
- Con all builds and runtime test link active_updater/history/journal_codec; focused nomination fixture included in Con all. Admission gate remains compile-time closed until complete real executor acceptance.

## Validation observed
- x64 bootstrap registration/recovery/activation/commit/abort:43982 checks,0 failures; SCM mocked.
- x86 same bootstrap suite:43982 checks,0 failures; SCM mocked.
- Remote host codec/SCM fingerprint model:116 checks,0 failures per architecture. Model covers suite1.13.6/updater1.13.5, historical ACKv1, strict v2 sizes/padding/version and actual ordinary-token refusal; no service created.
- Active updater focused fixture passesx86 andx64; owner-signature verifier modeled locally, not an actual RSA or protected pointer E2E claim. Production metadata RSA has separate existing gates.
- Admission bounded owned-thread fixture passes default-closed and modeled-enabled x86/x64 withv2 acknowledgment.

## Pending / boundaries
- Complete final Conall after outcome103 consumer and common status links settle; earlier builds do not cover final current source.
- Setup build owner must retain bootstrap_history and active_updater dependencies; root integrates initializer after fresh local deployment and carries updater-version through host/worker CLI.
- Actual SYSTEM fresh initializer must be verified on the new signed clean baseline before7031 enabling. No protected pointer, installed actor, production service, MQTT client or release publication changed by this task.
- Full watch/rollback fault acceptance and remote7031 E2E still required; isolated SCM/SFA acceptance does not substitute for these.

## Final combined Con gate
- `tools/l4con/build.cmd all` PASS x86/x64/default after common status/outcome source freeze; no compiler warning/error matches. Log: `tools/l4con/obj/outcome103-unified-build.log`.
- `tools/l4con/tests/test_rpc_runtime.cmd` PASS both architectures; log `tools/l4con/obj/outcome103-rpc-runtime.log`. Local isolated TCP/IPC fixture proves registered7032/event76 framing, strict marked orphan consumer, update busy/drain and closed7031 behavior. No live broker/terminal request and no duplicate client.
- Real7031 compiled admission remains0; these gates do not certify actual signed forward/watchdog/rollback or fresh anchor creation.
