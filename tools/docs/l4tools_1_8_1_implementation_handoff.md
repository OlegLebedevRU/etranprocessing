# Tools 1.8.1: implementation handoff (2026-09-25)

Branch `l4capture/l4c-11-release-package`, starting HEAD `733a173`. Owner: tools suite. Scope: L4PIN, L4SETUP, L4SUPERV packaging and L4CAPTURE integration. The worktree already contained unrelated edits and untracked files; none were reset or deleted. This packet records evidence from the current implementation pass, not release acceptance.

## Actual setup flow and failure surfaces

| Operation / phase | Action and prerequisite | Failure handling now |
|---|---|---|
| All / Check | Acquire single-instance mutex, OS/arch and version check, recover incomplete marker, disk and certificate discovery. The GUI first performs a read-only asynchronous inspection. | A failed recovery blocks further mutation; downgrade is blocked. Root CA handling remains in the explicit action path. |
| All / Prepare | Set process and Machine `MOSQUITTO_DIR`, PATH, firewall, CA and Win7 TLS settings. | Machine environment write failure returns 24 and records a failed summary. Firewall/CA failures still need a live-system verdict. |
| Install, upgrade, repair / Stop | Drain/stop services before payload swap. Verify skips this phase. | Drainage failure returns 22. |
| Install, upgrade, repair / Update | Extract payload to staging; precheck locked executables; move existing directories to rollback, then staging to live. Create `<dest>\\mosquitto\\log`, set SYSTEM/Administrators DACL, prepare config through L4SUPERV, register services. | A lock before swap preserves old rollback. A failed move invokes rollback; unrecoverable rollback keeps marker and staging. Mosquitto prep/registration failure is reported with actual rollback result. |
| All / Start | Certificate phase, then Leo4Proxy, Mosquitto, L4Con, L4Superv. Mosquitto config preparation also runs for Verify. | Already-running services remain running. STOPPED is immediate failure. Up to 3 attempts per service, 2-second interval, 120-second attempt cap, 480-second total cap; dependencies gate later starts. Interactive continuation is explicit; silent mode stops. |
| All / Verify and Finish | Local probes, state and summary write; successful statuses clear marker. | Partial service start is exit 24 with `partial`, reboot recommendation and intervention flag. A reboot is not automatic. |

The L4SUPERV configuration preparation and installer registration paths were inspected, but SCM ImagePath, ACL effectiveness, bridge-offline start, port conflicts and service recovery have not been validated on a designated machine. `MOSQUITTO_DIR` is written to process/Machine environment before service registration/start; actual SCM environment pickup is a live-system GAP.

## Stage status

| Stage | Implemented or confirmed | Remaining evidence / block |
|---|---|---|
| 1 L4PIN CNG | Fresh key container per issuance; no overwrite of active container; cleanup limited to newly created key on local failure; provider/operation/store/status diagnostics; PIN and raw certificate responses removed from logs. | Original NTE_PERM cause unproven. Permission-denied/partial-creation/restart tests are GAP. Live issuance and certificate installation are excluded by owner from this plan. |
| 2 L4PIN UI | GUI default, masked PIN, issuer-matched list/expiry, 30-day guard, confirmation of checkbox semantics; CLI user store stays unelevated; machine path uses UAC. New cert is checked for key match before old certificate removal. | Full checkbox boundary and UAC-denial interaction matrix are GAP. Live CA/certificate installation is excluded by owner. Partial old-certificate deletion failure needs a transactional recovery review before that separate work. |
| 3 L4SETUP services | Directory chain and DACL after payload, L4SUPERV config preparation, STOPPED classifier, bounded polling/diagnostics. | Real SCM and ACL/port/bridge behavior GAP. Pure state classifier is not an SCM test. |
| 4 L4PIN lock | `install_cert.cmd` no longer pauses and leaves install CWD; rollback preserves both trees on isolated open-file lock and succeeds after release. Owner reports that the historical error 32 came from starting PowerShell without administrator rights and considers it closed. | That root-cause statement is owner evidence, not independently reproduced by this pass. Intermediate swap and interrupted restart cases remain GAP. |
| 5 Partial deployment | Retry policy and per-service attempts in summary; explicit interactive continuation, dependency guards, distinct deployed/registered/running/reboot/intervention fields. | Live timeout/partial deployment and actual service registration matrix GAP. |
| 6 UI | Background read-only opening inspection, service text/color, larger controls, explicit action before mutating check. | Slow SCM/close/reopen and touch UI testing GAP. |
| 7 L4CAPTURE | x86/x64 builds succeed. Packager stages `l4capture/bin/{arch}/l4capture.exe`, which architecture filtering maps to `l4capture/bin/l4capture.exe` consumed by L4DESK. Package script now requires both binaries and license/SBOM/rollback material. Existing consumer already reads the 30-byte EVENT_METRICS payload. | Clean install/upgrade/rollback package inspection, two-hour soak, browser decode/overload, external input gate and live 100 cycles remain GAP. `test.cmd` reports 120/121; MF probe took 8422 ms versus its 8000 ms ceiling. Win7 runtime is excluded by owner for this plan. Published Agent artifact for L4D-17A must remain exact; local build cannot substitute. |

## Verification performed

- Windows host, MSVC `/MT`: `tools/l4pin/build.cmd x86` and `x64` exit 0; `build.cmd test` passes 7/7 for each architecture. These tests use isolated in-memory stores and a uniquely named temporary user CNG key.
- `tools/l4setup/build.cmd` x86 exit 0; `run_tests.cmd --safe` exit 0, 9/9, including isolated locked-file rollback/retry. `tests/run_service_state_tests.cmd` exit 0, 6/6 for x86 and x64.
- `tools/l4capture/build.cmd all`, `tools/l4superv/build.cmd all`, `tools/l4desk/build.cmd all` passed earlier in this pass for x86/x64. `git diff --check` exit 0 after code changes (line-ending warnings only).
- No root CA installation, real service action, broker connection, installer smoke on active system, Win7 runtime, external publication or deployment performed. No package was generated from this worktree.

## Release decision and recovery

**1.8.1: BLOCKED / NOT_BUILT.** Current L4SETUP version remains 1.7.7. No 1.8.1 artifact ID or digest exists. Do not run destructive packaging over an existing archive or publish until the runtime gates, release materials, registry identity and written OpenH264 distribution approval are in hand. Published 1.8.0 evidence does not cover this new candidate.

For a failed temporary-tree update, retain the incomplete marker and staging when rollback fails; release the blocking handle, inspect live and rollback trees, then retry recovery. On a real terminal, preserve current certificate and service state before recovery. No automatic key deletion, process kill or reboot is used.

## Owner scope update and designated Windows stand (2026-09-25)

The owner designated this Windows machine as the test stand. The owner considers the historical error 32 closed as a PowerShell launch without administrator rights. Windows 7 testing and certificate installation/issuance are explicitly outside this plan; no live certificate action was performed.

The stand runs with a high-integrity administrator token. Read-only SCM inspection found Leo4Proxy, Mosquitto, L4Con and L4Superv registered as LocalSystem with paths under `C:\l4tools`, all initially stopped. Existing Mosquitto log ACL granted inherited Modify to Authenticated Users. A new isolated test showed `SetNamedSecurityInfoW` left inherited entries even with a protected DACL flag. The code now uses `SetFileSecurityW` with a protected DACL. `tests/run_mosquitto_prepare_tests.cmd` passed 6/6 for x86 and x64: missing log directory, protected SYSTEM/Administrators-only ACL, repeat preparation, and file collision. This test uses unique temporary directories and removes them.

On the designated stand, Mosquitto started to RUNNING, listened on `127.0.0.1:1883`, and was stopped afterward. This checked the currently installed binary, not the new installer payload or full service order. `tools/l4capture/test.cmd` ran 121 tests: 120 passed, `test_mf_probe_graceful` failed because first probe took 8422 ms, over the 8000 ms ceiling. No rerun was used to mask the failure. The current implementation calls synchronous Media Foundation discovery/activation and does not enforce a hard wall-clock deadline for that call.

`tools/l4desk/tests/run_tests.cmd` exited 0 on the stand. Its protocol, orchestration and adapter suites passed; the adapter suite reports 21/21 and includes `test_event_metrics_plen30_reads_gdi_handles`. This verifies the current consumer parser without changing the wire ABI or MQTT client. `tools/l4setup/build.cmd` rebuilt successfully after the ACL change.

Package audit found that `ROLLBACK.md` names `NOTICE-OpenH264.txt` as a companion file, while `pack_zip.cmd` previously staged only the license, SBOM and rollback guide. The packager now requires and stages the NOTICE file as well. Package execution and archive content verification are still pending.

At this point the release was **BLOCKED / NOT_BUILT** because the MF probe gate failed and the remaining non-excluded package/runtime checks were incomplete. No artifact was published.

## Continuation after release-scope decision (2026-09-25)

The owner excluded the two-hour soak and placed any OpenH264 distribution permission review outside this stage, describing the intended artifact as for personal use and system debugging. This does not identify a private publication destination or count as evidence of an external distribution approval. Windows 7 runtime and live certificate installation remain excluded as previously directed.

MF probe now stops scheduling further hardware activation after its shared 5-second budget and treats a late success as a timeout with OpenH264 fallback. The test runner accepts an exact test name for focused diagnosis. `tools/l4capture/build.cmd all` passed x86/x64; the focused MF test passed at 828 ms, and the full `test.cmd` passed 121/121 with MF at 656 ms. These warm results do not establish a hard wall-clock bound for one synchronous Media Foundation call or reproduce the earlier 8422 ms cold run. Browser decode, external input gate, live 100 start/stop cycles and package smoke remain open unless separately excluded.

The owner then waived the two-hour soak only. The other live gates have not been waived. Candidate packaging exposed a false success: `pack_ffmpeg.cmd` used `Get-FileHash`, unavailable in the launched PowerShell, and ignored the resulting errors. A failed attempt was preserved under `candidate-1.8.1-attempt1`; the packager now uses a SHA-256 helper backed by .NET and propagates failures. A second package-layout candidate is at `tools/dist/candidate-1.8.1/`: `tools.zip` SHA-256 `c021eda0c7f2419d50f333d32b46aea21aa6c6b252fae98a252fa748c5a0df9` (59 entries); `ffmpeg.zip` SHA-256 `e829ab27548fe8582ca2c760e1a420b0510cb2132bf38f89fb0fc3453ed2c31a` (8 entries). Both architectures of L4CAPTURE and the OpenH264 license, NOTICE, SBOM and rollback guide are present. All seven entries in the FFmpeg internal SHA-256 manifest and its external checksum file verified. This is a layout candidate, not the final embedded 1.8.1 installer; versions remain unsynchronized.

An isolated install/upgrade/rollback harness was compiled at `tools/l4setup/tests/test_payload_smoke.c`, but execution was not performed: automatic command review rejected the planned command because it included recursive cleanup of its temporary directory (`blocked by policy`; no more specific reason was returned). The rejection was not bypassed. This package smoke remains **BLOCKED**, not PASS.

## Local beta artifact (2026-09-25)

The owner additionally excluded live browser decode, the external input gate and 100 L4CAPTURE start/stop cycles from this pass, and directed that the artifact be saved under `tools/l4tools-1.8.1-beta-1`. The two-hour soak, Windows 7 runtime and certificate installation/issuance remain outside scope. No external deployment or publication was performed.

All seven component build scripts completed for x86 and x64 on the designated stand. `tools/version.txt`, L4SETUP source/resource version headers and the L4SETUP dialog text now declare `1.8.1-beta-1`; L4CAPTURE's SBOM names that suite release. Individual component versions remain independent and are recorded in the release manifest. The previous generated L4SETUP payload resources were preserved under `tools/l4setup/obj/prior-generated-res-before-beta1`.

The separate output directory contains a 29,410,304-byte `l4setup.exe`, `l4tools-release.json`, `SHA256SUMS`, and `.stage/` with both architecture trees and compressed payloads. The release manifest check passed: PE ProductVersion `1.8.1-beta-1`, x86/x64 embedded resources present, compressed payloads over 10 MiB and staged payloads over 20 MiB. The installer is unsigned and the manifest correctly records `dirty: true`. SHA-256 of the installer is `eb2db8244427fbb7cdea92ea9d59f2f2759bd0f7052144999fec6c80fc8f8c4b`; x86 payload `0f339834a1041d39b70de9423a64e5ddad55ccfc91fcdd26a2a5d1ee0c0ebd11`; x64 payload `0917c95a89b3137f5b89c839081319595221f228fdc2c7dac66355f735a60cb0`. Each payload archive has 61 entries and contains L4CAPTURE at the L4DESK adapter path, OpenH264 license/NOTICE/SBOM/rollback files, FFmpeg, L4PIN and L4DESK.

The built installer was not executed. The isolated install/upgrade/rollback smoke remains unrun after the automatic review rejection described above. This artifact is a local beta for owner inspection and debugging, not an E2E-accepted deployment.
