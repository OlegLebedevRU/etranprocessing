# Stable tools launcher template

Native Win7+ `/MT` launcher; `build.cmd all` produces x86, x64 and default=x86.
The release pipeline signs and packages `l4launch/l4launch.exe`. The new bootstrap
copies these verified bytes to `<native Program Files>/Leo4/Tools/bin/<tool>.exe`.
The development `l4launch.exe` intentionally refuses to run outside that canonical
location/name. It accepts no alternate root, version, PATH target or legacy fallback.

Each supported CLI name reads its own protected
`ProgramData/Leo4/Tools/config/launchers/<tool>.target`. The exact `L4CLI1` format
contains numeric version, executable size and SHA-256; no path or command fields.
Control parents have SYS/BA ownership/protected policy, SYS/BA write and Users read;
file inheritance is allowed from those guarded parents, with the same permitted
principals. Reparse/hardlinks/ADS, invalid grammar or mismatched executable bytes
fail closed. `l4capture` resolves `bin/l4capture.exe` within its isolated component.

The executable and release ancestors remain pinned until the child exits. The
launcher uses explicit CreateProcess application path, preserves the original
argument suffix and standard streams, waits and returns the child's exit code.
Default CWD is the separately provisioned `state/l4con/work`; neither PATH nor
working-directory provisioning happens during launcher execution. Child receives
caller's token/environment; the launcher does not elevate or change accounts.
Ctrl-C/Break is left to the child while the wrapper waits. Console/process fault
and real ordinary desktop/Win7 tests are still pending.

Template installation uses full verified inventory plus journaled intent/done,
protected file staging, flush and same-directory publish. Existing template bytes
are verified/reused; differing bytes are never overwritten by this API. This is
bootstrap plumbing, not automatic launcher upgrades or the immutable recovery helper.
Pointer changes use the existing per-file journal/config transaction and rollback.

The new installer remains disabled. Tests use temporary explicit roots; no live
Program Files files, services, PATH or terminal MQTT state are changed. This is not
an installable admitted release. Authenticated complete inventory/signature adapter,
bootstrap registration/start, updater state machine and IoT barriers remain pending.
