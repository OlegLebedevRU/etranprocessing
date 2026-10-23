# HEVC admission for all terminal identities

## Task intake

- Owner direction: remove the pilot SN restriction for all devices; the server
  chain is ready to accept HEVC, including the camera integration being added.
- Scope: MenuBuilder backend admission/configuration and its immutable-image
  delivery. Native1.13.48 EXEs, MQTT protocol and shared DB are unchanged.
- Ownership: BFF owns codec policy; IoT owns authorized lease/control; source
  capabilities own actual encoder/format support; ingress/Janus own media format.
- Preserve user/tenant permissions, lease ownership, fresh capability evidence,
  selected source/profile, immutable descriptor and no software HEVC fallback.
- Root cause: installed48 does not remove the server's former one-SN allowlist.
  The reported1000007 was rejected as disabled_by_policy before capability probe.
- Verification: failing regression for non-pilot SN before removal; fresh READY
  availability afterward, global orchestration-off denial, owner-session and
  stream-permission regressions; quality checks and live1000007 browser decode.
- Deployment coordination: another MenuBuilder release may be in progress.
  Build separately; preserve current operator Compose files, use the standard
  deploy lock, recheck current image/source and active leases before rollout.
  Never replace another in-progress deployment or overwrite its image pin.

The obsolete video_hevc_allowed_sns setting and per-SN gate are removed.
Existing global orchestration, transport and hardware/format checks still apply.
Old VIDEO_HEVC_ALLOWED_SNS environment values are ignored by Settings.extra=ignore;
they cannot silently keep new terminals outside the HEVC path after this release.
Real camera admission belongs to its producer path; the synthetic test-camera
fixture policy does not become a hardware-readiness assertion for real cameras.

Delivery and live result will be appended after verification. No claim of full
TASK-60 acceptance, fleet matrix completion or a new native release is made.
