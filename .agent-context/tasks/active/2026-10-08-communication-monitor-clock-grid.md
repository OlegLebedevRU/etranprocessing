# Communication monitor clock-grid handoff — 2026-10-08

Scope: root-assigned `tools/l4common/communication_monitor.c` plus NEW deterministic
fixture `tools/l4common/tests/test_communication_monitor_clock.c`. Root retains
existing runtime diagnostic fixture, full native gates and all build/run_tests files.

The previous loop read UTC, then tick, and immediately classified an expired
monotonic cap with earlier UTC as TIME_SKEW. Sequential reads can cross an ordinary
deadline; bounded coarse UTC/tick grids also need explicit classification tolerance.
This is a deterministic source defect. The earlier real full-gate failure has not
been attributed to it; root's actual scenario/error diagnostic is still required.

The monitor now resamples UTC after the monotonic cap expires. A checked, fixed
32ms grace applies only to TIME_SKEW classification, with polling bounded by its
remaining time. Persistent rollback or fresh UTC earlier than the original arm
fails closed. Recovery still requires the immutable UTC deadline; no early execute,
deadline extension, guard change or new helper behavior. `monitor_current` live
admission/proof continues to enforce the original monotonic cutoff.

Verified:

- Production source standalone `/MT /W4 /WX`, Windows7-compatible API floor: PASS
  x86 and x64, isolated `tools/l4setup/obj/clock-grid/<arch>`.
- Deterministic fixture: 83 checks / 0 failures on both architectures.
- Cases: ordinary read-crossing with fresh UTC; 16ms grid gap; no early recovery;
  actual UTC deadline executes once; persistent rollback beyond32ms; large remaining
  UTC delta still polls only fixed grace; UTC before arm in either read; checked
  monotonic+grace arithmetic overflow.
- Real production run-loop/event/lock behavior with scripted UTC/Tick/wait values;
  SYSTEM, marker, immutable plan, guard and SCM executor authority explicitly
  modeled. This fixture is not a live watchdog/recovery/service E2E.

Reproduce from tools/l4setup:

```cmd
obj\clock-grid\test.cmd 32 x86
obj\clock-grid\test.cmd 64 x64
obj\clock-grid\production.cmd 32 x86
obj\clock-grid\production.cmd 64 x64
```

No service/config/marker/journal mutation, native output replacement, signing or
publication. No common clock source outside this monitor changed. Root may add the
new fixture to shared run_tests after coordination; this agent did not edit it.

Cleanup: isolated ignored fixture outputs/scripts remain reviewable. An initial
wrong-workdir command created empty nested `tools/l4setup/tools/l4setup/obj/clock-grid`
x86/x64 directories; its misplaced CMD was removed. No secret/live assets there.
Prior policy-blocked empty-directory cleanup is recorded in the outcome handoff;
no bypass attempted here.
