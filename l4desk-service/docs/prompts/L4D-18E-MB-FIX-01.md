# L4D-18E-MB-FIX-01 — restricted MenuBuilder activation

```yaml
prompt_id: L4D-18E-MB-FIX-01
scope_project: MenuBuilder
scope_root: D:\repo\platerra\Public\etranprocessing-l4d-18e-mb\MenuBuilder
prompt_type: corrective-production-rollout
registration_id: R-L4D-18E-MB-FIX-01-v2
blocked_prompt_id: L4D-18E-MB
required_handoff_ids: [H-L4D-18E-MB-EVIDENCE-CONTRACT-01-v1, H-L4D-18E-MB-DEVICE-PIN-CONTRACT-01-v1]
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
  - handoff_id: H-L4D-18E-MB-DEVICE-PIN-CONTRACT-01-v1
    artifact_commit: 57b7b015778ef46ea1d9f4b4b993e80c54042034
    paths:
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-device-pin-v1/iot-device-contract.md
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-device-pin-v1/iot-device-schemas.json
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-device-pin-v1/iot-device-examples.json
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-device-pin-v1/pin-contract.md
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-device-pin-v1/pin-schemas.json
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-device-pin-v1/pin-examples.json
      - l4desk-service/docs/prompts/contracts/acceptance-18e-mb-device-pin-v1/verification.md
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
registration, both accepted data-only direct inputs, all 21 Git/raw artifact digests,
versions, compatibility, no revocation, and accepted H-L4D-18D-MEDIA-v1 as
sequence-only gate. The original 18E prompt names historical 17E/17F IDs
that were never accepted. The accepted FIX handoffs and 18A/18B/18C/18D
provider handoffs are provenance for the finite export, not extra direct
inputs. Do not recursively gate them or read neighboring source, tests or
private files through report links. All cross-project data reads are the
21 exact documents above, from the two published immutable export commits.

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

The MB backend build may resolve and import the already accepted unchanged
`etranprocessing-db==0.1.1` package through its own pinned lock/Dockerfile
as a build dependency. The approved delivery is exact Git source per the
exported 18A manifest; no Python index wheel is assumed. This packaging
step is not a grant to inspect, edit, run commands/tests in, or publish the
neighboring `shared` project. Verify the package version and lock and keep
the accepted source bytes; a shared code/schema change is `BLOCKED_SCOPE`.

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

Include the owner-reported narrow-browser responsive defect in the MB
frontend scope: reproduce the affected monitoring, video and licenses views
at narrow widths, keep table text readable with an appropriate minimum
column width and horizontal scrolling, adapt forms where needed, and add
focused browser/UI regression checks. Do not change provider protocols or
neighboring components for this layout work. Record the exact reproduced
viewports and user-visible result.

Use `device_id` as the primary terminal identity in L4Desk selection,
ownership and navigation. Serial numbers are incomplete for some devices;
resolve/use `SN` only where an accepted provider protocol requires it, and
handle absent `SN` without hiding a valid tenant-owned device or binding it
to another tenant. Align L4Desk online/offline presentation with the same
authoritative status source and freshness rules already used by MenuBuilder
terminal management; do not hard-code `offline` or infer live presence from
a missing serial. Add negative tests for partial/missing SN and cross-tenant
device IDs, and a UI parity check against terminal management status.

The current public onboarding status advertises Agent `1.7.7` and its old
installer URL. The owner accepted Agent `1.8.2-beta-1` unchanged, but no
approved download URL for that version is in the finite input packet.
Do not present the 1.7.7 installer as the current Agent or invent a beta URL.
Show the accepted version and omit/disable the download action until an
approved published artifact URL is available. Record this as a release-link
limitation; do not rebuild or alter Agent binaries in MenuBuilder scope.

The accepted 18B provider has a privately configured service-auth token for
its internal certificate PIN API. Renewal in MenuBuilder may require the
same existing value in its server-only `PROCESSING_BACKEND_SERVICE_TOKEN`.
The user authorized this bounded private configuration action. Transfer the
opaque value on the production host through the approved private operations
path into the MenuBuilder backend private env file; production and isolated
MB backend consumers use that same env file. The agent must not read, print,
hash, log, place in a command argument, commit, or put the value in an image
or report. Do not inspect the raw PB env file, rotate/change PB credentials,
edit PB files or recreate PB. Save a restricted MB env rollback copy before
the transfer; replace only the MB key, preserve other settings and permissions,
and recreate only affected MB backend consumers. Verify configured presence
without revealing the value, missing/wrong credentials return 401, and an
authorized lookup of a nonexistent operation returns the provider's 404.
Then test renewal only for an approved tenant-owned terminal. If a safe
opaque transfer is unavailable, stop this part and request an operator
private-config step rather than exposing the credential. Restore the MB
private backup and affected containers on rollback; PB stays unchanged.

Allow an authenticated tenant user with the existing terminal-management
authorization to request a new certificate-renewal PIN for their own terminal
through the existing accepted PIN contract. Do
not add a new commercial entitlement, quota or arbitrary UI eligibility
restriction to this action. Preserve authentication, tenant ownership,
provider-side PIN lifecycle and abuse controls already required by the
accepted contract. Display the current unactivated PIN and its pending state
to that authorized user instead of concealing it; do not expose it in public
responses, logs, telemetry, URLs or another tenant's view. Test issuance,
pending display after refresh, activation transition and cross-tenant denial.
If the existing accepted provider contract lacks an endpoint or exact field
needed for this behavior, stop that part with `BLOCKED_CONTRACT` and request
a finite controller grant; do not infer provider semantics from source links.

At onboarding, let the authorized user choose the device type represented
by the existing `sys` tag: exactly `windows`, `linux` or `esp32`. Preserve
the chosen value through the existing MenuBuilder/device-tag contract and
show it on return to the form. Validate the enum and tenant ownership in
backend and UI, including rejection of unknown values. Do not change IoT
implementation or invent a new tag endpoint; if the accepted contract lacks
the required write/read semantics, stop this part at the contract gate.

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

The read-only preflight found production YooKassa effective disabled with
shop/key/webhook absent; the isolated test backend uses a nonofficial mock
endpoint. Do not infer live payment readiness from the mock. In current MB
configuration, enabling `l4desk_enabled` may implicitly enable YooKassa
even without its private credentials. Before any phase that enables this
umbrella flag, demonstrate an explicit, effective mock-only boundary and
negative test for live provider calls. If that cannot be proven, leave
payment/commercial flags disabled and report the exact blocker. Never set
live provider credentials or make a real charge in this step.

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
