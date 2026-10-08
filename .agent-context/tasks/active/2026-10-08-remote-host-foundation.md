# Independent SYSTEM remote-update host foundation

## Task intake
- Goal: original RPC7031 task UUID owns a persistent request and an independent authenticated SYSTEM controller, before any acceptance response.
- Scope: common remote host/ACK93 APIs, setup controller entry and build/test wiring. No MQTT contracts, live SCM changes, release publication, frozen helper modification or forward apply.
- Producer/consumer: installed SYSTEM L4Con flushes92 -> fixed private transient SCM host outside the console Job -> authenticates installed source and its own signed installer -> real engine preflight -> flushes93 -> parent independently observes full-chain protected journal plus live SCM/process proof.
- Invariants: no arbitrary URL/path source, no nil UUID, no source-version authority from argv, no false202/no-op engine, exact private service identity and one original PID/creation-time epoch; strict remote signature policy. All service actions refer only to this transient owned host.
- Verification: x86/x64/default native build and focused /WX Win7-target fixtures; actual live SYSTEM service launch/engine remains untested and disabled.

## Implemented
- common `remote_host.h/c`: canonical installed primary SYSTEM/session0 L4Con launch only; original request92 must be first record and target=suite. Updater target refuses before host creation.
- Holds fixed PF/setup/source-version installer and ancestors against write/delete before SCM create/start; exact LocalSystem/own-process/DEMAND command, UUID display marker and protected two-principal SY/BA DACL.
- Closes/transfers original deployment journal before SCM start. SCM supplies an independent service process; closing the observation context cannot kill the host or the future worker Job. Timeout never forces termination or adopts a foreign/live service.
- ACK93 binds original request timestamp, source version/arch, host PID/creation, size/SHA256. Live reader performs independent SYSTEM, private ACL/owner, exact UUID/header/full-chain checks without taking deployment lock or repairing a torn journal. Partial snapshots are retried within one monotonic deadline.
- Parent repeats SCM configuration/security/current PID, process creation, image, SYSTEM/session0 and alive checks after reading identity; parent-held EXE fence compares ACK hash/size. ACK is ownership evidence, not update success or transport readiness.
- setup `remote_entry.h/c`: authenticates existing installed source, source version/arch, held self hash/size and strict signed publisher admission. Compile-time real engine must exist; preflight is read-only, cannot append journal records or wait on IoT while parent waits in MQTT callback. Fresh source/SCM and cancellation are rechecked before93.
- `main.c` explicitly supplies NULL engine: unsupported/incomplete engine refuses before journal/source/ACK. No CLI enabling override. Full route preparation and forward apply are not advertised as working.
- Engine execute owns subsequent durable results, preparation cancellation, retained worker Job and journal handoff. Transient host retires only its own exact private SCM registration on finish.
- `build.cmd` includes remote_request/host, journal_reader, installed_source and remote_entry. `run_tests.cmd` integrates five focused tests.

## Validation
- x86/x64 each: durable original request/exact retry fixture PASS, strict/live reader 51 checks, installed-source receipt guards 37 checks, remote-host codec/private SCM contract 102 checks, remote entry guards 11 checks; all 0 failures.
- Host tests model SCM configuration/DACL/ACK bytes and exercise actual ordinary-token refusal. Entry tests fail loudly if source/signature callbacks unexpectedly execute; absent/incomplete engine refuses without ACK. These are not successful SYSTEM host lifecycle E2E evidence.
- Unified final `tools/l4setup/build.cmd all` completed exit0 with fresh x86/x64/default artifacts and no warnings; unsigned development artifacts only. No installed1.13.6/service changes, no signing/publishing.

## Remaining
- Actual SYSTEM host launch/ACK observation with authenticated signed fixture and meaningful nonempty engine, retained controller/worker Job across console restart, startup timeout/crash/race fault tests.
- No connection of main engine or 7031 acceptance until actual forward-update implementation is ready.
- Preparation controller waits for owner-backed runtime profile mapping: release catalog accepts supplied profile IDs; found windows-10-x64/windows-7-x86 values only in tests. No invented fixed LocalSystem catalog profile or new network selection.
- Native synchronous SCM/trust/file APIs cannot be interrupted by monotonic checks; deadline is checked around them and no guaranteed hard interruption is claimed.
