# MenuBuilder / media Native Auto 1.1 handoff

## Scope and source

2026-10-24, local implementation and tests only. Branch
`feat/menubuilder-camera-native-auto11` is based on current main
`6af7ec7c09cbf38d72a201640e0406e8d1dcd114` plus owned PR110
`5e629e83ed20b86f5cdf4ea52362099767b86a64`. PR110 remains the UI/admission
prerequisite. No firmware, IoT implementation, production, broker or DB changes.

## Delivered

- Standalone package: `contracts/siplite-camera-v1.1/`, typed observation and
  capabilities, descriptor/cadence markers, canonical descriptor/full-binding
  bytes and SHA256 vectors, verifier and frozen-baseline manifest.
- Exact schema SHA256:
  `bfd71ec96cf8de81657fd319c603f91a4769209887e32297fff4193752e3c62e`.
  BFF and media runtime copies are byte-identical. Frozen1.0/HEVC2.0 untouched.
- C reader selects1.1 only with the complete four-header bundle, preserves the
  complete immutable descriptor in session storage/restore, rejects mixed
  updates, and permits exact-version cleanup with the new gate disabled.
  RTP/Janus packet handling unchanged.
- BFF prepares1.1 only with fresh device observation and explicit device/IoT/media
  capabilities. Uses device geometry verbatim; no config writes or geometry
  inference. Opaque admission ticket retains full descriptor/cadence. Lease and
  START remain after readiness; no downgrade or extra mutation retry.
- Existing UI from PR110 consumes the same opaque tickets and error flow;
  frontend source unchanged in this continuation.

## Producer / consumer contract

All selected BFF/media/IoT operations require:

```
X-L4-Camera-Contract: siplite-camera/1.1.0
X-L4-Camera-Cadence: camera-cadence/0.1.0-proposed
X-L4-Camera-Enrollment: camera-enrollment/0.2.0-proposed
X-L4-Camera-Admission: camera-ctl-monotonic/0.2.0-proposed
```

Exact HTTP integration proposal and definitions are in the package README.
Selected IoT video-capabilities adds `camera_observation` (device facts/support)
and `camera_capabilities` (IoT support), while retaining fresh main_app and
physical camera checks. This shape is proposed by MenuBuilder; IoT confirmation
is pending. Prepare sends full descriptor plus physical MAC. IoT owns the new
full-offer/proof/commit envelope, bounds4096/depth8 and durable counters/floors.
It must include the complete descriptor and binding, recompute canonical hashes,
and match fresh trusted geometry/track/physical evidence. Never reset authority
on upgrade. Observation alone grants no enrollment/readiness/lease authority.

## Gates and compatibility

`CAMERA_NATIVE_AUTO_ENABLED=false`, `CAMERA_MONOTONIC_RUNTIME_ENABLED=false`,
`L4MEDIA_CAMERA_NATIVE_AUTO_ENABLED=false` by default. Existing media camera/HEVC
gates also apply. Gate selection requests1.1 and fails closed if any peer lacks
support; it does not silently retry1.0. Constant1.0 uses existing paths with the
new gate off; Native Auto sentinels alone never authorize1.1. Cadence substitution
changes the hash and fails selected-schema validation. No production gates enabled.

## Verification

- Full backend pytest:996 passed,25 skipped,53 warnings (existing deprecation
  and AsyncMock warnings); final new test file separately23 passed.
- Ruff check/format: passed; Pyright app: zero errors/warnings.
- New BFF tests:23 passed, including device/IoT partial support, missing media
  cadence, geometry/policy invalidation and exact HTTP headers for all operations.
- Package verifier: schema/copy hashes, canonical vectors, policy tamper,
  partial negotiation and frozen baseline passed.
- WSL Ubuntu22.04 `make test` and production `make` with
  `-O2 -Wall -Wextra -Werror -pedantic -std=c11 -D_GNU_SOURCE`: passed.
  Includes legacy/v2/camera1.0 suites and new camera1.1 C canonical bytes,
  immutable geometry/policy, replay, gated START, selected renew/STOP tests.
- Changed/new text secret scan: no candidates; git diff --check passed.
- UI: no new source changes; PR110 contains115 unit and22 browser regression
  passes (camera20, desktop2), build and cancellation/error/renew coverage.
- NOT RUN: real Redis restore integration (no isolated Redis configured),
  IoT/firmware cross-peer integration, original-RTP/decoded-camera/browser E2E.
  Local mock/C metadata evidence is not device acceptance.

## Next owner and evidence

User reports IoT continuation is not ready and awaits this handoff. Package and
schema SHA were supplied before runtime integration tests. IoT should pin the
schema, confirm the proposed HTTP projection, publish full enrollment0.2 fixtures
and its handoff; then compare canonical vectors and execute coordinated firmware
acceptance. A disagreement in projection requires adapter alignment, not relaxed
validation. Coordinator owns firmware canonicalization and full-offer validation.

External evidence: `D:/.codex/siplite-hevc/evidence/menubuilder-camera-native-auto11/`.
Contains package snapshot and final commit/PR provenance. No test credentials or
remote resources created. Context cards: MenuBuilder and l4media updated. Local
ignored build outputs remain in this isolated worktree for reproducibility.
