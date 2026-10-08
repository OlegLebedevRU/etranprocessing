# l4rollback — supervisor-only bootstrap helper

Initial local implementation, not installed or published. SYSTEM Task Scheduler
registration, bootstrap signing/packaging and live acceptance remain pending.
It is excluded from ordinary `target=suite/updater`: only owner-driven bootstrap
or justified repair installs this immutable executable. Existing helper and
published suite1.13.2 are untouched; new-layout apply remains disabled.

`build.cmd all` builds static `/MT`, `/W4 /WX`, Win7-subsystem x86/x64 and default
x86. `run_tests.cmd x86` and `run_tests.cmd x64` exercise isolated owned files/
children/Job objects, modeled SCM/health, and actual decision/deployment locks.
No test registers a task or mutates a real service. Link maps demonstrate that
the reader build excludes journal replay/write, general installer, marker write,
release/catalog/download code and health server. No WinHTTP/MQTT connection.

Production invocation is fixed: `--operation <original UUID> [--boot]`.
Execution requires LocalSystem and canonical protected
`Program Files\Leo4\Tools\recovery\l4rollback.exe`; roots come from KnownFolders.
Only `--help` is usable portably. No root/path/service/budget/command override.
Signing and exact approved bootstrap hash are enforced by the future installer/
task owner, not selected by this helper.

The producer must authenticate the signed source, image hashes, config snapshots,
original supervisor PID+creation, quiescence and original worker Job before
arming. Worker must belong to a SYS/BA-only protected named Job:
`Global\L4UpdateWorker.{UUID}` (StringFromGUID2 form, including braces).
Producer must create it with KILL_ON_JOB_CLOSE before worker starts, with neither
BREAKAWAY_OK nor SILENT_BREAKAWAY_OK; helper verifies this profile when it exists.
Worker/controller retain the Job handle for the operation. If original worker and
named Job are absent, that producer invariant proves no escaped Job descendants.
Helper does not create/adopt a missing Job or terminate a bare/reused PID.
It terminates only this owned Job and observes its zero ActiveProcesses and
original worker exit; a live matching worker without Job membership refuses.
Job descendants are fenced before deployment.lock is obtained. A reused worker
PID outside the Job remains untouched; helper itself must not be a member.

Private operation files: immutable `supervisor.recovery`, zero-byte
`supervisor.decision.lock` and `supervisor.runner.lock`, atomic `supervisor.result`.
The runner serializes helper instances across decision-lock release; acquire it
without holding the decision lock, then recheck completion. COMMITTED races
STARTED under the existing decision lock. STARTED is durable before worker stop;
release decision lock before stopping worker/acquiring deployment.lock. Missing/
unsafe locks are never created by a reader. Crash releases runner; a later
protected boot task can resume STARTED. FAILED blocks automatic retries.

Under the existing private deployment lock helper pins the exact old executable
and verifies size/SHA256 with a bounded streaming buffer. Only L4Superv with own
process type, LocalSystem, unchanged start type and approved before/after command
is accepted. It retains original/current process handles and waits for exit.
An additional exact-path process observation handles a candidate whose PID was
lost across crash/SCM STOPPED: unowned survivors are waited for, never killed.
Only the two approved supervisor images participate; legacy paths are excluded.

With no approved supervisor process alive, restore only fixed
`ProgramData\Leo4\Tools\config\l4superv.json`. Current bytes/SD must equal old or
candidate; operator drift/ADS/hardlinks/reparse/unsafe writes refuse. Exact old
bytes and owner/group/DACL are restored via owned flushed same-volume temporary
file; original absence deletes only the exact candidate. Then restore only SCM
ImagePath, start old supervisor, verify LocalSystem/PID+creation/image and fresh
private health cycle plus unchanged exact operation/window2 state. No helper
marker clear/deadline extension, communication restart or arbitrary command.
Rollback may happen after the marker deadline; expired pre-stop drain is not used
as a health gate. Full REQ/RSP+EVT/EVA remains the outer recovery-controller gate.

All process/SCM/lock/probe waits share explicit plan recovery_ms (no default
budget inferred). Failure result publication gets at most one extra bounded
decision-lock attempt of 1s when the work budget is exhausted. Late completion
cannot publish RESTORED. Synchronous OS file/SCM calls are not hard interruptible:
Task Scheduler execution bound and fault/blocked-I/O/reboot/Win7 acceptance remain
required. A crash before result leaves STARTED for recovery; no delivery/outbox.
