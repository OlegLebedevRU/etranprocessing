# Detached candidate — production media release

<!-- HANDOFF:H-L4D-18D-MEDIA-v1:BEGIN -->
```yaml
handoff_id: H-L4D-18D-MEDIA-v1
status: ACCEPTED
candidate_format: DETACHED_V1
contract_kinds: [API, REPORT]
producer_prompt_id: L4D-18D-MEDIA-FIX-01
producer_scope_project: l4media
producer_report_path: l4media/docs/l4desk/handoffs/L4D-18D-MEDIA-FIX-01-report.md
producer_branch: l4desk/l4d-18d-media
producer_commit: e6e681dcf74fb0e81da5cf1f7f0fc2d39dca7f33
report_commit: ed453db73b2dad4855cb099dfa5fdf9cf94ff655
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: media_management_openapi_1.0.0
artifact_version: 1.0.0
artifact_paths:
- l4media/docs/l4desk/handoffs/L4D-18D-MEDIA-FIX-01-report.md
- l4media/docs/l4desk/handoffs/L4D-18D-MEDIA-FIX-01-evidence.json
- l4media/ingress/openapi.json
- l4media/deploy/baseline-images.json
artifact_sha256:
- 85f554fc140ee10a2b7ba0b11d096a4e010af57a013dc2e723f37962b8f22b30
- 7ddc676e9fd3f002a9b68ff21fe30146fe44367f925f5baed945b9a5e90896b6
- 02298c2cba4c311e24d7ba893edc206ac01cb902746a931976c28225514cfd1c
- 7f5aeb8b1ec05ba6f8bf5c6ce88251a427ef91285d6cc2fffe4b43ace0777b75
artifact_commits:
- ed453db73b2dad4855cb099dfa5fdf9cf94ff655
- ed453db73b2dad4855cb099dfa5fdf9cf94ff655
- e6e681dcf74fb0e81da5cf1f7f0fc2d39dca7f33
- 815a7c33fae936c5537b7126a70dd1c11af52cad
compatibility:
  backward_compatible_with: [H-L4D-17D-MEDIA-CONTRACT-01-v1]
  breaking_changes: false
  notes: Media API unchanged; matching private credentials required and rotated in both MB consumers.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  L4MEDIA_ARCHIVE_WORKER_ENABLED: false
  archive_purge: false
contract_payload:
  verification_status: VERIFIED
  registry_delivery: pull_by_digest
  janus_upstream_revision: 3c39ce8cf11c54cf6f1607030a47ac9db798389a
  janus_rebuild: explicit_user_command_only
  ingress_build: approved_176_or_local_linux_fallback
  ingress_digest: sha256:3e0e0341f37702bedef06ca87538c494c7e6bdf8f87152e6d5901e4d792322fd
  janus_digest: sha256:93265665a92482ec1de2c9571a08d27c87b1dee06efc000f717ed42dfe2438ae
  auth_coordination_handoff: H-L4D-18D-AUTH-MB-v1
  human_browser_smoke: passed
  producer_rtp_and_stop: passed
  archive_dry_run_restore: passed
  secrets_exported: false
supersedes: []
known_risks:
- Rollback readiness verified; rollback not executed.
- Archive worker/purge disabled; off-host backup schedule and procedural retention are outside this step.
- Local fallback documented and syntax checked; actual release built on available 176.
consumers: [L4D-18E-MB]
next_prompt_id: L4D-18E-MB
```
<!-- HANDOFF:H-L4D-18D-MEDIA-v1:END -->
