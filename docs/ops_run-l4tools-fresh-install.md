# L4 Tools: readiness and first installation in the Windows layout

Scope: an explicit fresh installation, not C:\l4tools migration and not a live
RPC7031 update acceptance. The terminal certificate and backend contracts are
preserved. Default stand is the local machine, terminal773/tenant1.

## Prepared candidate

The candidate is built and signed through the locked uv pipeline from a separate
clean local Git checkpoint. An offline installation kit additionally contains an
owner-signed seven-day authorization of exactly one release/root hash, both layout
archives, signed descriptors/root and the universal x86 installer. This kit has no
stable promotion or update transition edges. Expired authorization is recreated
into a new directory, never by overwriting a release or an existing kit.

The concrete paths, source revision, hash and verification results are recorded
in the installation-readiness handoff. Signing alone is not communication or
actual installation acceptance.

## Before the actual installation

1. Inspect the original SCM commands/accounts/process epochs and certificate.
   Preserve the exact original system PATH type/value privately for the transition
   record; unrelated entries must remain byte-for-byte unchanged. Per the owner's
   latest decision, do not migrate/archive old tools files or configurations.
   Keep C:\l4tools only until the new installation succeeds, then remove it.
2. Run the exact candidate --fresh-verify command from an elevated interactive
   local Windows console. This prepares protected immutable files and original
   private inputs, and checks admission/certificate/operator tokens under SYSTEM.
   It does not change the four suite services or machine PATH. Save its UUID and
   exit code. A successful verify does not prove the new communication barriers.
3. Check that the local operator can intervene. Verify current IoT availability
   through the existing signal transport before beginning the connection outage.
4. During the authorized real installation, retire the old supervisor first,
   then console, broker and proxy. For every service recheck the observed command,
   account and PID before stopping/deleting it; wait for actual process exit and
   SCM absence; confirm the old listeners on18443/18883/1883 have disappeared.
   Do not run a second MQTT bridge with terminal773's client ID.
   This retirement is a separate operator-controlled step, not installer migration;
   the UAC script below automates it.
   Remove only the reviewed old C:\l4tools suite entries from machine PATH,
   preserving all unrelated entries and the original type. The current stand has
   eight such entries. This is required to prevent older executables shadowing
   the new PF/bin launchers. Native fresh setup appends its launcher directory
   and preserves the PATH it received. No automatic legacy restoration is provided.
5. Inspect the new ProgramData mutable roots. Fresh install requires empty
   component config/state/log directories and no active update.state. Do not
   automatically erase foreign data or a previous incomplete operation. Completed
   verification inputs and immutable candidate binaries may be retained.

## Actual installation command

For the existing legacy stand, use the operator wrapper in
`tools/l4setup/Invoke-L4FreshTransition.ps1` with its adjacent
`FreshTransition.psm1`. It requires Windows PowerShell5.1 (including WMF5.1 on
older Windows), no Python/build environment and no execution-policy bypass.
This reviewed operator script is separate from the sealed signed1.13.5 kit;
it has not been Authenticode-signed or embedded into that release.

```powershell
$kit = '<absolute signed kit directory>'
$pin = '<installer SHA256 from independently reviewed readiness handoff>'
& '<source>\tools\l4setup\Invoke-L4FreshTransition.ps1' -Bundle $kit -Version '1.13.5' -InstallerSha256 $pin
# Add -Apply to the same command for the actual interactive UAC procedure.
```

Default invocation is read-only. `-Apply` uses a visible native Windows PowerShell
operator session, requesting UAC if needed; rejection exits before changes.
It holds the pinned signed EXE and admission inputs, runs native fresh-verify,
then rechecks exact original legacy SCM/image/hash/process-creation fingerprints.
The four services must initially be running, AUTO, own-process, SYSTEM and use
their exact C:\l4tools component images; foreign dependents/configurations refuse.
It saves only a protected operation record and original system PATH under
ProgramData\Leo4\Tools\operator-transition. It does not archive old configurations.
Retirement order is supervisor, console, broker, proxy; Mosquitto has300s, other
stops120s each. No forced kill, automatic resume or old-service restoration.
After process exit/SCM absence and free ports, it removes only the reviewed old
PATH entries (exact type/raw unrelated entries, drift/readback checks) and calls
the native installer with a recorded UUID. After native success it deletes the
fixed C:\l4tools directory, refusing reparse descendants or processes still using
it. Cleanup refusal after install means installation succeeded but cleanup is
incomplete; inspect the operation's stage rather than reinstalling blindly.
The operator must confirm current IoT availability before accepting the outage;
fresh-verify is admission/certificate preparation, not the pre-outage IoT barrier.
There is no atomic guarantee against concurrent administrative service/PATH writers.

Local guard/refusal tests and a read-only plan have passed. Actual UAC approval,
retirement, PATH mutation, successful cutover/failure interruption and old-tree
deletion have **not** been executed on the live stand. Native installation and
its communication barriers still require the controlled installation acceptance.

Run the kit's universal installer from an elevated interactive local console:

```powershell
& '<kit>\l4setup.exe' --fresh-install --bundle '<kit>' --fresh-version '<version>' --arch x86
```

Save the printed operation_id. The installer first authenticates the complete
package and captures the original operation. It prepares protected configs and
launchers, registers four provisional manual SYSTEM services and sets the owned
broker's service-local MOSQUITTO_DIR. It starts/probes proxy, then broker (five
minutes), then console, requires a fresh REQ/RSP plus EVT/EVA barrier, starts
supervisor, then rechecks before commit. Commit selects startup types and PATH;
only then the parent broadcasts the Environment change. If a mandatory check
fails before commit, managed abort stops/removes only original owned services
before restoring configs/PATH. Unknown/foreign state is preserved and reported.

## Status and recovery

After an operator cutover has retired the old services, do not rerun the legacy
transition wrapper. For an inspected uncommitted receipted operation with a
stopped supervisor and known restarted broker/Con epochs, the separate
`tools/l4setup/Repair-L4FreshOperation.ps1` prepares an explicit operator repair.
It requires exact original/new signed installer pins, validates original native
status/journal/commands/image hashes and all current SCM owner markers, and is
read-only unless Apply is selected. Apply verifies both bundles before stopping
the inspected processes, restores only the original accepted broker candidate
and exact native security descriptor, and calls native recovery using the
**original** UUID. Complete abort is mandatory; no epoch record/journal rewrite,
automatic resume, force-kill or legacy restoration. Only reviewed failed runtime
files can be retained under the private original operation's diagnostics before
a new clean install. Unknown files, changed epochs/profile or incomplete abort
refuse. This controlled repair itself still needs live operator acceptance.
After successful original configuration rollback and diagnostic retention, all
three fresh runtime roots must contain no entries, including empty subdirectories.
The operator wrapper validates the whole tree, then removes only empty descendants
deepest-first without recursive/forced deletion. It preserves the roots and private
diagnostics. Do this after original recovery: its config plans need parent paths.
Files, links or concurrently recreated entries refuse cleanup and installation.
Fresh admission also requires no fixed `update/operations/update.state`. An
original bootstrap may have provisioned it before later abort; never delete or
reset an active/unknown state to enable fresh installation. In the inspected773
incident only exact generation-zero bytes, checksum, original provisioning
journal record and absent services permitted private retention in that original
operation. Completed original configuration rollback is verified from all13
native rollback-DONE records before removing its now unnecessary parent paths;
do not replay that completed rollback after the paths have gone.
PF/bin launchers can also survive an aborted installation. Native installation
accepts existing launcher copies only when their bytes match the candidate's
approved l4launch hash/size; it does not overwrite another release's copies.
Operator preflight rejects mismatched or unknown entries before install. Retire
only explicitly inspected original signed copies under the original private
operation, with services/processes absent and completed abort evidence. Preserve
immutable release directories; never replace an executable merely to bypass CRC.
Concrete1.13.5 incident / signed1.13.6 preparation / local launcher are recorded
in the2026-10-07 fresh-install epoch-fix handoff.

```powershell
& '<kit>\l4setup.exe' --fresh-status --fresh-version '<version>' --operation '<original UUID>' --arch x86
& '<kit>\l4setup.exe' --fresh-recover --fresh-version '<version>' --operation '<original UUID>' --arch x86
```

Status reports historical commit/abort, never current health. Recovery reconstructs
only the original accepted metadata and receipt85; it can abort after catalog
expiry, but cannot abort successful commit or forward replay an interrupted apply.
A crash before receipt85, changed process epochs after reboot or foreign state
requires explicit owner inspection/repair. Do not generate a new UUID to adopt
unfinished services or bypass these gates. A parent timeout may leave its owned
SYSTEM host still executing; inspect status rather than killing an arbitrary PID.

## Acceptance after actual installation

Confirm all four canonical SCM commands/accounts/start types, expected epochs,
new proxy identity, exact broker service environment and actual owned listeners.
Repeat fresh REQ/RSP and EVT/EVA through existing L4Con IPC, verify backend
contracts and the operator's desktop launch/access. Then perform separately
reviewed reboot/fault recovery acceptance. None of these results may be inferred
from a signed candidate or from a verification-only host. Frozen helper is not
rebuilt or replaced by this procedure; live RPC update controller acceptance
remains a separate outstanding implementation gate.
