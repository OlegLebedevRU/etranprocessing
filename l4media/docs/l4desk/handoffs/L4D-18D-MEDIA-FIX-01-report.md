# L4D-18D-MEDIA-FIX-01 — production media release

Producer verdict: ACCEPTED. Independent controller handoff acceptance: PENDING.
Verification completed 2026-09-27T22:07:58Z.
Candidate: `l4media/docs/l4desk/handoffs/L4D-18D-MEDIA-FIX-01-candidate.md`.
Evidence: `l4media/docs/l4desk/handoffs/L4D-18D-MEDIA-FIX-01-evidence.json`.

## Gate and scope

R-L4D-18D-MEDIA-FIX-01-v1 registered at 59e93938b486f08350a8ede8f1f54849752dabc5.
Final bounded prompt revision ca6dd84aacc0d095c1a42add422470ba9f3efae3 includes
explicit user-directed Janus source build, registry delivery and later frozen
Janus/local ingress fallback policy. Final packet manifest SHA-256:
17dfae1cf1266c8386f117ee4ca8d44afba445365b63ceae3968829e697bff2f.
All 15 files matched immutable Git/raw bytes; all 12 granted artifacts verified.

Direct inputs (accepted, version 1.0.0):
- H-L4D-17D-MEDIA-CONTRACT-01-v1, producer 77a666f17cc5cb154b6a46a8a119ed0594c087c0.
- H-L4D-18D-MEDIA-EVIDENCE-CONTRACT-01-v1, producer 583bf8f9982fdaac8e075cd8a94ad6577a03a180.

H-L4D-18C-IOT-v1 is sequence-only; no recursive neighboring source reads.
Source baseline c200d60485d32c805ae00530c57ad7a4b6b2b1ce, tree
7146b099451fce222efb000800de5005807c4023 retained accepted renew/stop/archive
behavior. Agent 1.8.2-beta-1 unchanged; no IoT/PB/broker/schema change.
MB credential consumer changes were executed under their separately registered
owner step. H-L4D-18D-AUTH-MB-v1 independently accepted in central commit
74abf1745989c24f19b0ba16b0d75522700e4f7e.

## Implementation and publication

Runtime implementation source: e6e681dcf74fb0e81da5cf1f7f0fc2d39dca7f33.
Git archive SHA-256: 71e09b7ce420a5ba1ea00c0451480803e1df4f041bd48c5c2ac8e1d2c5f4975e.
Branch: l4desk/l4d-18d-media; source and operational policy pushed to origin.

Changes: fail-closed ingress startup/auth without public token defaults;
private Janus admin credential rendered to a mode-600 runtime config;
Janus multistage compilation from upstream repository; source/archive labels;
registry image selection in Compose; isolated build/smoke/layer-scan scripts;
LF-preserving Git archive attributes; empty env examples; runbook/changelog.
C auth regression test changed to reject an empty configured token.

Janus upstream 1.1.4, same version as previous production:
3c39ce8cf11c54cf6f1607030a47ac9db798389a.
Upstream archive SHA-256:
a3e211e601980f0f618e1189705a2d23d36abc2b81c178a4c5cf20072edb19e5.
This is a source build, not a canyan image wrapper. Base Debian digest is pinned;
Streaming/HTTP/WebSocket/data-channel support compiled. Upstream source archive
and GPL license included. Apt packages resolve at build time; no claim of
bit-identical future rebuilds. Source/binary labels and exact tested digests
identify this release.

Operational policy follow-up: 815a7c33fae936c5537b7126a70dd1c11af52cad.
Only build scripts/docs/baseline metadata changed after runtime image publication.
Default build now changes ingress only, reusing frozen Janus digest. Janus
rebuild requires a separate explicit user command and `--build-janus`.
Preferred build host is approved 176; if unavailable, local Linux Docker build
uses the same Git archive/Dockerfile/tests. Every new image must still be pushed
to approved registry and pulled on production by digest; no save/load delivery.
Local fallback was documented/syntax checked, not exercised because 176 worked.
First published pair is preserved in `deploy/baseline-images.json`.

## Actual build and registry delivery

Both runtime images container-built on approved 176 host, then tested and
pushed to the existing approved private registry. Anonymous manifest retrieval
returned 401. Production performed Docker pull of the exact registry digests:

| Repository | Published/pulled digest |
|---|---|
| l4media-ingress | sha256:3e0e0341f37702bedef06ca87538c494c7e6bdf8f87152e6d5901e4d792322fd |
| l4media-janus | sha256:93265665a92482ec1de2c9571a08d27c87b1dee06efc000f717ed42dfe2438ae |

Production binary SHA-256 matched the tested build-host images:
- ingress: 1669166cf580ba688868e2f064f99aa1c22f76c6858331db4cf342ecf7308de8
- Janus: 85d7979e0a7aa15c1ad402983b5669ada5f0067f5528d170c287c5ee7d3ffee7

Release source staged at `/home/user1/.l4d-releases/e6e681dcf74fb0e81da5cf1f7f0fc2d39dca7f33`.
Changed public source/config files synchronized into `/home/user1/l4media`;
operational policy follow-up delivered separately from its own Git archive.
No production compilation, no direct image transfer. Registry references are
provided through private `L4MEDIA_INGRESS_IMAGE` / `L4MEDIA_JANUS_IMAGE`.

## Verification

- `uv run pytest -q`: 21 archive tests passed.
- `uv run ruff check --fix archive tests ingress/tests/test_media_lifecycle.py`;
  `ruff format`; `pyright archive ingress/tests/test_media_lifecycle.py`: passed.
  Additional layer scanner Ruff/format/Pyright passed, zero diagnostics.
- Docker ingress build runs C unit suite: passed.
- Source-built Janus + ingress in isolated internal Docker network:
  all 9 lifecycle and 6 regression/contract tests passed. Includes auth,
  idempotent start/stop, concurrency, lock conflicts, reconciliation, orphan
  cleanup, short TTL, stale RTP/RTCP distinction and malformed framing.
- Both images without required credentials exit 1. Layer scan found no private
  PEM keys, private env files or SSH files. Source scan findings were only
  synthetic token generation and the Janus placeholder; no private credentials.
- `bash -n` passed for final build/publish and smoke scripts.
- Compose validation succeeded without printing expanded configuration.
- Actual production bounded synthetic session: start 201, repeat 200, health
  200; explicitly removed only its synthetic mountpoint, then partial stop 200,
  repeat stop 200; route 404 and mountpoint error 455 afterwards. Seven ms
  total for this bounded API scenario; not a general performance benchmark.
- Real browser test on 1000007 confirmed moving video without delay and normal
  stop. First smoke counter snapshot was inconclusive, so it was repeated with
  a coordinated live observation; no claim is based on that first snapshot.
  During repeat: fresh RTP true, 5,331 packets observed, unrouted packets 0.
  On stop: state stopped, reason stream_stopped, elapsed 81 sec versus TTL 600.
  Final: 10,497 RTP packets / 10,926,315 bytes, no connected transport, route
  404, Janus mountpoint absent (455), active sessions 0, failed sessions 0.
  Browser visual result is human-confirmed; server counters corroborate actual
  RTP transport and immediate lifecycle cleanup, not a screenshot-based proof.

## Authentication, deployment isolation and resources

Public-default credentials were replaced through approved private rotation.
New distinct random media/Janus credentials generated only on production.
Files/private backups restricted; no values or secret hashes exported.
Both consumers matched provider. Missing/wrong/old service credentials 401,
new 200. Missing/old Janus admin credentials error 403; new accepted.
From each backend's actual configured settings, authorized metrics returned 200.

Only Janus/ingress and the two affected MB backend containers recreated.
MB application images unchanged. All other container IDs preserved; nginx,
TLS files, Agent, IoT, PB, broker, Redis and mock gateway unchanged.
Management ports remain internal, not published.

MCP Ops unavailable; noninteractive SSH fallback used. Before switch: available
RAM 1,991 MiB, root disk 51% used with 15 GiB free; initial load 0.33/0.22/0.20.
After bounded tests, idle snapshot: ingress 488 KiB/128 MiB, CPU 0%; Janus
5.906 MiB/512 MiB, CPU 0.12%. This is not a load-capacity benchmark.

## Archive dry-run and restore readiness

Existing paths `/mnt/l4desk-archive` and `/var/lib/l4media/telemetry` inspected;
no existing files in them. They are directories on root filesystem, not a
separate mounted disk. Over 15 GB available. Tested host-path bind access using
an isolated temporary child, non-root UID, exact release source mounted RO,
Python 3.14 and jsonschema 4.26.0. No production database initialized.

Synthetic closed-month data: 8 samples and 1 operational summary in memory.
`run_archive(..., dry_run=True, purge=False)` verified JSON schema from the
explicitly granted contract, generated/verified digests, state verified.
All 8 hot records and summary retained. Sample archive backed up to tar,
extracted to separate child, digests verified again, all 8 records restored.
Sample backup hash is in evidence. Production path file metadata unchanged;
temporary child removed. No real retention wait claimed.

`L4MEDIA_ARCHIVE_WORKER_ENABLED=false`; no archive-worker container exists.
Purge never enabled. This proves sample backup/restore and path readiness,
not an operational off-host backup schedule or three-year retention promise.
Those remain separate operational policy work, per scope and user direction.

## Rollback and cleanup

Private backup `/home/user1/.l4d-backups/18d-media-auth-20260927T214543Z` mode 700
contains old private envs (600), old Compose/Janus config and changed-source
backups. Four prior images retained under `l4d18d-rollback-<container>:pre`.
Old media images:
- ingress sha256:f201ff382eaa9ca127681cee16314e86deffa9d2441f6c293f3e10ff57e61c9f
- Janus sha256:80970d0b1c407222e2f88d7a6d31a16686fb9a5145a491b4b4d843ee40ee4ce1

Rollback requires restoring matching old provider/consumer credentials together,
old images and original Janus config, then recreating only those four containers.
Prepared and verified available, not executed. Prefer forward correction because
old credentials were publicly known; any emergency rollback is temporary.

Synthetic streams/routes/mountpoints, temporary token channel, test containers,
internal test network and archive sample removed. Immutable release/evidence and
rollback artifacts retained intentionally. No claim that old source Git history
was rewritten; effective exposed credentials have been rotated/revoked.

No known mandatory runtime blocker remains. Independent controller must accept
this report/candidate; do not automatically open 18E.
