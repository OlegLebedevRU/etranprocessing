# L4D-18E-MB-FIX-01 — restricted MenuBuilder activation

```yaml
prompt_id: L4D-18E-MB-FIX-01
scope_project: MenuBuilder
scope_root: D:\repo\platerra\Public\etranprocessing-l4d-18e-mb\MenuBuilder
prompt_type: corrective-production-rollout
registration_id: R-L4D-18E-MB-FIX-01-v1
blocked_prompt_id: L4D-18E-MB
required_handoff_ids: [H-L4D-18E-MB-EVIDENCE-CONTRACT-01-v1]
sequence_gate_handoff_id: H-L4D-18D-MEDIA-v1
artifact_byte_binding_ids: []
external_artifact_reads:
  - handoff_id: H-L4D-18E-MB-EVIDENCE-CONTRACT-01-v1
    artifact_commit: 7e1fc3c0eabcdb3fc333d0dbe88b938ae09acdab
    paths:
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-evidence-v1/17e-report.md
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-evidence-v1/17f-report.md
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-evidence-v1/17f-final-gate.json
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-evidence-v1/18a-schema.json
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-evidence-v1/18a-package-source.json
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-evidence-v1/18a-report.md
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-evidence-v1/18b-report.md
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-evidence-v1/18c-report.md
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-evidence-v1/18c-final-smoke.json
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-evidence-v1/18d-report.md
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-evidence-v1/18d-evidence.json
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-evidence-v1/18d-openapi.json
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-evidence-v1/18d-mb-auth-report.md
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-evidence-v1/verification.md
output_handoff_id: H-L4D-18E-MB-v1
next_prompt_id: L4D-18F-DOCS
branch: l4desk/l4d-18e-mb
mb_code_baseline_commit: 4184ee930e869ddfb044029512e51e7a69ed20f6
report_path: MenuBuilder/docs/l4desk/handoffs/L4D-18E-MB-FIX-01-report.md
candidate_format: DETACHED_V1
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-18E-MB-FIX-01-candidate.md
architecture_sections: [1, 2, 3, 4, 5, 6, 7, 9, 10, 13, 14, 15, 16, 17, 18, 19]
```

## Gate and baseline

Read the standard, this prompt and the journal first. Verify the unique §8
registration, accepted data-only direct input, all 14 Git/raw artifact digests,
versions, compatibility, no revocation, and accepted H-L4D-18D-MEDIA-v1 as
sequence-only gate. The original 18E prompt names historical 17E/17F IDs
that were never accepted. The accepted FIX handoffs and 18A/18B/18C/18D
provider handoffs are provenance for the finite export, not extra direct
inputs. Do not recursively gate them or read neighboring source, tests or
private files through report links. All cross-project data reads are the
14 exact copies above, from the published immutable export commit.

Use a clean separate worktree at the exact `scope_root` and branch above,
created from the published controller registration commit. Its MenuBuilder
backend/frontend code must match commit `4184ee930e869ddfb044029512e51e7a69ed20f6`
before new edits; subsequent central changes to MenuBuilder were docs only.
The accepted 17F evidence distinguishes current production source
`fb2273c0bb633426067b2a9487538d84155ef446` and image
`sha256:e0d17a09092e34b206eeb313b155186c419e55ee0419a684eb5ae2ce32e50090`
from the 4184ee9 test candidate. The accepted 18D MB auth step rotated
private media credentials without changing either MB application image.
Verify actual running image/config/flags before changing anything; report
any drift. Do not read or print secret values.

## MenuBuilder release and staged activation

Carry out the original 18E MenuBuilder backend/frontend rollout objective
within MenuBuilder only. Preserve the accepted Agent `1.8.2-beta-1`, PB
schema `027`, shared Git-source package `etranprocessing-db==0.1.1`, IoT
schema `0008_org_reservations`, and 18D media API/image. Do not change PB,
IoT, shared, media, Agent, Janus, broker or their server configuration.
Do not create or run migrations; ProcessingBackend owns Alembic. Do not
rebuild Janus. The accepted media build policy remains pinned: any future
ingress build defaults to authorized 176 with local Linux-container fallback,
and every new media image must reach production through the approved registry
by immutable digest. This step does not build media images.

Run the full MenuBuilder backend and frontend suites, consumer contract
fixtures, Ruff/format/Pyright where code changes require them, and frontend
type/build checks. Validate schema compatibility, ownership/PIN, media,
payment/entitlement and financial invariants. Commit/push only MenuBuilder
changes. Build the immutable MB backend image and frontend artifact from the
exact published Git source in isolated containers on the user-authorized
`176.108.247.249` build host. Test and scan the exact artifacts there for
private configuration. Publish each new image to the existing approved
registry, record its immutable manifest digest, then pull on production by
`@sha256:` digest. Do not use direct save/load to production or the beta
deployment framework. Deploy only MB components on approved production host
`87.242.100.34`. Follow the MenuBuilder runbook and MCP readiness; use
approved noninteractive SSH if MCP is unavailable. Preserve the exact
current MB image/config/SPA rollback checkpoint and select the new MB image
in a project-scoped deploy overlay, leaving other service entries untouched.
Registry credentials and private runtime config stay outside Git, logs and
report.

The user retired the beta deployment scheme for this release. The 176 build
host `etran-beta.timer` was already disabled and stopped by the authorized
runtime owner; verify it remains disabled/inactive and its service inactive.
Preserve the existing service/configuration files and do not start its worker.
Do not edit the production `.etran-ci` image-entry file or any unrelated
deployment config. Verify the manual MB image remains selected after rollout.

First verify the new backend/frontend with commercial controls disabled and
existing internal consumers/shadow metering. Activate in bounded phases:
restricted registration/onboarding, mock payment, entitlement, then
profile/Hub. At each phase record effective flags, affected tenant set,
provider response, reconciliation and rollback checkpoint. A flag that
cannot be restricted to the approved test scope must remain disabled until
a separately authorized general rollout. Restrict tests to existing tenant
1000/10000 and terminal 1000007 where applicable; do not create tenants,
send mass email, charge live YooKassa, or use production credentials as test
fixtures. Payment tests use the approved mock. Use bounded 10–30 minute
period-dependent tests and existing 120-minute historical evidence; do not
claim a fresh 120-minute or three-year wait without observing one.

Verify registration/link replay and collision handling, ownership/PIN,
console and moving video/start/stop, exact financial ledger double-entry,
payment idempotency/no duplicate transaction, entitlement after payment,
terminal-month charge, grace/block/stop, profile/Hub, archive status, old
user compatibility and current Agent. Distinguish HTTP transport, RTP and
browser-decoded frames. For money use integer kopecks; any mismatch must
immediately disable commercial flags and follow the saved rollback plan.
Leave archive worker/purge disabled. Existing 18D acceptance did not attest
an off-host backup schedule or activate retention.

Record actual image/source/schema/flags, phase timestamps, bounded test
subjects, commands, evidence and limits. A restricted test rollout is not
general commercial availability; state the exact effective activation in
the report and candidate. If a mandatory build, deploy, smoke or rollback
checkpoint fails, use the standard BLOCKED status and stop the cascade.

## Immutable output

Publish the final report and sanitized evidence at the registered report
path in commit R. Then publish a separate DETACHED_V1 candidate at the
registered candidate path in commit C. Include the Git/raw SHA-256 and
immutable commit for each output artifact; the candidate does not hash
itself. Use `accepted_at_utc: null` for controller completion. The producer
may report ACCEPTED only for checks actually completed. The independent
controller alone appends H-L4D-18E-MB-v1 to the journal. Address
L4D-18F-DOCS, but do not open 18F automatically.
