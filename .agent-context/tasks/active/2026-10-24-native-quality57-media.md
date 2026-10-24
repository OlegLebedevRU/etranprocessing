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

Release follows PR publication and merge through the standard builder (build-only)
and scoped deployer. Exact merged source SHA, immutable digest, health, capabilities
and unchanged neighbour IDs are delivered in the final stage1 receipt to coordinator.
Original production overlays include `hevc-task40-media-flags.json` and
`camera-reader-candidate.json`; scoped rollout retains both. Only ingress is replaced.

