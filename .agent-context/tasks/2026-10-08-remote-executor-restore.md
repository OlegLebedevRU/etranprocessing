# Remote executor and whole-old restoration — 2026-10-08

Owner: l4setup native worker. Main/compiled worker capability remains closed;
this packet is composition evidence, not signed runtime fault acceptance or
permission to send RPC7031. No installed services, signing, publication, remote
hosts or frozen helper source/binaries changed by this work.

## Production changes

- `remote_service.c`: keep the exited prior owned process handle through a
  failed StartService. Strict stopped observation/rollback can use this retained
  exit; a running PID is still captured only from this object's issued start.
- `remote_service_transaction.c`: original64/raw10-owned single-hop100/101;
  rollback104/105, audited pending100 abandonment106 and pending21 abandonment107.
  Never blind-repeat, adopt a PID or force-kill. Pending/recovery guards are
  actual original worker/task/Job/WAIT checks in production. Read-only final
  verification allows unique103 SUCCESS bound to exact102+64 SHA/UUID, followed
  only by exact pending clear65 at generation2+1/deadline0. Native complete still
  requires actual active window2; an actually cleared state refuses.
- `remote_restore.c`: opaque original69/source/actor capture before65. No
  installed-source verification/getter is used after SCM mutation. Whole-old
  proof requires all four fresh owned old epochs, all12 old configs, actor ACLs,
  signed inventory, real old app probes and fresh REQ/RSP+EVT/EVA. Window1 Sup
  uses drain2 plus actual independent recovery3 before arbitration; window2
  uses ordinary read-only health0. Both decisions become storage DONE before
  recording108; repeat local checks and DONE after bound RESTORED103 before0.
  Storage DONE is neither a new-suite receipt nor target-source authority.
- `remote_executor.c`: trusted fixed one-hop engine composition only. Reopen
  authenticated source, capture opaque target/old completion before65, probe
  candidate on distinct random49152+ exclusive ports, capture actual worker
  stop/watchdog gates, publish1, confirm, retain ALL4 old native handles, update
  Proxy and probe its actual policy/listeners before broker. Broker START and
  initial probe share one300000 monotonic phase. Fresh existing-channel barrier
  precedes communication DONE and window2. Rebind ALL4; update Con/Sup, apply
  typed config/launcher proposals, invoke opaque final completion600000.
  Known failure promptly restores Proxy and probes old policy before Broker,
  then Con/Sup. Unknown retained epoch, ambiguous unresolved intent, STARTED or
  expired guard leaves marker and honest recovery requirement. A settling or
  existing terminal proof is a no-rollback fence; exact finisher retries once.
  Settling without durable proof records RECOVERY_REQUIRED103 and preserves the
  active marker. Startup preflight only rechecks admission; the sole300s stop
  capture occurs in execute after candidate probing and before65.

## Fixed budgets and compatibility

Central policy owns60min communication /120min helper /130min controller ceilings
and30min whole-old reserve. Window2 deadline is original helper deadline minus
compiled30000ms overhead. Phase2 forward aggregate is1800000, clipped to active
deadline minus reserve. Restoring worst modeled allowance1560s plus240s margin;
initial broker start/probe share5min and final108/102 broker validation is a
separate fresh check inside the aggregate. These are watchdog ceilings, not
normal outage duration and not synchronous WinAPI preemptibility guarantees.

Release compatibility must also cover the reverse intermediate oldProxy/newBroker
mixture. Executor does not manufacture catalog evidence. Main remains closed
until actual owner-signed runtime fault acceptance, including independent
watchdog/boot recovery and all relevant mixtures, is recorded.

## Verification and integration

`tools/dist/.release/evidence/service-transaction-20261008/result.json` and logs
are the local evidence. Final focused runs: native observer682/0 each,
transaction1361/0 each,
whole-old4584/0 each, executor36550/0 each; production `/O2 /MT /W4 /WX`
compiles x86/x64 exit0. Executor fixture advances both monotonic/UTC model clocks
through1739s worst phases and injects each forward adapter refusal; unknown
rollback and sealed102 refusal have explicit cases. Actual native processes,
protected journals and terminal codec are tested separately. Source signatures,
SCM/SYSTEM, app/IPC channels and task/guard responses in composition fixtures are
modeled; these counts are not E2E/runtime acceptance.

Root owns `build.cmd`/`run_tests.cmd` integration. Add production sources
`src\remote_service_transaction.c`, `src\remote_restore.c`,
`src\remote_executor.c`. Fixture sources include their implementation internally:

- transaction / restore: fixture +common `journal.c`, `layout.c`; link
  advapi32,bcrypt,ole32,shlwapi. Do not link included implementation a second time.
- executor: fixture +`src\remote_policy.c`; link advapi32,bcrypt. Metadata, SCM,
  IPC, journal and watchdog adapters explicitly modeled.
- native observer: existing fixture/build deps unchanged; source retained-exit
  addition covered by actual failed-start/exited-process/old restart case.

## Cleanup

New fixture runs verify their fixed temporary root and refuse reparse children,
then clean their own scratch. Two earlier empty-layout temp trees remain:
`L4Restore-188732-2522416218` and `L4Restore-27784-2522326203` under user TEMP.
Automatic review rejected both verified recursive deletion and exact known file
plus empty-directory cleanup; returned only `blocked by policy`, no detailed
reason. No further alternate deletion attempted. Isolated ignored
`obj/service-transaction` runners/evidence remain for root integration; no broad
workspace cleanup was performed.
