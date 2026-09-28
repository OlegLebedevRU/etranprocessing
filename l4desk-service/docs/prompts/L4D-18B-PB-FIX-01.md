# L4D-18B-PB-FIX-01 — совместимый ProcessingBackend rollout

```yaml
prompt_id: L4D-18B-PB-FIX-01
scope_project: ProcessingBackend
scope_root: D:\repo\platerra\Public\etranprocessing-l4tools-182\ProcessingBackend
prompt_type: corrective-production-rollout
registration_id: R-L4D-18B-PB-FIX-01-v1
blocked_prompt_id: L4D-18B-PB
required_handoff_ids:
  - H-L4D-18A-SHARED-v1
  - H-L4D-18B-PB-EVIDENCE-CONTRACT-01-v1
sequence_gate_handoff_id: H-L4D-18A-SHARED-v1
artifact_byte_binding_ids: []
external_artifact_reads:
  - handoff_id: H-L4D-18A-SHARED-v1
    artifact_commit: 537a1e493c83d1fa8e8cb765228be8d1b24a1d62
    paths:
      - shared/docs/l4desk/schema-v1.json
      - shared/docs/l4desk/schema-v1.md
      - shared/docs/l4desk/package-source-v011.json
  - handoff_id: H-L4D-18A-SHARED-v1
    artifact_commit: 2b38855000d67f06269ddf40e6305382abe2d3a9
    paths:
      - shared/docs/l4desk/handoffs/L4D-18A-SHARED-FIX-01-report.md
      - shared/docs/l4desk/handoffs/evidence/18a-release-checks.json
  - handoff_id: H-L4D-18B-PB-EVIDENCE-CONTRACT-01-v1
    artifact_commit: 61d79a9ae9790ab6eee991ad64539e893aab6ade
    paths:
      - l4desk-service/docs/prompts/contracts/acceptance-18b-pb-evidence-v1/pb-17b-report.md
      - l4desk-service/docs/prompts/contracts/acceptance-18b-pb-evidence-v1/pb-04b-report.md
      - l4desk-service/docs/prompts/contracts/acceptance-18b-pb-evidence-v1/pb-06a-report.md
      - l4desk-service/docs/prompts/contracts/acceptance-18b-pb-evidence-v1/pin-contract.md
      - l4desk-service/docs/prompts/contracts/acceptance-18b-pb-evidence-v1/pin-schemas.json
      - l4desk-service/docs/prompts/contracts/acceptance-18b-pb-evidence-v1/pin-examples.json
      - l4desk-service/docs/prompts/contracts/acceptance-18b-pb-evidence-v1/verification.md
output_handoff_id: H-L4D-18B-PB-v1
next_prompt_id: L4D-18C-IOT
branch: release/l4tools-1.8.2-beta-1
report_path: ProcessingBackend/docs/l4desk/handoffs/L4D-18B-PB-FIX-01-report.md
candidate_format: DETACHED_V1
candidate_path: ProcessingBackend/docs/l4desk/handoffs/L4D-18B-PB-FIX-01-candidate.md
architecture_sections: [3, 4, 5, 7, 14, 15, 16, 17, 18, 19]
```

## Contract gate and scope

Read `PROMPT-STANDARD.md`, this prompt, and `contract-handoff.md` first.
Verify the published registration and both direct accepted inputs under
§§2 and 8: unique blocks, version/producer commit, artifact digest,
compatibility, deployment status, absence of revocation, and the 18A
sequence gate. The exact §8 registration supplies consumer addressing
to this corrective prompt. It does not accept the rollout in advance.

The historical 17B, 04B and 06A handoffs remain unchanged provenance.
17B has no artifact digest; 04B has a mismatch beyond LF/CRLF; 06A has
historical CRLF digests. Their reports and the accepted PIN contract
are carried by the independent finite data-only export, which is the
second direct input. Do not gate recursively on those historical IDs,
reconstruct a missing digest, or read their linked source files.

The `external_artifact_reads` list is exhaustive. Check those exact
Git/raw bytes and accepted digests at the stated commits. Do not read
or execute source files in `shared` or `l4desk-service`, follow links,
or inspect adjacent projects. The 18A source package is an immutable
accepted Git artifact used through the declared dependency. Runtime
work and code inspection stay within `ProcessingBackend`.

## Release work

Carry out the original `L4D-18B-PB.md` goal in ProcessingBackend only:
final compatible rollout of its package/schema/certificate provider on
the approved production host. Compare the exact shared 0.1.1 ref,
current PB implementation/source, immutable image, Alembic head 027,
terminal API and PIN contract before deployment. Record that the
currently running PB image has a legacy pip Docker layout while the
current repository Dockerfile uses uv and `/workspace`; build and
verify the new image from the intended release source rather than
equating these layouts by assumption. If the source, schema, package,
or contract differs, stop with a specific blocker.

Before deploy, complete the PB project suite, Ruff check/format and
Pyright, inspect the dependency lock and immutable build provenance,
and verify that no unrelated files or secrets enter the image. Perform
MCP Ops readiness per `AGENTS.md`; if unavailable or degraded, use the
documented noninteractive SSH path on the approved host. Confirm
backup/readiness and an exact project-local rollback target before
changing the service. Preserve the current verified production image
`sha256:e2194a9b3341ec24d5b0d176d825f6879eff6d7c074bb107276473c28936e511`
as the baseline; inspect its live identity independently before use.

Deploy an additive, compatible release. Run Alembic upgrade to the
exact accepted head only if required; never downgrade populated schema
or perform a destructive contract phase. Verify the running image,
source/package/head, health, legacy terminal certificate flow, and
service PIN contract smoke without exposing secrets or PIN values.
On failure stop and apply the reviewed compatible rollback without
changing neighboring services. Any finding in terminal authorization
must be evaluated against the existing licensebilling XML behavior;
do not weaken terminal auth or change neighboring ownership by
inference.

The output `H-L4D-18B-PB-v1` must carry exact image/commit/package,
schema head, checks, smoke, rollback status, known limits, and
consumers `L4D-18C-IOT` and `L4D-18E-MB`. The historical package
artifact and data-only export do not prove a new production deploy.

## Immutable report and candidate

Use §9 `DETACHED_V1`. Publish final report and sanitized evidence at
the registered `report_path` in commit R, then an independent
candidate at `candidate_path` in commit C. Compute Git/raw digests;
include the report at R in candidate `artifact_paths`, without
including the candidate's own path/hash/C. Send R, C, exact paths,
digests and image ID to the independent controller. Do not edit the
common journal or open 18C before the controller publishes ACCEPTED.
