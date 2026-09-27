# L4D-18D-AUTH-MB-FIX-01 — private media credential rotation for MB

```yaml
prompt_id: L4D-18D-AUTH-MB-FIX-01
scope_project: MenuBuilder
scope_root: D:\repo\platerra\Public\etranprocessing-l4d-18d-mb-auth\MenuBuilder
prompt_type: corrective-private-config-rotation
registration_id: R-L4D-18D-AUTH-MB-FIX-01-v1
blocked_prompt_id: L4D-18D-MEDIA-FIX-01
required_handoff_ids:
- H-L4D-18D-MEDIA-AUTH-CONTRACT-01-v1
sequence_gate_handoff_id: H-L4D-18C-IOT-v1
artifact_byte_binding_ids: []
external_artifact_reads:
- handoff_id: H-L4D-18D-MEDIA-AUTH-CONTRACT-01-v1
  artifact_commit: 1c2ad9eb1bc252908a839247036849959d7ed3b6
  paths:
  - l4desk-service/docs/prompts/contracts/media-auth-18d-v1/contract.md
  - l4desk-service/docs/prompts/contracts/media-auth-18d-v1/media-management-openapi.json
  - l4desk-service/docs/prompts/contracts/media-auth-18d-v1/verification.md
output_handoff_id: H-L4D-18D-AUTH-MB-v1
next_prompt_id: L4D-18D-MEDIA-FIX-01
branch: l4desk/l4d-18d-mb-auth
report_path: MenuBuilder/docs/l4desk/handoffs/L4D-18D-AUTH-MB-FIX-01-report.md
candidate_format: DETACHED_V1
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-18D-AUTH-MB-FIX-01-candidate.md
architecture_sections:
- 5
- 8
- 12
- 14
- 15
- 16
- 17
- 18
```

## Contract gate and exact scope

Read PROMPT-STANDARD.md, this prompt and contract-handoff.md. Verify the one
accepted direct data-only auth input, all three exact Git/raw digests, the
published §8 registration, and H-L4D-18C-IOT-v1 as sequence-only gate. The
media auth export gives an API header and correction target, not proof that
media has been fixed or restarted. Do not follow its provenance links or
read l4media source. Work only in the clean registered MenuBuilder worktree;
do not modify media, IoT, PB, Agent, broker, Janus or unrelated server files.
This is a separate owner step to unblock 18D, not 18E rollout.

## Prepare the consumer rotation

Within MenuBuilder scope, find the actual backend environment key that sends
`X-Media-Service-Token`; do not assume it equals the provider key name. Inspect
configuration using key names, presence/equality booleans and masked audits
only, never print old or new values. Check whether the isolated
`l4desk-e2e-test-backend-1` uses the same old credential. It is an affected
MenuBuilder consumer only if that bounded check confirms it; list both
`menubuilder-backend` and this test backend in the rotation matrix with exact
conditional action. Do not copy production credentials to an unrelated test
host or change any other container.

The media owner first publishes and tests its fail-closed/no-default source
and stages the new provider image/config plus Janus admin secret through its
separate media step. This MB step must not switch a consumer before the
provider and private owner have agreed a bounded coordination window,
rollback images/config and the same new service credential via an approved
private channel. User authorization for the separate rotation exists. Follow
the repository's server-file modification protocol: prepare concrete private
paths, redacted before/after key-state diff, rollback and reconciliation plan;
never put a secret in Git, command output, logs, chat or handoff. Keep a
private backup with restricted permissions. The MB source template, if it
contains a public default, must use empty/non-sensitive configuration.

During the agreed window, the media owner changes only media provider/Janus
private configuration and performs the media switch. The MB owner changes
only its confirmed consumer private configuration and restarts only affected
MenuBuilder backend containers. Production may have a short authorized
control-plane interruption; stop only the agreed test streams first. Verify
matching provider/consumer credential with a boolean comparison inside the
approved private environment, missing/wrong credential denial, a safe
authorized internal operation probe, MB health and preserved unrelated
container IDs. Do not expose a management endpoint publicly or issue a live
PIN/stream operation merely to test a credential. If the provider image or
private channel is not ready, report BLOCKED_DEPLOY without changing MB.

## Output

Record actual MB key name, affected container matrix, sanitized old/new
presence and equality booleans, redacted private path/diff and reconciliation,
restart scope, negative/authorized probes, rollback, limitations and time UTC.
Never record token or Janus secret values or hashes. Publish immutable report
and nonsecret evidence at the registered path in R, then a separate DETACHED_V1
candidate in C with `accepted_at_utc: null`, Git/raw report digest and exact
artifact commits. Output H-L4D-18D-AUTH-MB-v1 addresses
L4D-18D-MEDIA-FIX-01 as coordination evidence. Only independent controller
acceptance fixes that handoff; it does not itself accept 18D or open 18E.
