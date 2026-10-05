# L4 Tools release signing

For every `l4setup` build, pause after preparing the unsigned staging
payloads. Give the operator the command below and wait for confirmation that
it completed. Resume release verification and publication only after checking
the signatures of every staged EXE file (enumerated recursively in both architectures) and `tools/dist/l4setup.exe`.

From the repository worktree in a regular Windows PowerShell session:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\tools\release\Complete-SignedRelease.ps1' -PfxPath '<path to signing PFX>' -Version '<release version>'
```

The script signs both staged payloads, rebuilds the embedded installer, signs
the installer, and regenerates `l4tools-release.json` and `SHA256SUMS`. For an
encrypted PFX, supply `L4TOOLS_SIGN_PFX_PASSWORD` through the process
environment. Keep the PFX and private key outside the repository.

Both payloads and the rebuilt installer receive an RFC 3161 timestamp with
SHA-256 (`signtool /tr /td SHA256`). The default endpoint is
`http://timestamp.digicert.com`; override with `-TimestampUrl` on
`Complete-SignedRelease.ps1`. Timestamp failure, missing timestamp certificate,
or failed Authenticode verification stops signing/publication. The timestamp
preserves verification after the signing certificate expires; it does not
make modified executables or revoked/untrusted signatures valid.

The component build gate is `tools/build_dist.cmd <version>`. If a test fails,
record the failure explicitly. An unsigned build or a failed signature check
must not be published as a signed release.

Do not run `tools/build_dist.cmd` after signing: it rebuilds unsigned components.
The operator uses the existing `Complete-SignedRelease.ps1`; the agent then
verifies the exact signed staging/payload/installer hashes and publishes those
artifacts. Any source change after signing requires another build/sign/verify cycle.
Component versions in the manifest come from staged PE resources and must agree
between architectures. The universal installer remains x86; x64 is also built
for compilation/compatibility checks.

After verification, synchronize the signed first-party binaries from staging
into their build-output paths and commit the intended source/artifact changes.
Keep runtime backups under the ignored `tools/dist/.runtime-backup/` directory.
Refresh only the manifest/checksums from the clean checkpoint with
`New-ReleaseManifest.ps1`; confirm that the installer hash has not changed.
Use strict `deploy/publish_l4tools.py` verification; production releases must
not require `--allow-dirty`.

Publication requires complete HTTPS downloads of the installer, manifest and
checksums with matching sizes/SHA256. A matching registry HEAD Digest alone is
insufficient. If the publisher download is truncated, leave the immutable
version intact and verify complete downloads separately (for example, direct
`curl.exe --noproxy '*'`). Use the publisher's `record` command only after all
three downloads have been verified. Commit/push the release record afterwards.
The 1.9.4 cascade and the 1.9.5/1.9.6/1.9.7 releases used this fallback after truncated
Python downloads; the cause of the transport difference was not established.


For 1.9.5+, `New-PayloadInventory.ps1` writes `l4superv/package-components.json`
inside each staging tree. `Complete-SignedRelease.ps1` refreshes it after signing
and before ZIP creation. The release manifest includes these exact EXE hashes and
versions; both first-party architectures are mandatory and setup PE version must
match exactly. `Test-PayloadIntegrity.ps1` compares every embedded payload file
with staging and all capture PE sections with the current build (signature header
changes and appended certificates do not change those sections). This gate runs
in both unsigned build and signed completion. Never regenerate only the external
manifest when embedded inventory hashes are stale; repack and re-sign setup.

## Incremental releases

### Setup-only

For a setup-only fix, preserve both existing signed staging trees and use
`Complete-SignedRelease.ps1 -PfxPath '<path to signing PFX>' -Version '<release version>' -SignOnly l4setup`.
Update suite/setup version headers and run setup tests/builds for x86/x64 first.
The script refreshes inventories, repacks the unchanged component bytes, rebuilds
and signs setup. Component builds and signatures remain unchanged. The usual
payload, timestamp, clean-checkpoint and complete-download publication gates apply.

### Supervisor-only

For a supervisor-only update, use `tools/l4superv/build.cmd supervisor`, reuse the
previous signed outputs of every other tool, and rebuild staging/setup only.
Use `Complete-SignedRelease.ps1 -PfxPath '<path to signing PFX>' -Version '<release version>' -SignOnly l4superv`.
This signs only staged l4superv x86/x64 and the universal installer, all with the
same mandatory RFC3161 timestamp. Other staged EXEs keep their existing signatures.
The package/inventory/hash gates still run; the unrelated capture source comparison
is skipped. Do not use the full component build gate for this explicitly limited flow.
No live terminal checks are performed unless the operator requests them.

### Proxy/setup network hardening release 1.10.2

Use the existing signed staging for unchanged components and replace only
`leo4proxy/leo4proxy.exe` in each architecture with the fresh 1.8.1 build.
Suite/setup version is 1.10.2. Run proxy unified build/tests and setup tests
for both architectures; refresh inventories, ZIPs and unsigned setup.
Then run `Complete-SignedRelease.ps1 -PfxPath '<path to signing PFX>' -Version 1.10.2 -SignOnly leo4proxy`.
This signs both changed proxy binaries and setup, preserving other EXEs.
The same timestamp/payload/clean-checkpoint/publication gates apply.
See the [resolving/network matrix](../../docs/term_net-leo4proxy-resolving-reliability-matrix.md).

## Published 1.10.1 and documentation updates

[Publication record](../../artifacts/l4tools/1.10.1.json) records the signed setup
and immutable source checkpoint. All three complete HTTPS downloads matched
sizes/SHA-256 using the standard publisher. In the actual signing run components
were re-signed too; all 18 PE code/data/resource sections matched 1.10.0, while
complete EXE hashes changed. Operator Upgrade773 returned ready/0 and subsequently
confirmed video working. This is not a Windows 7 or full outage acceptance result.

Repository documentation was corrected after publication. These text updates
are not inserted into the already-signed embedded payloads of 1.10.1; do not
repack or overwrite that immutable release to update bundled README files.
Current instructions are in the [engineer guide](../../docs/term_tool-user-guide.md).

Known 1.10.1 source/documentation discrepancy: l4setup parses/logs --smoke-only,
but its engine does not consult the flag when selecting the operation. Treat
it as unsupported for guaranteeing diagnostics-only behavior. Use the installed
proxy's read-only probes instead; a code fix requires a separate signed release.

## Published 1.10.2

[Publication record](../../artifacts/l4tools/1.10.2.json): clean main `d43e4d5`, signed setup and all 18 staged EXEs with timestamps, 122 payload files matched. All three full HTTPS downloads matched size/SHA256. Installer SHA256 `6270c908f0275dbb12591f0b377f594a1e959cbe325dc14b1ef467a35e7e4ff1`.

These publication-status documentation edits were made after signing; embedded README snapshots remain unchanged. Do not repack the immutable release for text updates. Operator Upgrade773 to 1.10.2 accepted at 2026-10-04 16:36 UTC: ready/0, proxy1.8.1, MQTT/HTTPS/RTP valid. Outage/Win7/PIN and post-upgrade video acceptance remain open.

## RPC7011 incremental release 1.11.0 (published)

[Publication record](../../artifacts/l4tools/1.11.0.json): 19 timestamped signed EXEs,
130 embedded files matched staging; all three complete HTTPS downloads verified.
Setup SHA256 `3ee234ff1f5ab716a0de43a1b9d26303f9ad11432adc939bbb1e7d73cc1960ac`.
Operator confirmed successful installation of1.11.0 on773. Server migration030
and compatible PB/IoT/MenuBuilder deployed; real7011 rotation acceptance remains open.
Embedded README snapshots remain immutable. The following preparation/signing steps
are historical; do not rerun them against the published version.

Replace only staged leo4proxy1.8.2, l4con1.10.0 and l4pin1.8.0 for x86/x64.
Keep all other signed components from1.10.2 byte-for-byte. Backup prior staging
and setup under ignored tools/dist/.runtime-backup before replacement.
Native builds/tests and setup tests must pass first. Refresh embedded inventory,
ZIPs and unsigned setup; preserve the existing network-profile resource.

In a regular Windows PowerShell session at this worktree, pass SignOnly as an
actual array (not a comma-separated string through powershell.exe -File):

```powershell
& .\tools\release\Complete-SignedRelease.ps1 -PfxPath '<path to signing PFX>' -Version 1.11.0 -SignOnly @('leo4proxy', 'l4con', 'l4pin')
```

The script signs these six component EXEs and the universal setup, rebuilds
inventories/payloads, verifies timestamps and preserves unrelated EXE bytes.
Do not publish before post-sign verification. Schema030 / compatible PB, IoT
and MenuBuilder plus upgraded terminal agent are required before enabling the
UI flow. See [flow matrix](../../docs/term_arch-rpc7011-flow-matrix.md).

## FM incremental release 1.12.0 (signed locally, not published)

Historical preparation below; the wrapper now targets1.12.1. Do not rerun against this signed packet.
Run Prepare-FmSignedRelease.ps1 -Mode Prepare only against the verified signed1.11.0 staging.
It backs up that baseline, builds/tests only l4con1.11.0 x86/x64, preserves all other signed
components and the network profile, regenerates inventories/ZIPs and builds setup1.12.0.
The ignored fm-signing-input.json seals all staged hashes and setup resources.

The operator signs the already-prepared packet, with PFX outside the repository and password
only in L4TOOLS_SIGN_PFX_PASSWORD:

```powershell
& .\tools\release\Prepare-FmSignedRelease.ps1 -Mode Sign -PfxPath '<external PFX path>'
```

Sign verifies the sealed input, delegates to Complete-SignedRelease.ps1 -SignOnly l4con,
verifies payload/signatures/timestamps and preserved component bytes, and synchronizes
signed l4con binaries into build-output paths. Publication is separate. Do not rebuild
components after signing. FM root/API configuration is also separate from signing.

## FM automatic installation / proxy transport 1.12.1

Signed1.12.0 is preserved in the ignored runtime backup. Prepare now requires that signed baseline,
updates only leo4proxy1.8.3/l4con1.11.1, builds/tests setup1.12.1 for x86/x64 and seals a new packet.
The installer automatically creates/protects C:\l4tools\fm. Agent discovers the ready matching-SN
local Leo4Proxy with fm_transport; L4FM_API_URL is no longer used. L4FM_ROOT is an optional admin override.
PB metadata uses loopback HTTP→proxy mTLS; storage uses restricted HTTPS CONNECT through the same
proxy, without client certificate or bypass. PB policy provides the exact S3 host/port. Common
MQTT/RTP/FM deny closes active storage sockets; absent/stale routing fails closed. PB requires fs.proxy.

After preparation, from this worktree:

```powershell
& .\tools\release\Prepare-FmSignedRelease.ps1 -Mode Sign -PfxPath '<external PFX path>'
```

Password only in L4TOOLS_SIGN_PFX_PASSWORD. A new signature is mandatory because native source changed.
Preserve other signed component bytes; do not rebuild after signing. Live Windows/provider/policy E2E
remains distinct from local fixtures; publication follows the existing strict signed-release workflow.
