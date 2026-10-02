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
The 1.9.4 cascade used this fallback after two truncated Python downloads;
the cause of the transport difference was not established.
