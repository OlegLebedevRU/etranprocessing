# Native quality57 - media stage 1

## Task intake
- Goal: implement and deploy common media v3 in l4media ingress only.
- Scope: ingress HTTP/schema/lifecycle/restore/tests and existing scoped deployer.
- Base: origin/main 6af7ec7c09cbf38d72a201640e0406e8d1dcd114; feat/media-native-quality57.
- Contract owner: integration coordinator. Package copied exactly, never forked.
- Producer -> transport -> consumer: native/camera RTP -> existing L4RTP/Janus -> browser;
  BFF -> authenticated management HTTP -> ingress route/mountpoint. No DB migration.
- Invariants: auth, lease/SN/session/stream identity, immutable descriptor, actual fmtp;
  no transcode, packet/SPS/timestamp rewrite, infrastructure recreation or DB changes.
- Validation: C Werror suite/build, schema negatives, isolated Redis roundtrip,
  shared contract suite, scoped deployer tests, secret scan, deployed health/capabilities.
- Acceptance: coordinator checks browser/native773 after downstream stack releases.
  Camera writers/runtime and MenuBuilder/IoT changes are outside this task.
- MCP Ops Readiness: UNAVAILABLE (tools absent); SSH preflight succeeded. Production:
  ~1980 MiB RAM available, disk33%, load0.26; builder ~2614 MiB, disk74%, load0.22.

## Contract and implementation
- Exact schema: `l4media/ingress/spec/media-v3/media.schema.json`.
- SHA256: `b86ca6f066fcdecb09cf7145a7fb23d87b77311a53c7ea04f0fb6e8a905b2818`.
- Existing `/api/v2/media/*` URLs require `X-L4-Media-Contract: l4-media/3.0.0`
  for v3; envelopes and descriptors carry contract_version=3. CTL/IPC stay version2.
- Explicit selection never coerces body/saved versions. Frozen schemas stay intact;
  old identity endpoints cannot mutate v3 sessions.
- All sources share concrete even geometry/declared cadence without preset caps.
  Janus probes determine advertised H264 profiles/HEVC support; START/replay checks
  exact PT/codec/fmtp. No new codec/player or Janus packet handling is introduced.
- Redis records retain format_version=3 and the complete versioned request/descriptor;
  absent readers, unknown and mixed record/envelope revisions fail closed.
- Scoped deployer preserves all Compose overlays, flags and neighbours. New options
  `--media-v3 --allow-interruptions` verify v3 and perform authorized immediate rollout.

## Validation and release
- PASS: Linux GCC `make test` and production binary, `-Wall -Wextra -Werror -pedantic`.
  All legacy/unit/feedback/deadline/frozen-v2/camera-v1 and new v3 groups passed.
- PASS: final v3 test against isolated localhost Redis, including unknown/mixed record
  versions, missing schema, exact descriptor roundtrip, renew/stop identities and native level150/153.
- PASS: coordinator schema suite, 6 tests; exact source/copy SHA256 verified.
- PASS: scoped deployer pytest, 9 tests; Ruff check/format and Pyright Linux, 0 errors.
- PASS: read-only secret scan of changed/new text files (0 matches), diff whitespace check.
- NOT RUN: real773/native/browser acceptance; downstream IoT/MenuBuilder/native owners must
  deploy v3 first. Local mocks and capabilities are not decoded-frame evidence.

## Deployed stage 1 (2026-10-24, smoke 19:27:59 UTC)
- Implementation PR113: https://github.com/OlegLebedevRU/etranprocessing/pull/113
- Packaging PR114: https://github.com/OlegLebedevRU/etranprocessing/pull/114
- Deployed source: `2e37e960c200aaa1352515229726a8d7eb2c4c3b` (accepted main).
- Registry digest: `sha256:3d7535bc8a11d9046b8a91cfdb9a899b0406bac237438b6255833e1511a86fe8`.
- Builder: standard launcher, `--component l4media-ingress --build-only`; 36 release
  tests and full Alpine GCC Werror suite/build passed before publication.
- First build at 7a725322 stopped before publication/deployment because the Docker
  context excluded openapi-v3.json. PR114 fixed that allowlist; the final build passed.
- Production: tracked scoped deployer from the deployed SHA, with
  `--media-v3 --allow-interruptions`; only ingress replaced. Existing HEVC/camera
  flags and all four Compose files retained. RestartCount=0, image and OCI revision matched.
- Independent runtime schema checksum exactly equals the coordinator SHA above.
- `/health`: `{"status":"ok","routes":1,"active_media_sessions":0}`.
- Authenticated capabilities with `X-L4-Media-Contract: l4-media/3.0.0`:
  `contract_version=3`, `media_contract_versions=[3]`, exact deployed server_revision,
  `dependencies_ready=true`, `hevc_runtime_verified=true`, H264 baseline/main/high
  (RFC6184/90000), H265 Main (RFC7798/90000), one video track, reverse RTCP, no audio.
- Receipt: `.agent-context/tasks/active/2026-10-24-native-quality57-media-receipt.json`.
- All 12 neighbour container IDs unchanged: app1, MenuBuilder, signer, ProcessingBackend,
  nginx media/default/mTLS, Janus, l4mcp, RabbitMQ, Redis and the existing mock gateway.
  Janus/nginx/DB/broker were not recreated. No terminal stream/test credentials were created.
- Cleanup: isolated test Redis stopped by EXIT trap. No production test sessions remain;
  standard release receipt/overrides persist. Temporary uploaded release archive/helper
  removed after deployment; local sanitized evidence retained in the isolated worktree.

## Handoff to stage 2
Media stage 1 is deployed and ready for IoT v3 integration. Use the exact header and
pinned common descriptor; existing URL paths are unchanged. Do not infer v3 support
from old envelopes or silently downgrade. Desktop/native/browser773 and camera
acceptance remain unverified here and belong to their respective owners/coordinator.
