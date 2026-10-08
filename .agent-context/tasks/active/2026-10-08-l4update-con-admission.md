# Con7031 admission adapter groundwork

Owner: existing Con extra_service client. Root owns the setup SYSTEM controller
engine and acceptance enabling. No new MQTT client, installed actor, live RPC,
helper or MB/PB changes. IoT marked-probe/result76 server delivery is separately
recorded in the IoT release handoff.

## Boundary implemented

`update_admission.[ch]` has one pending/running request and a separate thread;
MQTT dispatch never waits for IoT/controller ACK. Input is only original7031
task UUID, exact canonical version/latest and suite/updater target. No URL/path,
budget, caller capability or environment escape. The authoritative common host
boundary enforces canonical installed Con, primary SYSTEM and signed setup image.

Default `L4CON_REMOTE_ADMISSION_ENABLED=0` explicitly returns501
controller_engine_unavailable without creating journal/SCM state. Updater target
always501 updater_engine_unavailable because its independent identity/engine is
absent. During an active update window7031 refuses409;7032 remains available.

The future enabled native path creates the original UUID journal, flushes92 and
launches the independent SYSTEM host using the existing common API, fixed120s
startup budget. Only real durable93/live ownership proof plus exact receipt target,
version, accepted timestamp and process epoch permits202 controller_accepted.
Storage paths canonicalize UUID hex case; the reply retains original task text.
Failed launch is an admission response, never a fabricated update result94/95 or
host kill/retire. Authoritative owner handles any durable partial state.

Shutdown rejects new tasks, discards a pending task and joins the active callback
before context free. A timed-out close preserves allocation; Con fail-closes its
own process rather than freeing a live callback context. Con waits at most125s
for admission teardown. Pre-ACK engine verification is strictly local; long
IoT/catalog preparation starts after93. Existing SCM STOP_PENDING hint5s is not
proof that a manual stop during pre-ACK verification finishes within that hint;
normal remote quiescence starts after93, when admission has completed.

Link/probe/drain proof additionally refuses while the owned adapter request or
reply callback is pending/running. It returns idle after that callback retires;
independent controller execution and7032 reporting never keep this bit busy.
Gate0 always returns idle. Local proof endpoint callbacks are joined before the
adapter context is freed, and the endpoint starts only after adapter init.

## Verification

Focused `tests/test_update_admission.cmd`: closed/enabled-mode model on x86/x64
PASS. Tests cover invalid original UUID/version, updater refusal, closed gate
without backend mutation, copied input, asynchronous blocked backend, busy slot,
close timeout retaining context and refusing new work, join, separately blocked
reply callback retaining BUSY and timed-out close, callback retirement busy/idle,
mismatched receipt
and native launch failure never202. Backend is modeled; these are not real host
or recovery acceptance. No real SCM/broker/file mutation in the fixture.

`tests/test_rpc_runtime.cmd` both architectures PASS, including actual MQTT
dispatch's closed7031 response and actual private probe/drain BUSY/timeout while
the adapter predicate is active, then successful drain after release.
Existing7032/event76/orphan/drain checks retained.
Logs: `tools/l4con/obj/admission-rpc-runtime-final.log`. Final unified
`build.cmd all` x86/x64/default COMPLETE, exit0, includes both guard modes and the
callback-retirement fixtures, retained reporting/FM/state tests:
`tools/l4con/obj/admission-quiescence-unified-build.log`.

## Still closed

No release enablement until root proves real executor and full watch/recovery
acceptance. A pure compile-time fixture that models93 is never that acceptance.
No successful update on773 is claimed by this adapter packet.
