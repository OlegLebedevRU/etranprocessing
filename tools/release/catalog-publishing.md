# Owner-signed compatibility catalog publication

`catalog-plan` is read-only. It verifies the owner signature of each pipeline acceptance
report, downloads and authenticates both endpoint releases and both complete
native ZIP inventories, then emits a reviewable JSON plan. Public reads never
send Registry write credentials. Catalog versions, profiles and evidence hashes
are derived from authenticated inputs; there are no URL/key/version/hash overrides.

```powershell
uv run --locked python -m l4release catalog-plan --evidence <pipeline-acceptance.json> --env-file <external-sw_sign.env> > <reviewed-plan.json>
uv run --locked python -m l4release catalog-publish --plan <reviewed-plan.json> --env-file <external-sw_sign.env>
```

The second command repeats admission and compares the entire reviewed plan against
current evidence, public artifacts, catalog ancestry and a clean source checkpoint.
Source cleanliness shares the input-snapshot classification: configured generated
inputs and bin/obj/dist/__pycache__ segments are excluded, while required assets
remain sources even when binary. Renames check both paths. Initial release
clean_at_start still checks all Git changes before the build begins.
Only then does it load the existing encrypted metadata signing key. It uses the
existing `AR_GENERIC_KEY_ID` / `AR_GENERIC_KEY_SECRET` multipart PUT directory API.
The metadata public key must match the compiled owner key; no new secret is needed.

The fixed public pair is `l4tools/metadata/catalog.json` and `.json.sig`.
New subdirectories within the existing `l4tools` namespace are:

- `metadata/evidence/<actual-report-SHA256>.json` and `.json.sig`;
- `metadata/archive/<revision>/catalog.json` and `.json.sig`.

Evidence and revision archives must be immutable. Existing different bytes are
refused. Every upload is verified through public GET. Initial publication keeps
`stable=null`; admission of an explicit version does not promote `latest`.

The external key directory also contains `catalog-publisher/floor.json`, a private
monotonic revision/hash floor and one exact pending publication. Windows owner and
DACL checks reject foreign writes, replacement and reparse paths; these checks do
not grant permissions. The build writer must retain this directory between runs.
There is a workspace lock and a publisher lock, but no distributed Registry CAS or
atomic pair guarantee. JSON/signature mismatch temporarily fails closed at native
readers. After a partial PUT, rerun the *same unchanged reviewed plan*: only the
known old/candidate bytes may be repaired. Unknown drift requires investigation.
Expired signed catalog bytes may establish publisher ancestry only; terminal
admission and routing retain the normal expiry gate. HTTP409 refuses publication.
The actual Registry mutable-pair acceptance still needs a controlled real test.

## Required pipeline acceptance

Compatibility admission belongs to the release pipeline. The stand runs one normal
transition and one forced rollback through the same shared executor. The forced attempt may run first,
restore the source, then the normal attempt completes the target. There is no
forward-before-restore requirement. Intermediate states are observed inside these
runs; there is no separate six-mixture collector
or byte-identity acceptance path. New communication must remain backwards
compatible, checked by one shared pipeline gate before compatibility admission.

The report and detached `<report>.sig` use the existing compiled-owner RSA3072 key.
Schema1 has exactly these fields: `schema`, `kind=l4tools-pipeline-compatibility`,
`key_id`, canonical UUID `run_id`, integer `terminal_id=773`, `tenant_id=1`, `arch`,
`platform`, `from`, `to`, `producer`, Unix-second `started_at` / `finished_at`,
`backward_compatibility`, `forward`, and `forced_rollback`.

`platform` contains observed native major/minor/build/native_arch/product_type.
Each endpoint contains exact version, signed root SHA256 and signed layout
descriptor SHA256 (`inventory_sha256`). `producer` is the fixed
`{"name":"l4release","schema":1}` pipeline identity. It does not assume the target
installer is the active updater: that identity is independent and must be verified
through the actual executor's existing authenticated updater admission.

`backward_compatibility` contains exactly
`{"contract":"communication-backwards-v1","report_sha256":"<actual check report SHA256>"}`.
This is one pipeline check covering new communication's backwards compatibility;
its actual report is bound to the same authenticated endpoints/profile/architecture.
The pipeline derives the hash from the real check output, never a caller PASS or
free CLI hash argument.

Both `forward` and `forced_rollback` contain exactly: `operation_id`,
`plan_sha256`, `proof_kind`, `proof_sha256`, `outcome_sha256`, `result`, `error`,
`installed`, `finished_utc`. The normal transition requires typed102 proof plus
bound103 SUCCESS, error0 and the authenticated target installed identity. Forced
rollback requires typed108 proof plus bound103 RESTORED, the original nonzero error
and the authenticated source installed identity. Operation IDs and protected64 plan
hashes must differ between the two executor attempts. `installed` uses the same
version/root/descriptor identity shape as endpoints; finished_utc is FILETIME within
the acceptance run. No completion claim is derived from exit code, guard DONE,
mutable summaries, caller booleans or an unbound journal-record hash.

The production adapters are implemented in source: native local admission and
protected export plus the `acceptance-plan` / `acceptance-run` pipeline. Actual
stand acceptance remains mandatory before publication. The native producer verifies
102/108, bound103, original64/catalog/purpose, exact installed identity/configs/epochs
and actual final clear. Python reads only the fixed SYSTEM-owned export under held,
read-only, non-reparse handles and seals the combined result with the compiled-owner
metadata key. Child exit code alone never becomes evidence. Synthetic fixtures
model native authority and are not stand acceptance.

The shared backwards check combines real fixed Setup x86/x64 pair-contract gates
with the genuine forward102 export. Passing the executor's required mixed-service
barrier before Con/Sup switch is an inference from that authenticated, fixed executor
contract; the report does not invent observed intermediate nonces.

## Local acceptance and private revision reservation

```powershell
uv run --locked python -m l4release acceptance-plan --source <installed-source> --target <published-target> --arch x86 --env-file <external-sw_sign.env> > <reviewed-acceptance.json>
uv run --locked python -m l4release acceptance-run --plan <reviewed-acceptance.json> --env-file <external-sw_sign.env>
```

The first command is read-only and verifies both full public releases, the native
Windows profile and the protected publisher floor. The reviewed second command
requests one UAC elevation if necessary, then uses the exact installed source
l4setup with owner-signed purposes. It runs forced rollback first, then the normal
transition through the same native executor. Each purpose is created immediately
before its attempt, with at most four hours of validity; previously staged signed
bytes remain unchanged on resume. Native admission requires at least151 minutes
remaining before stage and original92. The Python wait is156 minutes per attempt
and never kills the native controller on timeout.

`catalog-publisher/trial-reservation.json` reserves the private catalog revision R
and exact retained signed plan/catalog under `trial-R`. This is separate from
`floor.json`: an initial public404 remains valid and does not claim publication.
Native admission consumes the real catalog floor R. The following public plan
therefore derives revision greater than R. No revision/reset override exists.
Protected genuine exports are read again on exact resume; an existing started log
without a completed export prevents automatic re-launch of that original UUID.

## Reviewed stable promotion

After the operator has verified actual RPC7032 SUCCESS, persisted result event76
and the live target services/identity/channel, use the same accepted report:

```powershell
uv run --locked python -m l4release promotion-plan --evidence <pipeline-acceptance.json> --env-file <external-sw_sign.env> > <reviewed-promotion.json>
uv run --locked python -m l4release catalog-publish --plan <reviewed-promotion.json> --env-file <external-sw_sign.env>
```

The stable target is derived from the signed acceptance's exact already-public
admitted edge and authenticated images. The plan cannot add an edge, select a free
version/hash, replace its evidence or promote a revoked target. It uses the same
archive, public pair verification and monotonic protected publisher mechanism.
The explicit reviewed invocation is operator authority after those runtime checks;
the publisher does not independently certify the RPC observation.

The schema's terminal_id and tenant_id are the owner's declared approved stand
scope (773 / 1), rather than observed tenant metadata. The producer observes actual
SN, certificate thumbprint and authenticated channel in protected native output.
It must not infer tenant from certificate O or add a DB/IoT identity contract.
Owner sealing binds approved stand scope to actual held native executor output.
