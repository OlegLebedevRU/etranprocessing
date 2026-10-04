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
