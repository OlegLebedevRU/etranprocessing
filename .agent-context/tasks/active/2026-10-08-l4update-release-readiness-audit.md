# Read-only release readiness audit, 2026-10-08

## Scope
Public GenericRegistry GET/HEAD, local source/metadata and configured env key names only. No secret values, private key/PFX access, signing, publication, installed pointer or service changes. Native Con runtime remains the existing client only.

## Observed
- Current HEAD e3b62d66c8549bae78d9e2067c71a4fbcd51f59b, branch feat/l4update-release-pipeline-stage1; release scope had245 dirty/untracked entries and841 input hashes at inspection. Shared worktree must not be cleaned or directly published; a reviewed clean source checkpoint is required.
- All required native assets present. Frozen unsigned x86/x64 helpers match exact APPROVED size/hash; they were not rebuilt or signed.
- Current `tools/dist` metadata is stale1.13.2/source20033b2045b0e09b86d3c04ec93feac34a197edf and lacks detached root signature.
- Sibling signed1.13.6 root(source7c83bf41bbe9b19343aab4a92a9514043ed1445f) hash acda2efec72c0d6da668280f62550ad2919853d79ca2064cc3d8ea9faf26deff; detached signature present. Its kit has no bootstrap metadata. Local setup catalog has stable=null/transitions=[] and about6.3days remaining; it is fresh-install authorization, not remote transition admission. Existing generated/tracked binaries in that sibling are dirty after its build; preserve it as evidence.
- Public HEAD of all10 canonical artifact names for1.13.6/1.13.7/1.13.8 returned404. These versions had no observed partial canonical artifact set; absence is a point-in-time check, not a reservation.
- Exact native fixed catalog paths `l4tools/metadata/catalog.json` and `.sig` both GET404. No production transition graph is currently downloadable.
- External sw_sign.env exists and includes signing, registry, metadata key and terminal773 key names. L4TOOLS_BOOTSTRAP_DIR name absent. Values were not inspected. W_SIGN_PFX_PASSWORD original name is absent, SW_SIGN_PFX_PASSWORD alias name exists; narrow code alias support was added separately with conflict refusal and tests.

## Stable pipeline commands
From root: `uv run --locked python -m l4release plan --version <version>`; `prepare` for unsigned native gates; `release --env-file <external-env> --no-publish` for signed local candidate; ordinary `release` publishes immutable candidate. `verify` checks signed existing artifacts. `bootstrap-seal` writes first immutable signed helper seal outside Git without rebuilding frozen source; `install-kit` requires that seal through L4TOOLS_BOOTSTRAP_DIR. No signing/publication command was executed in this audit.

## Remaining blockers before real7031
1. Final native controller/worker/watch/rollback acceptance and compiled capability gates; model gates alone are insufficient.
2. Reviewed clean checkpoint containing final sources; new signed fresh baseline and separate signed target. Suggested free pair1.13.7→1.13.8; don't use old1.13.6 as implicit adopted source.
3. First owner-approved bootstrap seal and configured external L4TOOLS_BOOTSTRAP_DIR; then new kit includes exact frozen helper receipt/assets. No helper implementation modification needed.
4. Actual fresh SYSTEM nomination/installed-suite ancestry and strict communication barrier proof on baseline.
5. Public immutable roots/packages for both endpoints, measured exact-platform compatibility evidence, owner-signed current catalog with explicit single-hop edge. Current CLI has no production catalog publisher; library/test signing alone is insufficient.
6. Measure final expanded Setup gate duration against300s per-arch timeout in release config before pipeline run; do not silently omit new gates.

## Catalog publisher design for review (not implemented)
Fixed registry/native authority and compiled owner key only; strict reviewed release endpoints and measured evidence bound to root hashes/native platform/arch; compatibility switch-point matrix for old/new actors and mandatory backwards-compatible communication. Preparation is read-only and leaves reviewable canonical plan. Publication uses protected external revision/hash checkpoint and single build-script writer/workspace lock; initial404+no prior floor only bootstraps revision1. Existing-floor404/downgrade/equal-revision-different-bytes refuses. Numbered immutable signed archive precedes existing fixed catalog pair, full public byte/hash verification follows. No distributed CAS or atomic pair availability claim; concurrent drift refuses, transient mismatched pair fails closed at native reader. Stable promotion is separate; explicit target test can keep stable=null. Never fabricate evidence to admit a transition.
