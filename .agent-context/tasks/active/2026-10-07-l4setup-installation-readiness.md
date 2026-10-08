# L4 Tools first installation readiness — 2026-10-07

Subsequent owner steering supersedes the legacy retention/manual backout below:
no old tools/configuration archive or migration; a separate UAC operator wrapper
retires the old services and removes C:\l4tools after native success. See the
[operator transition handoff](2026-10-07-l4setup-operator-transition.md). The signed
candidate itself is unchanged; actual transition remains unexecuted.

## Scope / owner / boundary

Owner: tools suite release and terminal installation. User requested readiness
before real installation. No retirement of terminal773/tenant1's four working
services, no system PATH changes, no legacy migration, no stable promotion or
backend contract change. Original worktree/index preserved; isolated clean local
source checkpoints were used for signing. Frozen helper was not rebuilt/changed.

## Concrete ready candidate

- Candidate: **1.13.5**, local signed offline kit, not published/stable.
- Source: `9c8adebff20b9d9aa56bd88c9effb10871ed1857` (isolated clean checkout).
- Kit: `C:\Users\oleg_\.codex\worktrees\55a5\l4-ready-1.13.5\tools\dist\.release\install-kit\1.13.5`.
- Universal installer SHA256: `cc49d8e07d01168368697a29ac18de3f09a46d4bc9e043d7fc34a6acc9ff4230`.
- Owner authorization expires: `2026-10-14T18:26:51+00:00`.
- SYSTEM verify operation: `17730000-0000-4000-8000-000000000005`, exit0,
  suite_install_requested=false. Native status: committed=false, aborted=false,
  receipt0, live_health_checked=false. This is successful preparation, not an
  installation/communication commit.
- Exact procedure: [fresh-install runbook](../../../docs/ops_run-l4tools-fresh-install.md).

## Implementation / contracts

- Strict fresh entry admits a current owner-signed offline kit and its exact
  signed installer, prepares protected roots before the original journal, stages
  private original inputs/immutable installer, then a fingerprinted temporary
  SYSTEM service. Only explicit fresh-install mutates suite SCM/PATH; all four
  names must be absent. Legacy install fallback is closed; no silent elevation.
- Native bundle archive import uses the existing canonical private cache,
  exact size/hash, held ancestors and read-only reopen. Native unpack/admission
  guards were preserved. Descriptor/root/signatures and both payloads are verified.
- Original receipt85 reconstructs abort-only context, including PATH records80..84;
  committed state/new epochs/foreign drift are never silently adopted. Before-
  receipt interruption and reboot/new epochs require owner repair.
- Service-local broker REG_MULTI_SZ Environment/MOSQUITTO_DIR points to new
  ProgramData config; global variable untouched. Readiness checks it around
  broker/full communication barriers. Existing fourteen routes, SN/backend and
  l4con=extra_service/l4desk=svc_desk contracts preserved.
- Offline kit command signs seven-day exact-version/root authorization with
  stable=null/transitions=[]; existing kits/releases are never overwritten.

## Verification evidence

- [x] Locked full native/signing pipeline1.13.5, clean_at_start=true, status=signed;
  x86/x64/default builds, both setup suites, signing/timestamps/full integrity pass.
- [x] Native setup production/tests: no compiler warnings/errors. Install profiles
  628/0 and composition1019/0 per arch; SCM/admission/signals modeled in composition.
- [x] Protected cache69/0 per arch: actual file/hash/ACL/held handles, modeled remote
  stream; local import success and hash/size/handle refusals without network.
- [x] Real isolated SCM/REG_MULTI_SZ/SYSTEM inheritance13/0 per arch; journal
  admission modeled. Additional override fixture14/0 proves precedence over the
  normal OS variable without changing global environment. Owned services removed.
- [x] Real owner-signed bundle/cache/unpack/Authenticode/full inventory consumer
  passed on an isolated temporary root (admin); separate public SYSTEM verify1.13.5
  passed on the stand and prepared immutable files without suite SCM/PATH mutation.
- [x] Native signed CLI: help, unknown/invalid flags, invalid UUID, non-SYSTEM
  worker, old-service presence and closed legacy fallback; status bound to UUID.
- [x] Python release/publisher94 tests; scoped new modules ruff/format/pyright pass.
- [x] Nine fresh x86/x64 PE artifacts. Default image code/resources match x86;
  installer default/dist are signed, architecture build outputs are unsigned, so
  raw byte equality is intentionally different only by Authenticode checksum/
  security directory/certificate table. Normalized image equality proven.
- [x] Certificate valid, no duplicate; live SYSTEM proxy uses the same LocalMachine
  certificate. Public SYSTEM verify confirms private key discovery/operator token.
- [x] Four original SCM commands/accounts/start/states/PIDs, machine PATH digest,
  global broker environment,237 task identities, helper hashes and every published
  local1.13.2 artifact unchanged. No temporary host/fixture service remains.
- [x] Scoped credential scan273 changed/untracked files: only three reviewed
  masking/regex/synthetic matches; no actual credential. New files whitespace pass.
- [ ] Actual suite start/communication/IoT barriers, fresh install commit/failure,
  post-reboot/new-epoch repair and desktop acceptance: next authorized installation.
- [ ] Typed forward/RPC7031 production controller, mode3 admission, updater-only
  self-update, live mixed-version/stable promotion acceptance remain separate work.
  Do not infer their implementation/readiness from this cold-install candidate.
- N/A: PB/MB/shared/front checks, production MCP/SSH/deployment — no changes there.

## Real failures found and corrected

1. Signed1.13.3 verify error2 before SYSTEM host: initial journal required roots
   that the public entry had not prepared. Fixed ordering after crypto admission.
2. Signed1.13.4 reached SYSTEM, then error123: archive supplied from original inputs
   instead of canonical cache. Added bounded held-file cache import; real signed
   consumer and final SYSTEM1.13.5 verify pass. Guards were not weakened.
3. Temporary test runner edit during execution and fixture name/path mistakes
   produced failed test attempts; final immutable x86/x64 runs passed. Negative
   native exits now fail the test script, not merely positive errorlevel codes.
4. An early1.13.5 launch raced checkout creation and reported missing module before
   pipeline/report start. Restarted only after a complete clean source checkpoint.
5. Initial post-sign raw default=x86 equality assertion was too strong: signing
   modifies only the default installer. Inspected both images and proved equality
   after removing only documented signature/checksum fields; no artifact changed.

## Evidence / cleanup / next stage

Ignored logs and JSON under original `tools/dist/.release`: installer-ready-
1.13.5-release.log, -kit.log, -system-verify.json, -status.json, -cli.json,
installer-readiness-final.json, installer-ready-baseline.json/-after.json,
installer-ready-certificate.json/-old-images.json/-old-path-count.json,
real-bundle-probe.log, cache-import-tests.log, broker-environment-override.log.
Native per-step logs/report live in the ready checkout's tools/dist/.release/1.13.5.
No secret values recorded. Original private verify inputs/cache and immutable
PF candidate/installer files retained intentionally for readiness, not live use.

First real installation must back up exact old SCM/PATH, retire supervisor/con/
broker/proxy, wait for original process exit/SCM absence and disappearance of
18443/18883/1883 listeners. Remove only eight reviewed old suite PATH entries
before fresh install; preserve unrelated entries/type and the private original
snapshot for manual backout. Old C:\l4tools stays as manual recovery evidence.
Actual fresh install then repeats mandatory probes/REQ-RSP+EVT-EVA before commit.

Cleanup of the owned real-bundle temporary fixture was rejected by automatic
review (blocked by policy, no detailed reason). Retained:
C:\Users\oleg_\AppData\Local\Temp\l4bundle-real-229088-2456743671.
No retry/alternate deletion. Earlier policy-retained empty temporary tree also
untouched. Signed local1.13.3/1.13.4 candidates remain as failed-readiness evidence;
use1.13.5 only. No cleanup is an excuse to change the successful signing checkpoint.
