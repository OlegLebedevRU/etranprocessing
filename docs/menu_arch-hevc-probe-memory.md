# Desktop HEVC probe memory: MenuBuilder handoff

2026-10-24. Local changes on b608a9dd96d4dd5c6faa867e86e39dd2d3b64688.
No publication, image build, runtime patch or deployment performed.

## Task intake

- Goal/type: cross-stack reuse of confirmed desktop HEVC instead of repeated probes.
- Scope/owners: MB owns BFF/UI, IoT owns query/correlation, l4desk owns measurement/cache.
- Producer → transport → consumer: l4desk → MQTT ctl → IoT REST → MB BFF/UI.
- Invariants: tenant/lease/policy, exact source/profile/track, epoch/geometry,
  short-lived snapshot checks and existing START remain. Camera path unchanged.
- Risk: old strict readers reject new fields; coordinated rollout required.
- Context: MenuBuilder component card, remote-control contract; intake and
  frontend-remote-control-safety skills. Firmware writers are outside scope.
- Verification: focused backend tests/quality, frontend build/unit and mocked
  browser checks; no new live stream/lease, terminal install or production mutation.

## Changes

contracts/hevc-probe-memory-v1 documents every changed boundary. Capability adds
optional measured_at_ms (actual encoded measurement), while checked_at_ms means
the current applicability check; validate measured <= checked. Snapshot TTL and
all existing START checks stay. Frozen G0 files are unchanged.

BFF forwards optional profile_id to the existing IoT GET only for desktop disp:
sources. A missing HD can be probed without repeating known Medium. No START fields.
IotPlatformClient forwards the query. Native keeps persistence; MB adds no cache DB.

UI displays encoder_busy as a deferred check for an unknown mode. A verified
mode stays available through native cache reuse. After inventory refresh, fetch
capability again when loading finishes; prevent an older response from replacing
the current selection using existing effect cancellation. No stream commands.
Camera headers, routing and firmware controls are unchanged.

## Checks / rollout

-63 backend pytest passed; Ruff check/format and Pyright0 errors.
-Frontend build,106 unit tests and20 browser tests passed (2 desktop,18 camera).
  Browser API fully mocked; refresh/source selection issue no POST or stream commands.
-Actual cached native x86/x64 fixture replies accepted; MB selects HEVC low.
-No live hardware or deployed chain acceptance for this patch.

Order: IoT readers → native l4desk → this combined MB reader/sender/UI patch.
For independently staged MB reader, deploy reader before native and sender after.
Keep old capability without measured_at_ms accepted. Do not send profile_id to
old strict native. Deliver source through the normal reviewed repository/image
flow; no server runtime-only edits. Detailed logs/three patches are in l4tools
.scratch/mode-memory; final handoff is docs/hevc/mode-probe-memory-handoff.md there.
