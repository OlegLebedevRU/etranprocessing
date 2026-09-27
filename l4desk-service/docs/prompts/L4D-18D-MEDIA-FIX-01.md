# L4D-18D-MEDIA-FIX-01 — compatible l4media release

```yaml
prompt_id: L4D-18D-MEDIA-FIX-01
scope_project: l4media
scope_root: D:\repo\platerra\Public\etranprocessing-l4d-18d-media\l4media
prompt_type: corrective-production-rollout
registration_id: R-L4D-18D-MEDIA-FIX-01-v1
blocked_prompt_id: L4D-18D-MEDIA
required_handoff_ids:
- H-L4D-17D-MEDIA-CONTRACT-01-v1
- H-L4D-18D-MEDIA-EVIDENCE-CONTRACT-01-v1
sequence_gate_handoff_id: H-L4D-18C-IOT-v1
artifact_byte_binding_ids: []
external_artifact_reads:
- handoff_id: H-L4D-17D-MEDIA-CONTRACT-01-v1
  artifact_commit: 77a666f17cc5cb154b6a46a8a119ed0594c087c0
  paths:
  - l4desk-service/docs/prompts/contracts/media-17d-v1/contract.md
  - l4desk-service/docs/prompts/contracts/media-17d-v1/verification.md
- handoff_id: H-L4D-18D-MEDIA-EVIDENCE-CONTRACT-01-v1
  artifact_commit: 583bf8f9982fdaac8e075cd8a94ad6577a03a180
  paths:
  - l4desk-service/docs/prompts/contracts/acceptance-18d-media-evidence-v1/08a-openapi.json
  - l4desk-service/docs/prompts/contracts/acceptance-18d-media-evidence-v1/08a-report.md
  - l4desk-service/docs/prompts/contracts/acceptance-18d-media-evidence-v1/15c-report.md
  - l4desk-service/docs/prompts/contracts/acceptance-18d-media-evidence-v1/archive-contract.md
  - l4desk-service/docs/prompts/contracts/acceptance-18d-media-evidence-v1/archive-manifest.schema.json
  - l4desk-service/docs/prompts/contracts/acceptance-18d-media-evidence-v1/archive-examples.json
  - l4desk-service/docs/prompts/contracts/acceptance-18d-media-evidence-v1/18c-report.md
  - l4desk-service/docs/prompts/contracts/acceptance-18d-media-evidence-v1/18c-production-archive.json
  - l4desk-service/docs/prompts/contracts/acceptance-18d-media-evidence-v1/18c-production-final-smoke.json
  - l4desk-service/docs/prompts/contracts/acceptance-18d-media-evidence-v1/verification.md
output_handoff_id: H-L4D-18D-MEDIA-v1
next_prompt_id: L4D-18E-MB
branch: l4desk/l4d-18d-media
media_baseline_commit: c200d60485d32c805ae00530c57ad7a4b6b2b1ce
media_tree_oid: 7146b099451fce222efb000800de5005807c4023
report_path: l4media/docs/l4desk/handoffs/L4D-18D-MEDIA-FIX-01-report.md
candidate_format: DETACHED_V1
candidate_path: l4media/docs/l4desk/handoffs/L4D-18D-MEDIA-FIX-01-candidate.md
architecture_sections:
- 3
- 4
- 5
- 8
- 12
- 14
- 15
- 16
- 17
- 18
- 19
```

## Contract gate and baseline

Read PROMPT-STANDARD.md, this exact prompt and contract-handoff.md first. Verify
this published §8 registration, the two unique ACCEPTED direct inputs, exact
versions/commits/compatibility/deployment state, all 12 Git/raw digests in
the finite read grant, no revocation, and accepted H-L4D-18C-IOT-v1 as
sequence-only gate under §8.4. Historical H17D/08A/15C are provenance, not
direct FIX inputs. H18C is not a direct subject input: its accepted artifacts
include neighboring implementation. The export supplies its report and
archive evidence as exact data-only bytes. Do not follow links, execute
exports, read neighboring source, or recursively gate provenance.

Use the clean worktree `D:\repo\platerra\Public\etranprocessing-l4d-18d-media`,
branch `l4desk/l4d-18d-media`, scoped to its `l4media` directory. It is based
on the published controller registration commit and contains the complete
accepted media source tree from `c200d60485d32c805ae00530c57ad7a4b6b2b1ce`
(tree `7146b099451fce222efb000800de5005807c4023`), including the
`55405e194a3bb71e07b17c6958fdf6ff3a37eab2` renew fix, 17D lifecycle
and 15C archive. Preserve the owner-approved Agent `1.8.2-beta-1` baseline.
Establish exact current source/image/config/route/flag matrix before mutation.

## Media rollout and verification

Carry out the original 18D production objective only within `l4media`:
accepted idempotent start/health/stop lifecycle, partial/repeated stop,
route and Janus mountpoint cleanup, stream continuity, deterministic media
archive and safe restore sample. Run the full project-local tests, config
validation and provider contract fixtures before release. Use current project
build rules; do not substitute historical report PASS for current verification.

Build both Janus and ingress as isolated containers on the owner-authorized
`176.108.247.249` build host, using its separate approved identity. Keep its
existing containers, volumes and configuration intact, and use no production
credentials in the build context. Build Janus from pinned upstream repository
source for version 1.1.4 at commit
`3c39ce8cf11c54cf6f1607030a47ac9db798389a`; a wrapper around a
prebuilt `canyan` runtime is not a source build. Record the exact upstream
repository/ref, source archive digest, build recipe and resulting binary
version/commit. Build ingress from the exact published l4media source commit.
Validate both images, config and project-local tests on the build host.

Publish only these two validated images to the existing approved registry,
after checking the destination/visibility and scanning image contents/layers
for private configuration, keys and tokens. Obtain registry credentials only
through the existing private mechanism; never print or commit them. Record
both pushed immutable manifest digests and source provenance. The user has
authorized this source build and registry publication. The build host is not
a production deployment target.

Publish immutable source/image evidence and exact rollback image/config before
the switch. On the production host pull and deploy both images by immutable
`@sha256:` registry digests, verify the running image IDs and Janus binary
version/commit, then perform the required smoke. A tag alone does not prove
the deployed bytes.

Keep management APIs internal and service-authenticated; verify unauthorized
requests are denied without reading or printing credentials. Do not expose
management endpoints, PINs, tokens or private configuration. Verify existing
streams/routes survive or cleanly recover; distinguish RTP transport state
from fresh browser-decoded frames. Do not modify IoT, PB, MB, Agent, broker
or shared schema.

Archive purge and destructive cleanup remain disabled. Verify media archive
volume mount, path containment, permissions, capacity, backup/restore
readiness and effective worker/purge flags before activation. Exercise only
safe deterministic dry-run and sample restore with synthetic or approved
non-destructive data, verify manifest/digest and no mutation of production
records. Do not start a production purge worker, initialize or erase a
production volume, or claim a three-year retention wait as a test. The
producer's safe restore sample is separate from any later operational
retention/backup rollout.

Use the repository-first l4media runbook and the approved production host
`87.242.100.34`; do not touch unrelated containers. Perform MCP Ops
readiness; if unavailable, use approved noninteractive SSH fallback. Record
RAM/disk/load readiness. Verify start→health→stop, idempotent and partial
stop, orphan cleanup, resource/latency metrics, archive dry-run/restore,
backup and rollback readiness, actually running image/config/flags. If a
mandatory test, deploy or smoke cannot pass, stop with the standard BLOCKED
status and report the exact gap.

## Immutable output

Report real checks, commands, source/image/config digests, flags, mount,
backup, smoke, limitations and rollback. Publish report and evidence at the
registered path in commit R, then separate DETACHED_V1 candidate in commit C
with Git/raw digest of R and each artifact's immutable source/report commit.
Candidate must contain the canonical fields, including `accepted_at_utc: null`
for the pending controller timestamp. Output H-L4D-18D-MEDIA-v1 must address
L4D-18E-MB. Only an independent controller may append ACCEPTED; do not open
18E automatically.
