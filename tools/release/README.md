# L4 Tools release signing

## Unified pipeline

Run from the repository root with Python 3.14. The root uv project and lockfile
own release dependencies; the existing `tools/pyproject.toml` contract-test project
is independent. The ordered component plan is [config.toml](../../l4release/config.toml).

The pipeline starts with an administrative, isolated SCM regression for both
architectures. It creates one unique stopped test service, verifies supervisor
restart configuration rights, then deletes that service and checks its absence.
It never starts the test service or opens a Tools Suite service. Run the pipeline
from an elevated Windows session; missing SCM privileges fail this gate explicitly.
Ordinary native `build.cmd all` does not run this test; its explicit entry is
`tools/l4setup/build.cmd test-scm`.

```text
uv run --locked python -m l4release plan --version <version>
uv run --locked python -m l4release prepare --version <version>
uv run --locked python -m l4release release --version <version> --env-file <base-project>/sw_sign.env --no-publish
uv run --locked python -m l4release release --version <version> --env-file <base-project>/sw_sign.env
```

`plan` has no build/sign/publish effects. `prepare` is unsigned local validation,
including existing native build gates and l4setup tests for both architectures.
It works with dirty sources but never publishes. `release` requires a clean
source checkpoint, automatically signs from the supplied env, rebuilds only
the installer around signed payload, verifies every staged EXE and embedded
payload, and publishes a **candidate**, using the existing strict publisher.
`--no-publish` stops after signed verification. `verify --version ... --env-file ...`
checks existing signed files without rebuilding/signing. Ordinary commands never
call IoT or change terminal services.

The owner explicitly approved automatic signing: the old mandatory operator pause
is replaced by this pipeline. `Complete-SignedRelease.ps1` remains the signing
implementation; its direct operator command below is an optional maintenance path.

Candidate publication does not imply transition admission or stable. Authenticode
and owner-signed metadata are mandatory. The explicit `acceptance-plan` and
`acceptance-run` commands use the local engineer entry and the common native
executor; `catalog-plan` and `catalog-publish` admit measured transitions.
The historical `release --with-terminal-gate` and `promote` switches remain closed;
ordinary release commands do not touch terminal services. See the accepted
[architecture](../../docs/term_arch-l4update-flow.md).

### Credentials and key creation

Use only the explicitly supplied `sw_sign.env` (default: repository root); no
search or fallback into `.env` profiles. Relative secret-file paths are resolved
against its directory. The file must not be tracked.

```dotenv
SW_SIGN_PFX=
W_SIGN_PFX_PASSWORD=
AR_GENERIC_KEY_ID=
AR_GENERIC_KEY_SECRET=
L4TOOLS_METADATA_KEY_PATH=
L4TOOLS_METADATA_KEY_PASSWORD=
IOT_API_KEY_773=
```

`SW_SIGN_PFX_PASSWORD` is accepted as an alias for `W_SIGN_PFX_PASSWORD`.
Set either spelling; if both contain different nonempty passwords, the pipeline
refuses before signing without printing either value. A nonempty spelling takes
precedence over a blank alias. Passwords from the shell environment are not used.

Registry fields are the existing Generic Registry **write** credentials. Read
downloads are public. A blank signing password is only suitable for an unencrypted
PFX. IOT_API_KEY_773 is reserved
for a later explicitly enabled terminal gate and is not passed to build children.

For one-time metadata key creation, create a protected directory **outside Git**,
set an absolute private PEM path and a separate password, then run:

```text
uv run --locked python -m l4release keys init --env-file <base-project>/sw_sign.env
```

This creates encrypted PKCS#8 RSA-3072 PEM, a sibling `.public.pem`, and prints
only the public key ID after a sign/verify self-test. Existing files are never
overwritten. Protect the directory with Windows ACL and retain an owner backup.
The native verifier embeds the reviewed public key. Its identity must match the
external signing key before release or catalog publication.

### Checkpoints, inputs and missing dependencies

After completing a build/release, preview local scratch cleanup from the repository
root with `./tools/release/Clean-BuildArtifacts.ps1`; add `-Apply` to remove it.
The script uses the release workspace lock and selects only Git-ignored artifacts
under explicit tools build/cache paths. It preserves tracked/untracked sources,
vendor dependencies, frozen rollback binaries, final distribution files,
install-kits, numbered release reports and retained installer evidence. It never
touches Program Files/ProgramData or sibling signed release checkouts. Generated
staging/components are removed, so an unfinished signing checkpoint should be
completed before cleanup. Removal details are in ignored `.release/cleanup-last.json`.

Reports/logs live under ignored `tools/dist/.release/<version>/`. A workspace
lock excludes concurrent builds of different versions. Checkpoints bind exact
source/config/dependency hashes and output bytes. Source changes invalidate reuse;
review the change and use a fresh version/checkpoint. There is no dirty-release
or failed-test bypass. After a signing failure, intact retained staging can be
re-signed without rebuilding components. Signed checkpoints still undergo full
signature/payload verification on every publication attempt. Changing PFX identity
does not silently replace an existing release.

Generated version headers/binaries are separated from source inputs. Only when
the run started clean and input hashes remain unchanged can final manifest
provenance mark the source checkpoint clean. No arbitrary caller-provided dirty
override is available. Actual inputs digest and Git revision are recorded.

Preflight checks native dependencies before building. In particular, the current
l4capture build requires prepared `vendor/openh264` headers and x86/x64
encoder/common/processing static libraries. Missing assets stop the pipeline;
old EXEs are not substituted. The recovered working OpenH264 2.6.0.2502 dependency
is pinned byte-for-byte in `l4release/openh264.lock.json`: four API headers and
six static libraries, verified before every build. To populate another worktree:

```text
uv run --locked python -m l4release import-openh264 --source <working-vendor>/openh264
```

The import verifies all source hashes before copying and refuses to overwrite a
different dependency. Keep these locked build assets in the build environment;
this is recovery of existing libraries, not an attested rebuild from Cisco source.
A source revision and a reproducible codec compilation recipe remain outstanding.

Plan limits are maximum **child-command** times, not mandatory waits or measured
typical duration. Hashing/filesystem operations and child cleanup are additional.
Per-command actual duration is recorded. For restricted environments, set
`UV_CACHE_DIR` to a writable local directory; the build_dist compatibility wrapper
defaults it to repository `.uv-cache`.

Windows PowerShell children rediscover their native module paths instead of
inheriting PowerShell 7's `PSModulePath`. Native OS gates require an execution
profile with access to the Windows desktop, key storage and test ACL operations.
Codex's isolated Windows sandbox can deny those operations; use the appropriate
local execution profile rather than bypassing tests.

### Direct maintenance signing

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

The compatibility build gate `tools/build_dist.cmd <version>` now delegates to
`l4release prepare`; it has no independent component list. If a test fails,
record the failure explicitly. An unsigned build or a failed signature check
must not be published as a signed release.

Do not run `tools/build_dist.cmd` after signing: it rebuilds unsigned components.
The pipeline uses `Complete-SignedRelease.ps1`, verifies the exact signed
staging/payload/installer hashes, and publishes those artifacts. Direct incremental
maintenance commands below remain available; they do not establish unified pipeline
checkpoint evidence. Any source change after signing requires another
build/sign/verify cycle and must not overwrite a published version.
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

## FM automatic installation / proxy transport 1.12.1 (published)

[Publication record](../../artifacts/l4tools/1.12.1.json): all19 timestamped signed EXEs valid,
all130 embedded files match staging; all three complete HTTPS downloads verified.
Setup SHA256 `838615b489a3cf7fa344e4244c63d145ee34772d3cc747dbf2e70d926a71adaa`.
Preparation/signing steps below are historical; do not rerun against this published packet.
Embedded README snapshots remain immutable. Installation and live terminal FM E2E remain open.

Signed1.12.0 is preserved in the ignored runtime backup. Prepare now requires that signed baseline,
updates only leo4proxy1.8.3/l4con1.11.1, builds/tests setup1.12.1 for x86/x64 and seals a new packet.
The installer automatically creates/protects C:\l4tools\fm. Agent discovers the ready matching-SN
local Leo4Proxy with fm_transport; L4FM_API_URL is no longer used. L4FM_ROOT is an optional admin override.
PB metadata uses loopback HTTP→proxy mTLS; storage uses restricted HTTPS CONNECT through the same
proxy, without client certificate or bypass. PB policy provides the exact S3 host/port. Common
MQTT/RTP/FM deny closes active storage sockets; absent/stale routing fails closed. PB requires fs.proxy.

Historical signing command after preparation, from this worktree:

```powershell
& .\tools\release\Prepare-FmSignedRelease.ps1 -Mode Sign -PfxPath '<external PFX path>'
```

Password only in L4TOOLS_SIGN_PFX_PASSWORD. A new signature is mandatory because native source changed.
Preserve other signed component bytes; do not rebuild after signing. Live Windows/provider/policy E2E
remains distinct from local fixtures; publication completed through the existing strict signed-release workflow.

## FM v2 release 1.13.0 — signed and published

Prepare-FmSignedRelease.ps1 uses signed 1.12.1 as the baseline, preserves other signed
components and stages l4con1.12.0/l4superv1.11.0 plus setup1.13.0. The packet contains
FM v2-only MQTT navigation/confirmed close, ordinary-user writes, Explorer drive policy,
and explicit Mosquitto contract3 migration. Setup preserves terminal config/ACL and
rolls the previous suite back on failed migration/start/critical health during upgrades.

Operator command from this worktree:

```powershell
& .\tools\release\Prepare-FmSignedRelease.ps1 -Mode Sign -PfxPath '<external PFX path>'
```

Password only through L4TOOLS_SIGN_PFX_PASSWORD. Sign verifies sealed staging inputs,
signs changed components and rebuilt installer, and validates embedded payloads.
After operator confirmation: verify all signatures/hashes, publish immutable1.13.0,
then update terminal1000007 using the user-authorized l4mcp flow and verify FM.

Publication record: [1.13.0](../../artifacts/l4tools/1.13.0.json). All19 timestamped
signatures valid,128 embedded files match staging, all three complete HTTPS downloads
verified. Setup SHA256 d9cb76c947d5358093e8b93bd4bd505259fb2987babcce705428c98184d71530.
Commands above are preparation history; do not rerun/repack the immutable published packet.
Operator installation on 1000007 verified via l4mcp: suite 1.13.0 ready, services running, signed binaries and explicit MQTT routes valid. Live FM navigation, transfer/hash and close/reopen passed; see the FM improvements handoff for remaining checks.


## Prepared 1.13.1: Explorer sorting

The current Prepare-FmSignedRelease.ps1 targets 1.13.1, preserving the signed
1.13.0 baseline in its private backup. L4Con 1.12.1 sorts complete listings before
pagination; all other payload components keep their signed baseline binaries.
Preparation/build/tests and embedded payload verification passed. Operator signing:

```powershell
& .\tools\release\Prepare-FmSignedRelease.ps1 -Mode Sign -PfxPath $env:L4TOOLS_SIGN_PFX
```

Password comes only from L4TOOLS_SIGN_PFX_PASSWORD. Signing uses the existing
Complete-SignedRelease.ps1 and verifies the sealed inputs. Publication and terminal
upgrade follow only after signature and payload verification. No publication yet.


1.13.1 signed/published and remotely installed on 1000007 through l4mcp.
[Publication record](../../artifacts/l4tools/1.13.1.json): full HTTPS GET checks passed.
Setup ready/exit0, all services running; signed L4Con 1.12.1 and fresh PB v2
registration verified. One-shot update task and temporary S3 object removed.
The signed 1.13.1 packet is immutable; do not prepare or rebuild it again.


## Установка опубликованной suite через l4mcp

[Runbook и точный промпт агенту](../../docs/ops_run-l4tools-update-via-l4mcp.md).
Подпись/публикация и установка — отдельные этапы. Установщик доставляется через
приватный S3/Leo4Proxy и запускается независимой SYSTEM-задачей; повторный запуск
после потери ответа допускается только после выяснения состояния прежней задачи.


## New layout payloads (pipeline; installation still disabled)

`uv run --locked python -m l4release` now produces additional fixed-name
`l4tools-layout-{x86,x64}.zip` and `.json` assets. Each deterministic archive has a
complete schema1 inventory: version/architecture/publisher certificate SHA256,
archive SHA256 and every file's path/size/SHA256. Ten native executables and three
configuration templates are mandatory. Old installation scripts, legacy inventory,
unclassified configuration, runtime logs/state and private credentials are refused
or explicitly excluded. Bounds match the native consumer:64 files,512MiB per file,
1GiB total. Unsigned preparation has a null publisher; native admission refuses it.
Signing rebuilds the assets from signed staging before sealing the checkpoint.

Templates remain immutable inside the release; their ProgramData destinations are
planned separately. `base_path` is removed from the supervisor template, allowing
executable-derived release/ProgramData paths. No configuration is applied here.
Publisher validates all four assets and their hashes/sizes/identities, uploads
payloads before checksums and publishes `l4tools-release.json` last. Existing
registry release prefixes suffice; no additional registry section is required.
Historical three-file releases remain immutable and verifiable. No1.13.2 repack.


## Detached metadata signature format

The shared producer API `l4release.metadata.sign_bytes` signs exact document bytes
using RSA3072, exponent65537, PKCS#1v1.5/SHA256. The sidecar `.sig` is exactly384
raw signature bytes (big-endian RSA), without JSON/base64/envelope/canonicalization.
Document bound is1..65535 bytes. `public_blob` produces the411-byte Windows public
RSA blob; `key_id` remains SHA256 of DER SubjectPublicKeyInfo as in `keys init`.
The owner-managed trust root must be embedded by a signed bootstrap build; a
public blob received with metadata is never a trust root.

`load_signing_key` reads the existing externally stored encrypted key through
L4TOOLS_METADATA_KEY_PATH/L4TOOLS_METADATA_KEY_PASSWORD. It creates/rotates nothing.
This library is not yet wired into release checkpoints/upload/metadata acquisition;
unsigned existing releases are not retroactively given metadata signatures.
Python/native integration uses ephemeral in-memory test keys and public artifacts,
with real CNG verification on x86/x64. Owner secrets and published1.13.2 untouched.


## Metadata signing wired into the release pipeline

Signing preflight requires the existing encrypted metadata key, its public companion
and a match with the compiled owner trust root in `tools/l4common/metadata_owner_key.h`.
The pipeline pins the DER public-key ID in its report. After signed staging and final
provenance it emits both layout `.json.sig` files, inventories their hashes/sizes,
adds `metadata_signatures` (schema1, algorithm, key_id, fixed root signature name),
signs exact final root bytes and writes checksums covering all signatures/documents.
The root signature is outside its own document to avoid a circular digest.

Signed checkpoints include all three signatures. Metadata finalization failure
retains the signed staging checkpoint, preventing component rebuild on retry.
Verification requires signed metadata and the independently configured public key.
Publisher reads only the `.public.pem` companion; no private key/password is needed
for publication checks. New layout publication refuses absent metadata signatures.
Fixed upload set has10 files: setup,4 layout assets,3 signatures, checksums, root
release document last. Full download/audit loops include signatures. Historical
three-file verification remains available; published1.13.2 is unchanged.

Initial owner key is now provisioned outside the repository with protected access;
path/password live only in external sw_sign.env. Private key rotation is never
automatic. Public root/header and non-JSON self-test signature are safe tracked
inputs. No owner-signed release packet was created/published in this stage; tests
use separate ephemeral keys for packet fixtures. Signed root/catalog parsing and
revision/expiry storage and native acquisition APIs are implemented; prepared
packages, worker connection and installation remain pending. The config loader
requires the Registry authority to match the compiled native consumer before build/
sign/publication; changing only the publication URL is rejected.


## Signed catalog schema1 (local APIs; publication not enabled)

`l4release.catalog` validates and signs exact catalog bytes using the same owner key.
The exact root fields are schema/key_id/revision/issued_at/expires_at/stable/releases/
transitions. Times are positive UTC Unix seconds; revision is positive u64. Each
release has version/manifest_sha256/revoked. Each directed transition has from/to/
arch/profile/evidence_sha256. Versions match native canonical three-part numeric
layout (each part<=65535); profile is a bounded lowercase ASCII evidence identifier,
separate from bundle architecture so x86-under-WOW64 is not proof of Win7/x86.
Bounds24 releases and24 transitions fit the existing512-token native JSON reader.

`stable=null` refuses latest; stable must name a present nonrevoked release. Route
selection uses a shortest directed chain filtered by exact architecture/profile,
with pinned root manifest hashes; no compatibility inference from compilation or
transitivity. Revoked destinations/intermediates are excluded; a revoked installed
version can leave only through an explicitly admitted edge. No-op requires exact
installed digest. Duplicate releases/edges/JSON keys, zero/invalid digests, future/
expired catalogs, revision rollback or same revision with different bytes refuse.
Signatures are checked before document interpretation. Evidence/promotion commands
and owner review of actual gate results remain required; catalog APIs fabricate none.

Catalog publication uses the existing Registry prefix `l4tools/metadata/`;
reviewed plan/publication commands are implemented. No catalog upload or promotion
was performed during this source integration; actual acceptance is still required. Network acquisition
and scoped773 trial admission remain pending. Native signed root release admission
now consumes the exact finalizer format: catalog-pinned version/root hash, owner
signature, clean signed source, both fixed-name architecture inventories and bounded
sizes/hashes. Root-bound descriptor admission checks its signature, publisher and
ZIP identity before handing off to the existing signed protected extraction gate.
This local read-only admission does not establish connectivity, saved-operation
recovery, service switching or terminal compatibility evidence.

## Initial immutable recovery bootstrap

`l4rollback` is outside normal suite/updater compilation and signing. The first
bootstrap seal copies the owner-reviewed existing x86/x64 native bits, verifies
their fixed approved size/hash and PE architecture, signs only those copies with
the existing certificate and RFC3161 pipeline, then verifies the signed reference
release's publisher. Its source/bin artifacts are never rebuilt or modified.

After a clean reviewed source checkpoint and a verified signed reference release:

```powershell
uv run --locked python -m l4release bootstrap-seal --version <signed-reference-version> --env-file <external-sw_sign.env> --output <new-external-immutable-directory>
```

Add `L4TOOLS_BOOTSTRAP_DIR=<external-immutable-directory>` to the external
`sw_sign.env`. Ordinary `install-kit` reuses the exact sealed assets without
signing them again. An existing/partial seal is never overwritten or re-sealed.
No bootstrap Registry upload is implemented by this command.

The kit adds fixed `l4tools-bootstrap.json/.sig` and `l4rollback-x86.exe` /
`l4rollback-x64.exe`. This independent owner-signed schema1 metadata binds the
fresh release version/root hash and both helper identities; the kind
`frozen-supervisor-helper` is fixed supervisor-only ABI1. Root/catalog schemas
are unchanged. Native fresh verification requires owner signature, root binding,
held helper hash and publisher Authenticode under the explicit local policy.

Fresh SYSTEM installation creates protected `Program Files/Leo4/Tools/recovery`
and keeps the original signed receipt and selected helper there. Existing complete
identity can be reused, including a receipt bound to an earlier fresh root; helper
size/hash/publisher must match exactly. No automatic replacement, ACL repair or
partial-directory adoption occurs. A partial initial directory returns
`ERROR_NOT_READY` and requires an owner-reviewed repair; it does not trigger suite
mutation. Read-only fresh verification checks any existing helper before reporting
success: absence under verified PF ancestry is allowed, partial/unsafe/mismatched
identity is refused before the operator transition stops communication.
Abort preserves this immutable bootstrap. Remote receipt loading remains
strict and holds helper/receipt/ancestor handles for its owned lifetime. These gates
do not enable the remote engine or prove a live watchdog/rollback cycle.

Owner-signed catalog plan/publication and pipeline acceptance boundary:
[catalog-publishing.md](catalog-publishing.md). Admission requires pipeline compatibility checks,
one real forward transition and one forced rollback using the same executor.
There is no separate six-mixture collector or byte-identity admission mode.
The local acceptance entry and production report producer remain unimplemented;
publication cannot treat unit fixtures or a caller-supplied PASS as admission.
