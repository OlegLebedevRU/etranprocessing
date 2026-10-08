# L4Update original supervisor watch proof / fixed SCM crash profile

Scope: native monitor/watch read-only proof, fresh owned supervisor registration,
modeled fixtures. Working tree without commit, 2026-10-08. Frozen recovery helper,
installed suite and production SCM were not changed. No MB/PB/API/MQTT calls.

## Delta

- `communication_monitor.[ch]`: independent thread emits readiness only after
  observing its immutable plan/marker; proof requires actual live thread, unspent
  monotonic and UTC deadline, exact original SYSTEM supervisor epoch, expected
  operation/generation/worker/Con epochs, same held immutable plan, live worker Job,
  and existing WAIT decision. Proof never arms, scans, writes or renews a timer.
- `communication_watch.[ch]`: separate proof requires native startup reconciliation
  actually registered and completed, retained watch/profile/monitor/pin, original
  owner PID/creation, marker and deadline, current installed supervisor OWN_PROCESS,
  AUTO_START, LocalSystem and fixed SCM failure policy. Public mode3 query remains
  explicitly ERROR_NOT_SUPPORTED pending full actual failure acceptance.
- `supervisor_crash_profile.h`: exactly one repeating SCM RESTART action after
  1000ms, reset86400s, crash-only flag, no command/reboot message. Delay is a minimum,
  never an OS scheduling promise. Registration changes only provisional owned
  STOPPED L4Superv, journal intent96 before write and readback97 after. Other actors
  preserve prior policy. Phase55/91 and start readbacks require the exact profile.
- Historical completed registration records remain readable; absent profile fails
  remote proof instead of changing already installed services.

## Evidence

- Sup `build.cmd all`: Unified Build COMPLETE, x86/x64/default, exit0;
  `tools/l4superv/obj/policy-gate-unified-build-final.log`.
- Watch fixtures427/0 per architecture; real immutable plan/state/decision/thread,
  modeled SYSTEM/SCM/signals/executor. Public mode3 stays closed.
- Bootstrap fixtures43982/0 per architecture; all SCM mutation is modeled. Fixed
  profile matrix rejects missing/noncrash/runcommand/wrong delay/reset/message;
  commit/local deploy preserve crash-only policy.
- Root actual SYSTEM SCM/SFA acceptance: x86/x64 each97/0, exact fixed profile
  write/read on a CREATE_NEW isolated service, non-crash flag/budget drift refusal,
  self-crash followed by SCM restart with new PID/creation, graceful STOP remains
  stopped. Evidence `tools/dist/.release/evidence/system-sfa-20261008`. Test
  services/task/private launcher assets removed; four installed services Running.
  This proves the actual fixed crash profile, not a full watchdog rollback.
- Protected boot fixtures1364/0 per architecture. Earlier full run had two mode4
  failures under the original 1000ms admission reserve, before final barrier and
  durable UNCONFIRMED result. Unchanged rerun passed; functional fixture now allows
  bounded5000ms for real files/hash/locks, with diagnostic stage/count output.
  Explicit mode8 expiry still uses1000ms; production budgets/timers are unchanged.

## Still required

- Full setup unified gate is owned by root after these bootstrap changes.
- Actual fixed SFA restart is verified above. Complete monitor proof and bounded
  full watch/recovery fault scenarios remain. Never operate installed services for
  these isolated fixtures. Only after full acceptance may root open mode3 integration.
- Independent SYSTEM update worker/apply executor, signed source acquisition and
  result76 integration remain separate chunks; this proof API alone is not E2E.
- Test outputs remain under ignored obj/bin; no temporary live service/task was
  created by this agent. No credentials were read.
