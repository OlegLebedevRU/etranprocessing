# Immutable recovery helper bootstrap

## Task intake and scope

- Owner: L4 Tools release/fresh installer; native Windows tools + root l4release.
- Working tree, no commit/signing/publication/deployment. Parent separately owns
  worker_entry; forward engine remains NULL.
- Producer: explicit initial bootstrap seal + offline install-kit. Consumer:
  native fixed-name bundle admission, SYSTEM fresh installer, strict recovery
  receipt loader, existing hash/ACL scheduler adapter.
- Root release/catalog schemas unchanged. No MQTT/backend/IoT changes here.
- Frozen helper implementation and all source/bin bytes unchanged. Owner approved
  first Authenticode sealing of staged copies of these initial native bits;
  ordinary suite/updater flows never rebuild, replace or re-sign the helper.

## Implemented contracts

- `l4release/bootstrap.py`: fixed approved unsigned input size/SHA256 and native
  PE architecture; first seal copies assets into a new external directory, signs
  copies through existing Sign-Executables.ps1, verifies RFC3161 Authenticode and
  reference release publisher via Verify-Bootstrap.ps1. Clean source/lock/checkpoint
  and compiled owner metadata key are mandatory. Detached owner signature is the
  final seal checkpoint. Existing/partial output is never reused/re-sealed.
- CLI `bootstrap-seal --version <signed-reference-version> --env-file
  <external-sw_sign.env> --output <new-external-directory>`. No actual invocation
  with signing credentials was performed. Add non-secret
  `L4TOOLS_BOOTSTRAP_DIR=<external-directory>` to external sw_sign.env for kits.
- `install_kit.py` authenticates owner seal, both immutable asset identities and
  exact publisher; no re-sign. Creates fixed l4tools-bootstrap.json/.sig bound to
  current signed root digest/version. Destination helper size/hash is checked
  against the signed document, including a source-drift race refusal fixture.
- Native bootstrap_receipt.c authenticates exact bytes before strict JSON; kind
  frozen-supervisor-helper/schema1 is fixed supervisor-only ABI1. Both architecture
  entries/fixed names/limits/hashes/publisher/key/root binding are mandatory. A
  fixture-key parse cannot confer production owner trust.
- install_bundle.c opts into bootstrap assets only for fresh verify/install,
  stages fixed signed metadata and selected signed helper into original private
  inputs, validates held hash + explicit Authenticode admission policy. Historical
  installed-source/abort bundle admission does not require new files.
- recovery_receipt.c installs fixed PF/Leo4/Tools/recovery only under SYSTEM,
  native KnownFolders and original deployment lock. Owner signature + exact hash
  and publisher Authenticode are required. Existing complete receipt can be bound
  to a prior fresh root, but helper/publisher identity must match. No replacement,
  ACL repair, adoption of unsigned or incomplete files, SCM/task registration or
  helper execution. Partial directory refuses ERROR_NOT_READY; an owner-reviewed
  repair is required. Abort preserves the immutable bootstrap.
- Read-only fresh SYSTEM VERIFY checks the existing destination before reporting
  success (and before the operator transition can stop legacy communication).
  Missing directory is permitted only FILE_NOT_FOUND/PATH_NOT_FOUND under held
  protected native PF ancestry. Denied/reparse/unsafe/partial destinations or
  different helper/publisher refuse. No create, ACL repair, replacement or adoption
  occurs in this probe; an identical earlier immutable receipt is retained.
- `setup_recovery_receipt_load(j,arch,&owned)` is STRICT remote admission;
  helper/doc/sig/ancestor handles remain held. Size/hash, no hardlinks/reparse/ADS,
  readonly broad principals and compiled owner signature are checked. Getter
  returns borrowed L4RecoveryHelper while receipt remains alive. Initial fresh
  install alone uses the existing LOCAL_OFFLINE policy; known revocation and all
  other signature/chain/publisher/time failures remain fatal. Missing terminal
  enrollment certificate/channel remains permitted for clean local deployment.

## Verification evidence

- Python full l4release suite: final 80 passed (parent executed); changed
  bootstrap/install-kit targeted suite: 6 passed.
- Ruff check + format check root l4release: passed; pyright: 0 errors/warnings.
- Native real RSA3072 metadata signature tests and actual protected file/hash,
  CREATE_NEW no-overwrite, hardlink/ADS and partial-path guards: final gates below.
- Win32 Authenticode provider success and SYSTEM fresh install are NOT proven by
  these file-only fixtures. Native production code uses unchanged real admission
  policy; fixture refuses any accidental entry into external Authenticode stub.
- Final unified native build x86/x64/default: exit0, no compiler warnings/errors;
  default hash equals x86. Final affected fixture rerun both architectures:
  receipt113/0, install profiles634/0, fresh install1137/0.
- Full x86 run_tests: SUCCESS. Initial x64 passed prefix through root admission,
  then metadata stop semantic tests exceeded fixture1s budget (1152 checks,
  13 failures/error1460). Same unchanged binary isolated passed1225/0 in27793ms.
  Changed only named fixture semantic budget to10s/racejoin30s; explicit deadline,
  invalid99ms and cancellation tests + all production timeouts unchanged.
- Changed metadata rerun both architectures1225/0; remaining x64 recovery/task/
  worker/handoff/communication/setup run SUCCESS. Actual worker handoff202/0 both;
  worker entry361/0 both. A single full x64 pass after all changes is NOT claimed:
  evidence records passed original prefix + changed fixtures + passed remainder.
- [Final structured evidence](../../../tools/dist/.release/evidence/worker-bootstrap-20261008/result.json)
  and raw logs in the same ignored evidence directory. Initial failed x64 attempt
  and original-budget isolated pass are retained separately.
- Frozen input hashes remain exact approved identities:
  x86 8a3146938a11e68e0f50d5e86d09f5d635162bbc697be191434edc8d06733ee2;
  x64 d52dd106371533a49eb6512407e9ac3abdca07272b17f4640fee03bafc11dc7e.

## Remaining work and limits

- Current initial helper bins are unsigned; no real signed frozen baseline/seal
  exists yet. First seal requires clean reviewed source and signed reference
  release. No runtime readiness or rollback guarantee is claimed.
- External seal survives ordinary repo build/cleanup; release prepare/build has
  no l4rollback build step. Registry publication/retrieval of bootstrap assets is
  not implemented by this explicit initial seal command.
- Future worker uses owned trusted receipt + existing task adapter. Worker launch,
  watchdog arm, forward stop/apply and real signed SYSTEM installer/rollback cycle
  remain separate gates; production engines stay NULL.
- Fixtures created only temp files/private ephemeral keys in memory; native temp
  files are deleted. No real certificate/private key/secret was read or emitted.
  Own four runner scratch files removed after all sessions completed; no active
  runner or fixture process remains. Build objects/logs are ordinary ignored
  artifacts. Root owns component/flow/index
  context integration; this packet is helper-only.
