# L4D-18A-SHARED-FIX-01 — release фиксация shared package

```yaml
prompt_id: L4D-18A-SHARED-FIX-01
scope_project: shared/etranprocessing_db
scope_root: D:\repo\platerra\Public\etranprocessing-l4tools-182\shared
prompt_type: corrective-release
registration_id: R-L4D-18A-SHARED-FIX-01-v1
blocked_prompt_id: L4D-18A-SHARED
required_handoff_ids:
  - H-L4D-17F-DOCS-FIX-01-v1
  - H-L4D-04A-SHARED-v1
sequence_gate_handoff_id: H-L4D-17F-DOCS-FIX-01-v1
artifact_byte_binding_ids: []
external_artifact_reads:
  - handoff_id: H-L4D-17F-DOCS-FIX-01-v1
    artifact_commit: a4b98e95e1493c76f0fdfc409a6441065f8e7020
    paths:
      - l4desk-service/docs/handoffs/L4D-17F-DOCS-FIX-01-v2-report.md
output_handoff_id: H-L4D-18A-SHARED-v1
next_prompt_id: L4D-18B-PB
branch: release/l4tools-1.8.2-beta-1
report_path: shared/docs/l4desk/handoffs/L4D-18A-SHARED-FIX-01-report.md
candidate_format: DETACHED_V1
candidate_path: shared/docs/l4desk/handoffs/L4D-18A-SHARED-FIX-01-candidate.md
architecture_sections: [3, 5, 7, 9, 10, 14, 16, 17, 18, 19]
```

## Contract gate

Read `PROMPT-STANDARD.md`, this prompt, and `contract-handoff.md` first.
Use the exact registration above under §8; verify its published commit and
the two direct accepted inputs. `H-L4D-17F-DOCS-FIX-01-v1` is both the
accepted E2E input and the sequence gate. `H-L4D-04A-SHARED-v1` is the
accepted schema/package input. Check uniqueness, accepted status, exact
versions and producer commits, artifact paths and SHA-256, compatibility,
deployment, producer completion, and absence of revocation. The §8 grant
supplies addressing to this corrective prompt; it changes neither input.

The 17F report is the sole cross-project data-only read grant. Read its
Git/raw bytes only at the stated commit and verify its accepted digest.
Do not follow its links or read other l4desk-service artifacts, source,
or commands. The controller separately verified all 16 accepted 17F
artifact digests. The three 04A artifacts belong to this shared scope;
verify their accepted Git/raw digests directly. If any mandatory contract
check fails, return `BLOCKED_CONTRACT` without changing shared files.

The original `L4D-18A-SHARED.md` remains historical. Its absent
`H-L4D-17F-DOCS-v1` is not substituted or fabricated. This registered
prompt is the sole authorized 18A execution path.

## Release work within shared scope

Preserve the original 18A goal: fix the production candidate package
version and exact provenance after accepted E2E, without new models,
business behavior, or migrations. Verify schema/declarative metadata
against 04A and the 17F release matrix. On a clean checkout, run the
required shared package build, lint, format, type and full tests; inspect
wheel/sdist contents, dependency lock, provenance, and tracked files for
secrets or unrelated content. Apply code quality checks required by
`AGENTS.md` to any Python code actually changed. The release report must
state exact commands, results, package version, SHA-256, delivery location,
consumer compatibility, and rollback target.

04A accepted `etranprocessing-db==0.1.1` as immutable Git-source delivery
under `deployment_status: PUBLISHED`, `deployed_environment:
artifact-registry`; its local wheel/sdist were verification artifacts.
First compare the current package's accepted 20 source/config/schema Git
blobs with `package-source-v011.json`, and confirm the shared metadata and
both consumers' locks. If the already published source is byte-identical
and all release checks pass, reuse that exact Git artifact and version.
Do not bump or overwrite an identical published version or invent an
external index publication. If any source bytes differ, establish the
compatibility and publication route before creating a new immutable
version; do not claim reuse. This is the original 18A reuse condition,
with the accepted 04A delivery made explicit.

Stay inside `shared/etranprocessing_db` for implementation. No shared
runtime/source or server change is authorized by the registration itself.
PB remains Alembic owner; do not create migrations here. The 18A output
must identify `L4D-18B-PB` and `L4D-18E-MB` as consumers. Do not edit
`contract-handoff.md` from the shared runtime scope.

## Immutable publication and handoff

Use §9 `DETACHED_V1`. Publish the final report and evidence at the exact
`report_path` in commit R; compute its Git/raw SHA-256. Publish the
candidate separately at the exact `candidate_path` in commit C. It must
contain the canonical handoff `H-L4D-18A-SHARED-v1`, contract kinds
`SCHEMA` and `DEPLOYMENT`, the report digest, exact package artifact
version/URL/SHA-256, tests, rollback, producer and report commits,
compatibility, deployment state, and both consumers. The candidate
must not include its own digest or commit. Send R, C, paths, and exact
Git/raw digests to the independent controller. Only the controller may
append `ACCEPTED` after separate release verification; do not launch 18B.
