# L4D-18C-IOT-FIX-01 — compatible IoT release

```yaml
prompt_id: L4D-18C-IOT-FIX-01
scope_project: iot-rpc-rest-app
scope_root: D:\work\iot.leo4.ru\iot-rpc-rest-app-18c
prompt_type: corrective-production-rollout
registration_id: R-L4D-18C-IOT-FIX-01-v1
blocked_prompt_id: L4D-18C-IOT
required_handoff_ids:
- H-L4D-18B-PB-v1
- H-L4D-18C-IOT-EVIDENCE-CONTRACT-01-v1
sequence_gate_handoff_id: H-L4D-18B-PB-v1
artifact_byte_binding_ids: []
external_artifact_reads:
- handoff_id: H-L4D-18B-PB-v1
  artifact_commit: 3d76c7d6d5165959e4cd077b4730a5712d7e1f6a
  paths:
  - ProcessingBackend/docs/l4desk/handoffs/L4D-18B-PB-FIX-01-report.md
- handoff_id: H-L4D-18C-IOT-EVIDENCE-CONTRACT-01-v1
  artifact_commit: e7a03f8e089537fd91fcaad62659ffdd2c349515
  paths:
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/01c-report.md
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/01c-agent-compatibility.json
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/01c-golden-vectors.json
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/02-event-feed-contract.json
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/02-event-feed-openapi.json
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/02-remote-session-event.schema.json
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/02-remote-session.schema.json
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/02-event-feed-examples.json
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/06b-provision-openapi.json
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/06b-provision-request.schema.json
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/06b-provision-response.schema.json
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/06b-provision-examples.json
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/06b-report.md
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/07-remote-session.schema.json
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/07-remote-session-event.schema.json
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/07-event-feed-openapi.json
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/07-report.md
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/15b-report.md
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/17c-report.md
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/17c-video-watch-contract.md
  - l4desk-service/docs/prompts/contracts/acceptance-18c-iot-evidence-v1/verification.md
output_handoff_id: H-L4D-18C-IOT-v1
next_prompt_id: L4D-18D-MEDIA
branch: l4desk/l4d-18c-iot
report_path: docs/l4desk/handoffs/L4D-18C-IOT-FIX-01-report.md
candidate_format: DETACHED_V1
candidate_path: docs/l4desk/handoffs/L4D-18C-IOT-FIX-01-candidate.md
architecture_sections:
- 3
- 4
- 5
- 6
- 8
- 11
- 12
- 14
- 15
- 16
- 17
- 18
- 19
```

## Contract gate and release baseline

Read `PROMPT-STANDARD.md`, this exact prompt and `contract-handoff.md` first.
Verify this published §8 registration, both unique accepted direct inputs,
versions, producer commits, compatibility, deployment state, no revocation,
and the accepted 18B sequence gate. Check every exact Git/raw digest in the
finite external read grant. The grant permits only one 18B report and the
21 data-only IoT export files. Do not follow links, read tools source, read
neighboring implementation, execute exported documents, or recur through
historical provenance. Original 01C/02/06B/07/15B/17C handoffs have missing,
CRLF-only or displaced digests; they are not direct FIX inputs. The accepted
export is historical contract evidence, not proof of current runtime.

Use the clean IoT worktree `D:\work\iot.leo4.ru\iot-rpc-rest-app-18c`, branch
`l4desk/l4d-18c-iot`, based on `60f7762ec766e432bf372e255e94fdd33d3d91d2`.
Do not reset it to an older `master` or silently merge another line. Establish
an exact current source/deployed image/schema/feature-flag matrix before
changing implementation. Keep the accepted `l4tools 1.8.2-beta-1` Agent
baseline unchanged; prove compatibility through supported interfaces.

## Mandatory build-context security gate before build or deploy

The baseline `.dockerignore` is incomplete. Docker `COPY app-service` would
include local `.env`, and a tracked `.env.bak` contains historical credential
URLs. Never print, copy, upload, package or log their values. First harden
IoT-local `.dockerignore` and any relevant Dockerfile/build configuration so
private env, backup env, local secrets and unrelated development files cannot
enter the build context or resulting image. Remove the credential-bearing
backup from the tracked release and follow the repository secret-remediation
rule through an approved private owner path; do not publish its contents.
If exposure/rotation state cannot be established safely, report
`BLOCKED_SECURITY` before production deploy. Use only synthetic env in tests.

Prove the new image has no `.env`/`.env.bak` or secret payload using filenames,
counts and booleans, never raw env values. Verify the twelve `APP_CONFIG` runtime
settings are supplied by the private production environment, with sanitized
presence/equality checks, before switching image. Do not rely on Dockerfile
or template defaults to reproduce private runtime configuration.

## Build, rollout and smoke

Carry out the original 18C production objective only in `iot-rpc-rest-app`:
provider compatibility for current/old supported Agents, provisioning,
durable event feed/resume, single session lock, graceful console response,
video stop, archive dry-run/cursor guard and rollback. Run the full IoT local
suite, Ruff check/format, Pyright where configured, provider fixtures and
immutable image build. Verify schema/migration compatibility, image source,
secrets exclusion, effective flags and exact rollback image before deploy.
No MQTT topic/method/payload change and no destructive cleanup/archive purge.

The owner-approved `176.108.247.249` is a **test build host only**, reached
with its separate identity through the approved runbook. It already has four
containers and buildkit: do not change their containers, volumes, config or
images. Isolate build/tests with synthetic configuration and check available
resources before use. It is not a production deploy target. Do not copy the
private production environment or credentials there. Production remains the
approved `87.242.100.34` host via the standard repo-first deployment flow.
Perform MCP Ops readiness; if degraded/unavailable, follow the approved
noninteractive SSH fallback. If target/credentials/readiness are not fixed
locally, return `BLOCKED_DEPLOY` instead of guessing.

Apply only IoT-owned compatible migrations when required. Verify actual
running image/source/schema/flags, old/current Agent contract smoke,
provisioning, feed resume, lock, console/video stop, archive safe dry-run and
cursor guard. Inspect queue lag, duplicate/orphan sessions/events and
project-local rollback. Never expose secrets or mutate PB, MB, media or Agent.

## Immutable output

Report real checks, test/build/deploy commands, exact source/image/schema,
security gate, sanitized config evidence, metrics, rollback and limitations.
Use the registered `DETACHED_V1` paths: final report/evidence in commit R,
then separate candidate in C, with Git/raw digest of the report and no
candidate self-reference. Output `H-L4D-18C-IOT-v1` must address
`L4D-18D-MEDIA` and `L4D-18E-MB`. Only an independent controller may add
ACCEPTED to the journal; do not open 18D automatically.
