# Fresh install773 failed at supervisor activation — 2026-10-07

## Intake / authorization / boundary

Owner: tools suite installer and supervisor. Operator manually approved UAC and
ran the transition; legacy services are now gone. Fix the observed failure and
prepare a reviewed local repair, without silently installing/retiring services
from the agent. No legacy files/config migration or automatic legacy restoration.
Producer native fresh setup → existing broker/Con → supervisor → native readiness
and original abort. Original operation IDs and process ownership remain binding.
No MQTT/backend contract change, no duplicate terminal bridge or epoch adoption;
keep old signed1.13.5 immutable and frozen helper untouched.

## Observed incident

- Successful SYSTEM verify2325898b-f324-46d5-8a9f-b6230dc5abcf, mode1/error0.
- Failed install44f8259e-6c05-4202-b17d-2f2f798f2fca, receipt16/bootstrap14,
  committed=false/aborted=false, parent error1237. Private wrapper record is
  ProgramData/Leo4/Tools/operator-transition/22b840bf-9b51-44a1-bff6-4c08fc92570d.
- Journal shows original proxy185244, broker27624, Con202728, supervisor204368.
  Proxy/broker/Con READY, then supervisor starts. Rollback begin and supervisor
  STOP_DONE exist, but Con stop refuses its changed epoch.
- Current three services are still running under SYSTEM, same operation SCM
  display markers/manual start types/original signed1.13.5 images: proxy185244,
  broker228492, Con195372. Supervisor is stopped. No commit/AUTO startup/PATH
  launcher finalization. Do not infer complete install from working communication.
- Exact13 prepared config contents still match except broker. Supervisor first
  activation regenerated broker bytes and native security descriptor100→136bytes,
  restarted broker/Con, and wrote fresh runtime state. It interpreted initially
  empty supervisor identity as a network identity transition. No agent service
  mutation/reboot/legacy deletion during diagnosis.

## Fix / preparation

- Supervisor's first identity activation now retains an already prepared link
  only if its own prior SN/thumbprint are empty, actual store certificate is
  valid/expiring with no duplicate, store and fresh proxy SN/thumbprint agree,
  current broker contract3/config/SN is valid and broker+Con are running.
  Known identity transitions and invalid config/stopped services retain normal
  regenerate/restart behavior. Native epoch/probe/barrier/commit gates unchanged.
- Extended modeled identity tests cover preservation, invalid config/service
  fallback and persistence failure. State/config/SCM/proxy are explicitly modeled.
- New candidate1.13.6 is built in clean detached source7c83bf41bbe9b19343aab4a92a9514043ed1445f;
  checkout sibling l4-ready-1.13.6. Original worktree/index and signed1.13.5 were
  preserved; only locked assets copied, external sw_sign.env used, no publication.
- Separate Repair-L4FreshOperation.ps1 is an explicit operator procedure, not
  native forward replay. Default is read-only. It pins signed old/new installers,
  verifies original native journal/status, exact original bootstrap commands,
  descriptor image hashes and SCM markers/manual profile. Holds new admission
  inputs. Apply verifies both packages under SYSTEM before stopping the inspected
  epochs, restores only original accepted broker bytes/native SD, then requires
  original native fresh-recover and aborted=true. No journal edits or new-epoch
  record adoption. Only reviewed failed runtime files can be moved into the
  original private operation diagnostics before a new clean installation.
  Legacy directory removed only after new native success. No force-kill/resume.

## Verification / material limits

- [x] Read-only diagnosis: original native status/journal kinds/epochs, current
  SCM and content hashes, native broker SD comparison; no raw credentials printed.
- [x] Supervisor identity fix tests passed x86/x64 in candidate pipeline. An
  initial new test incorrectly assumed one state-save call; corrected to require
  persistence without assuming call count (standby exit also saves state).
- [x] Repair Windows PowerShell5.1 parser/read-only actual incident plan passed,
  original commands/hash pins/markers, changed broker content identified.
  Initial status invocation required an explicit stdout pipeline for Windows
  GUI-subsystem EXE; Out-String added and actual native status then passed.
- [x] Original native SD restore/readback on isolated owned workspace file:
  managed Set-Acl did not preserve exact native representation; SetFileSecurityW
  did. The repair uses bounded native readback, not managed SD normalization.
- First full build failed an unchanged communication-boot fixture; retry passed
  supervisor then failed an unchanged communication-runtime monitor assertion.
  Standalone unchanged runtime subsequently passed1941/0. No gate bypass or
  production timeout change; all failures retained in ignored logs. Final full
  pipeline/signature/integrity outcome must be recorded after completion.
- [ ] Actual operator repair/stop/abort/runtime move/new installation and IoT
  barriers/reboot acceptance remain unexecuted by the agent. Real world settings
  may drift between inspection and action; script refuses rather than adopting.
- N/A: backend/front tests or frozen helper rebuild (not modified).

Evidence: ignored tools/dist/.release/installer-ready-1.13.6-release*.log;
candidate tools/dist/.release/1.13.6/report.json and per-step logs;
repair_policy_fixture.ps1/.bin/.conf contain only public ACL fixture evidence.

## Final preparation result

- [x] Full locked pipeline final run: signed, clean_at_start=true, all native
  builds/setup tests x86+x64/signatures/timestamps/full integrity passed. Prior
  failed attempts remain in separate release logs; no timeout/gate bypass.
- [x] Signed offline kit1.13.6 at sibling l4-ready-1.13.6/tools/dist/.release/
  install-kit/1.13.6. Universal SHA256:
  509d826632ff4391e5573a84452379474b0f9daf781e2ff911bbcb565ca3288e.
  Catalog expires2026-10-14T19:54:19Z. No publication/stable promotion.
- [x] Actual public SYSTEM verify17730000-0000-4000-8000-000000000016,
  mode1/error0/suite_install_requested=false. Explicit stdout pipeline waited
  for the Windows GUI-subsystem executable; the earlier bare launch of verification
  UUID...000006 had not provided captured completion evidence and was not used
  as the positive result.
- [x] Actual Windows PowerShell read-only repair plan with both real signed
  kits passed: current exact three epochs unchanged, supervisor stopped,
  broker drift identified, original bootstrap commands/descriptor pins checked.
- [x] Scoped credential scan279 files: only previously reviewed masking/regex/
  synthetic matches. No backend tests or native rebuild after signed checkpoint.
- Operator launcher (ignored local, **not executed**): original worktree
  tools/dist/.release/Continue-Fresh773.cmd. It invokes the reviewed repair with
  complete incident arguments and exact old/new installer pins, requesting UAC.
  The independent operator script is not embedded/signed into sealed1.13.6.
  Actual repair, diagnostic runtime moves, new installation/communication commit
  and legacy directory deletion still require operator execution/acceptance.

## Operator repair second stop — configuration ACL, 2026-10-07

Operator ran the prepared launcher: both SYSTEM verifications passed, then
Con/broker/proxy orderly stops completed. Original fresh-recover returned error5.
Read-only native status reports committed=false, aborted=true, receipt16. SCM
now has none of the four services; no suite/media processes were found. Bootstrap
ABORT_DONE is journal sequence102; config undo5..13 finished through sequence120.
Config4 broker undo has no intent yet. Therefore bootstrap-aborted is not proof
of complete fresh configuration cleanup; four original configs remain.

Pinned current config/mosquitto directory ACL contains a concrete operator SID
with full access. The common journal directory fence rejects WRITE_DAC/
WRITE_OWNER/GENERIC_ALL on a non-controller SID. Origin of this directory ACL
is not yet established; do not attribute it to supervisor without evidence.
Broker bytes and its restored native SD match the original prepared candidate.

Added explicit ResumeAbort operator stage requiring native bootstrap-aborted,
not committed, all services/processes absent, remaining bytes matching plans,
bounded existing runtime inventory and SHA256 pin of the inspected directory
SDDL. Apply repeats signed SYSTEM package verification, narrows only that
private directory to SYSTEM/Administrators full control, restores original
broker SD, and requires successful original native fresh-recover. All13 planned
fresh files must be absent before moving reviewed diagnostics/new install.
No journal edit, epoch adoption, force kill or permissive security bypass.

Read-only actual ResumeAbort plan passed; PowerShell parser/whitespace passed.
Updated local Continue-Fresh773.cmd selects this precise continuation stage.
Real ACL repair/native completion/new1.13.6 installation remain operator actions
and have not been executed by the agent. Signed1.13.6 remains immutable.

## Operator repair third stop — directory rename, 2026-10-07

Operator's two verify operations succeeded and original mode3 recovery returned
error0. All13 prepared configs are absent. Four SCM services and suite/media
processes remain absent. The next PowerShell Move-Item of config failed access
denied; failed-runtime exists with exact private operation ACL and is empty.
No new1.13.6 installation was reached. Six residual diagnostic files remain in
config/state/logs. Elevated read-only DELETE-handle probes on these roots passed;
an isolated Move-Item fixture with matching root ACLs also passed. The precise
live whole-directory rename failure is not established; do not claim it was a
specific reader, ACL denial or antivirus without evidence.

ResumeAfterAbort is a separate explicit stage: requires native aborted and not
committed, no suite services/processes, all prepared configs absent, original
validated journal/installer pins and bounded known runtime inventory. It allows
only an empty failed-runtime with exact original private operation ACL. Repeat
SYSTEM verify/recover remains required. Retain only reviewed residual files via
same-volume File.Move/no overwrite, leaving runtime directories in place. The
next clean native installer recreates/prepares its own runtime policy.

Actual WinPS5.1 read-only ResumeAfterAbort passed (six files), parser passed.
Real Move-L4DiagnosticFile fixture passed with directory reader held, preserving
file bytes/ACL/source root and refusing non-runtime source. An initial fixture
incorrectly assumed a metadata-only directory reader must block Directory.Move;
that assumption failed and was removed, not treated as proof of incident cause.
Fixtures retained under ignored tools/dist/.release. No native/signed rebuild,
backend testing or stand mutation performed by the agent. Updated local
Continue-Fresh773.cmd now selects ResumeAfterAbort; operator execution remains
pending. Actual install/communication/reboot acceptance remains open.

## Operator repair fourth stop — SYSTEM-only broker log

Operator's repeated verify/recover succeeded. File retention moved four files
(config previous + three state files), then File.Move failed on mosquitto.log.
Actual log SDDL was O:BAG:SYD:P(A;;FA;;;SY): owner Administrators, protected
SYSTEM-only file ACL. Parent-directory rights did not grant file DELETE.
This is direct evidence for the file failure; the earlier tree failure remains
unproven. Two logs were left in the runtime tree; no new installer was reached.

Agent completed the reversible diagnostic stage after checking original native
aborted/not committed and all suite services/processes absent. Matched exact
SYSTEM-only log ACL, added the standard Administrators full-control ACE without
taking ownership or altering contents. Used the reviewed file helper for both
remaining logs; pre/post SHA256 equality passed. All six retained diagnostic
files now have private protected SYSTEM/Administrators ACL. Original runtime
config/state/log roots now contain no files. No installation, service launch,
native rebuild, legacy deletion or backend access by the agent.

ResumeAfterAbort can accept this completed diagnostic retention only with an
explicit independently inspected inventory SHA256 pin, exactly six approved
relative names, no links/unreviewed files, private file policy, zero runtime
files, original protected destination and native aborted/not committed. A
partial move still refuses. Directory AI bookkeeping is normalized, preserving
exact owner/group/protected DACL/ACE checks, as native journal code does.

Actual WinPS read-only admission passed (RemainingDiagnosticFiles=0), deliberately
wrong aggregate pin refused. Updated Continue-Fresh773.cmd includes exact pin
9a1bf162ef217e494cb493b098dd798e4498d2307c2260806a0884bd626e5322.
No diagnostic tree/file move remains on that next operator execution. Signed
SYSTEM verify/recover and clean1.13.6 install/commit still run through native
gates. Actual installation/IoT/reboot acceptance remains pending.

## Clean1.13.6 attempt — native empty-directory admission/error183

Operator completed verify/recover and invoked new install operation
602aa123-732a-42b4-8ea6-a0729367a977. SYSTEM host returned183. Actual native status:
receipt0, committed=false, aborted=false. Journal is header-only (no plans or
mutations recorded), all four suite services absent. Runtime roots contain no
files but retain empty descendant directories from1.13.5.

Native fresh_install.c data_empty enumerates direct entries under all three
roots and rejects any entry, including empty directories, with183. Operator
wrapper's file-only retention had omitted this stricter native admission rule.
This is a wrapper defect; no native gate weakening/new release is needed.

Added Remove-L4EmptyRuntimeDirectories to the operator module, called after
successful original recovery and diagnostic retention, before next fresh install.
Validates complete config/state/log inventory before removal; rejects files,
links, escaped paths and unexpected >256 directories. Removes only checked
descendants deepest-first via Directory.Delete(path,false), retaining all three
roots and original diagnostic archive. Concurrent new files cause refusal.
Recovery stays before removal since original config plans need parent paths.

Actual WinPS isolated fixture passed: file presence refuses before any deletion,
empty-only removal satisfies native roots, roots/archive retained, repeat safe.
Actual read-only incident admission still passes (zero runtime files, exact
six-file diagnostic pin). No stand directory removal/new installation by agent
this turn. Continue-Fresh773.cmd already points to updated operator code and
unchanged signed1.13.6 kit. Failed pre-receipt UUID kept for evidence, never
replayed/adopted. Full real install/communication/reboot acceptance still pending.

## Second183 — original neutral bootstrap marker

Operator next1.13.6 operation57b4dc46-9a94-4727-9e65-1100267b6825 again failed183
before receipt. Actual journal24/header-only, services absent, all three runtime
roots now truly empty. data_empty's second predicate rejected existing
operations/update.state,112bytes, created at original1.13.5 registration.
The previous diagnosis had verified only the directory predicate, not the marker.

Actual marker is exact initial generation-zero L4UPD01 (owner/window/generation/
plan/deadline all zero), checksum verified, exact byte match to original journal
record65/sequence61. Original native status aborted=true/committed=false, suite
processes/services absent and controller-only protected marker ACL verified.
Agent retained that specific idle original bootstrap file at original operation
retired-bootstrap-update.state, preserving bytes; no active update state reset,
no generic deletion/lease clearing, no journal edits or installation.

ResumeAfterAbort now requires native verified journal CONFIG_DONE rollback
records for all13 original plans plus current config absence. It does not rerun
an already fully completed original recovery whose parent directories are gone.
Other repair stages still require native recovery. This is completed-state
verification, not forward replay/adoption or skipping an incomplete abort.

Added exact operator Assert-L4NativeFreshData matching both native data_empty
predicates (all direct root entries absent and fixed update.state absent). Actual
read-only incident admission passed after marker retention. WinPS fixture passed
file/empty-child/marker refusal, no marker modification, empty-only cleanup,
root/archive preservation and repeated admission. Original signed1.13.6 stays
immutable. Next operator launch still owns real install/commit; no guarantee of
unexecuted runtime/IoT/reboot acceptance is implied. Earlier partial diagnoses
and failed operation UUIDs retained rather than overwritten.

##1.13.6 error23 — old launcher identities retained after abort

Operator install e75a45d0-c9e1-4e4b-9b84-8c6bcd469d08 reached receipt16.
Journal first launcher INTENT30 at17, no launcher DONE; automatic abort finished
and all13 config rollbacks recorded through57. Actual status aborted=true,
committed=false, receipt16; all suite services absent. Failure is native
l4_release_copy_launcher's existing-target hash verification. PF/bin/leo4proxy.exe
is a1.13.5 launcher, not1.13.6: old sha256 ef9c7df2ab95f8f3ae281563462d9b74870a09379d59764ddeae19e4eedc71ba,
new4825cc9e38dcc4d81624acb4a24b5f1fe09a15040b785038b1471b1a02ba1caa.
Native copies refuse overwrite with a different approved hash. Neither a damaged
candidate payload nor a disk CRC fault is implied by this specific error23.

Agent verified all nine original canonical names/count/bytes/size/valid Authenticode
against original signed operation descriptor, no live suite processes/services,
latest native abort complete. Retained those exact files under original protected
operation retired-launchers-1.13.5; each resulting hash verified. No deletion,
arbitrary executable overwrite, frozen-helper modification, registry change or
service launch. Original old1.13.5 release and current signed1.13.6 remain intact.
Empty runtime descendants recreated by new prepare were again removed via the
verified empty-only helper. Runtime/bin now satisfy fresh admission.

Assert-L4FreshLaunchers checks existing PF/bin files against candidate launcher
size/hash before an operator attempt and again before install, rejecting unknown
entries/reparse links. It does not relax native verification; mismatched original
files still require explicit inspected retention. WinPS fixture passed matching/
empty admission, changed bytes/unknown refusal with no mutation. Actual full
read-only repair admission passed with zero remaining runtime files and exact
original diagnostic pin. No new native build/sign/publish/installation this turn.
Next operator invocation still must prove actual runtime and communication commit.

## Actual installation accepted / workspace cleanup

User reports running services and successful MB console/control/FM checks.
Read-only SCM confirms four1.13.6 services RUNNING/AUTO. Native fresh-status for
latest install54c027a1-b7b7-41f0-a9c3-abde8b32d2a4 reports committed=true,
aborted=false, receipt16 (historical status, not fresh live-health evidence).
Reboot/fault/remote7031 update-controller acceptance remains separate/unproven.

User explicitly authorized cleanup of build leftovers in this worktree. Added
tools/release/Clean-BuildArtifacts.ps1 (preview default, Apply explicit), selecting
only Git-ignored paths in fixed tools scratch/build/cache areas. Preserves source,
tracked binaries, vendor assets, environment, frozen rollback bin, final dist,
install-kit/version reports/installer-ready evidence and sibling signed checkouts.
No SCM or Program Files/ProgramData mutation during cleanup.

Executed cleanup:2204 files,520.7MiB, zero errors. Actual removed
byte count is authoritative in ignored .release/cleanup-last.json; snapshot
verification passed for759 tracked/non-ignored tools/l4release files before and
after deletion. Old raw fixture logs/binaries referenced above were intentionally
removed; summarized results and selected release/installer evidence remain.
Empty-list bug found on first repeat preview, fixed; repeated preview/Apply pass
with zero files. Cleanup shares the release byte-range workspace lock; actual
running-release-lock refusal passed. No native/backend rebuild or tests needed
for artifact removal. README documents the repeatable operator command.
