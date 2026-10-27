# Camera compact tools — implementation intake

User approved MENUBUILDER-CAMERA-UX-PROPOSAL on28.10.2026, including a styled
online/offline camera list, one compact toolbar, manual Native/preset/custom WxH,
image drawer, power/day-night commands and configured-pin IR-cut diagnostics only.
Automatic raster and GPIO pin search remain deferred. User subsequently authorized
all agreed camera IoT contracts/gates and requested an audit for artificial blockers.

Scope: MenuBuilder frontend and its existing camera BFF. No shared DB/migration,
ProcessingBackend, desktop EXE, STM32 or firmware change. Existing IoT169 contract
is consumed; deployed IoT image is retained for camera-gate activation.

Authority: existing tenant/device access and operator checks, original publisher
quality ownership and camera lease fencing. Camera mutations are never retried
after ambiguous responses; configured IR-cut state persists across reload and
cleanup remains addressable by its original camera/session. No RPC168 route.

Validation: backend full tests/lint/types; frontend tests/build; visible Chrome
camera fixtures and desktop regressions; immutable images and scoped rollout;
separate real4623 camera command/video acceptance. Broker/HTTP ACK is not decode
or optical proof. UI sizes/presets are requests, not a new media whitelist.

Workspace: D:/.codex/siplite-hevc/worktrees/menubuilder-camera-ux.
Evidence: D:/.codex/siplite-hevc/evidence/task106-camera-ux.
