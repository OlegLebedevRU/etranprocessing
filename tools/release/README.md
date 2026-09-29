# L4 Tools release signing

For every `l4setup` build, pause after preparing the unsigned staging
payloads. Give the operator the command below and wait for confirmation that
it completed. Resume release verification and publication only after checking
the signatures of all 18 staged EXE files and `tools/dist/l4setup.exe`.

From the repository worktree in a regular Windows PowerShell session:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\tools\release\Complete-SignedRelease.ps1' -PfxPath '<path to signing PFX>' -Version '<release version>'
```

The script signs both staged payloads, rebuilds the embedded installer, signs
the installer, and regenerates `l4tools-release.json` and `SHA256SUMS`. For an
encrypted PFX, supply `L4TOOLS_SIGN_PFX_PASSWORD` through the process
environment. Keep the PFX and private key outside the repository.

The component build gate is `tools/build_dist.cmd <version>`. If a test fails,
record the failure explicitly. An unsigned build or a failed signature check
must not be published as a signed release.
