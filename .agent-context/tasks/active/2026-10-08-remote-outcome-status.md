# Remote applied outcome and observation, 2026-10-08

Owner: tools/l4setup whole-suite completion/restore producers; tools/l4common
protected read-only status and canonical outcome codec. Root owns completion102,
executor/controller/clear policy. Restore108 is owned by the transaction agent;
Con7032/event76/reporting consumer is owned by the IoT agent.

Implemented source:

- `tools/l4common/remote_outcome.h/c`: canonical280-byte record103, separate from
  preparation94 and launchfailure95. SUCCESS1 has error0 and102 reference/hash;
  RESTORED2 preserves original nonzero error and108 reference/hash; REQUIRED3
  carries nonzero error, no proof and cannot clear. Pure codec grants no authority.
- `remote_status.c/h` and included `remote_status_apply.inc`: observed65,
  config21/22, service100/101/104/105/106 and config-abandon107,102/108/103. Applied
  history requires typed64 with12 config20 refs/fixed four10 refs; exact pending
  actions and final process epochs bind completion proof. Protected92/93 identity,
  route60 target/source roots, exact64 digest and103 proof digest must agree.
  Historical opaque preparation-only64 remains progress observation; no applied
  history/outcome can follow it. Current native executor supports one hop only.
- Host93 historical112/schema1 and new144/schema2 are accepted; new schema retains
  source suite at72 and independent updater version at112.102 updater identity
  must match the original host receipt. Actual live-host authority remains in its
  existing native producer, not this history reducer.
- Snapshot `has_outcome` means recorded finishing/result evidence. `has_result`
  remains reserved for94/95. Clear65 is an intent, not atomic-publication evidence:
  `outcome_clear_recorded` and `outcome_clear_generation` are separate from
  `outcome_cleared`. Only live observe of protected actual state at exact clear
  tuple, or a later strict-CAS generation, yields FINISHED. No timeout auto-clear.
- `tools/l4setup/src/remote_outcome_internal.h/remote_outcome_report.c`: private
  writer for trusted opaque completion/restore producers. Derives identity/time
  from original92/93/64, verifies current primarySYSTEM/session0 workerPID/birth
  against exact68/69, matched protected proof bytes/hash and active marker. No
  WAIT-only worker/helper audit after guardDONE. Opaque producer remains responsible
  for fresh final native checks and settled guards; no public rawproof/bool writer.
  Exact retry preserves timestamp and sequence. No SUCCESS demotion to REQUIRED
  after102/108. Writer does not publish or clear the marker.

Validation, isolated x86/x64 `/MT /W4 /WX`:

- Existing protected live-status fixture350/0 each; additional reducer fixture
  `test_remote_status_apply.c`513/0 each including the original checks. It uses
  real private journal/snapshot and switch codecs; action/admission/opaque proof
  producers are modeled. Covered SUCCESS102/103, RESTORED108/103, REQUIRED refusing
  clear,106/107 abandonment, malformed proof/ref/epoch/mode and clearintent without
  claiming atomic completion.
- `tools/l4setup/tests/test_remote_outcome_report.c`53/0 each. Real private
  original92/93/64/68/69/102/103, hashes and actual current process birth; SYSTEM
  token APIs, admitted metadata, opaque proof and active marker are modeled within
  this test TU only. Covered identity derivation, reference/hash/birth/session
  refusal, exact retries, original restore error preservation, REQUIRED without
  proof and duplicate outcome refusal. No native completion claim.
- Production `remote_outcome_report.c` compiled independently both architectures.
  Parent owns final complete Setup/Con build integration and release gates.

Build integration notes: reducer fixture includes `remote_status.c` and the
existing storage fixture includes `journal_reader.c`; do not link duplicate TUs.
Writer fixture includes `remote_outcome_report.c`; do not link it separately.
Status now requires common `remote_outcome.c`, `update_state.c` and existing
`journal_codec.c` for exact switch decoding. Pure outcome codec has no journal
link dependency. Ignored commands are under `tools/l4setup/obj/outcome-*` and
outputs under `obj/outcome-status/x86,x64`.

Not performed: live service/marker changes, installation, signing, publication,
native real7031 apply/rollback/actualclear E2E. Fixtures are not evidence of channel
or installed-source terminal readiness. All local fixture private temp journals
were removed; no scheduled tasks or live private artifacts were created here.
