# H-L4D-REDIS-MEDIA-01-v1 — Candidate (DETACHED_V1)

```yaml
handoff_id: H-L4D-REDIS-MEDIA-01-v1
status: CANDIDATE
contract_kinds:
  - MEDIA_LIFECYCLE
  - REDIS_OWNERSHIP
  - DEPLOYMENT
producer_prompt_id: L4D-REDIS-MEDIA-01
producer_scope_project: l4media
producer_report_path: l4media/docs/l4desk/handoffs/L4D-REDIS-MEDIA-01-report.md
producer_branch: l4desk/l4d-15c-media
registration_id: R-L4D-REDIS-MEDIA-01-v1
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: l4media/docs/l4desk/handoffs/L4D-REDIS-MEDIA-01-candidate.md
contract_version: 1.0.0
schema_revision: '027'
artifact_version: 1.0.0
artifact_paths:
  - l4media/ingress/src/media_redis.h
  - l4media/ingress/src/media_lifecycle.h
  - l4media/ingress/src/l4media_ingress.c
  - l4media/ingress/Makefile
  - l4media/ingress/Dockerfile
  - l4media/compose.yaml
artifact_sha256:
  - D5161A3D3E6D45B134924B9F696E5481D5D9E4E5111E9C22368D827A87CBFB89
  - CF92F54CEFD7C4110B0FD1062AB5A003C1E069CA30166A2C86DEECDCCB588F8B
  - 5FFD888D08DEB2462FC76D94D3FB4AB21756B707446108D71623F44580443259
  - C3033D9ED6EABCBB0D2E68704AAD71B786750323109DC3049ABC250CE774AEED
  - 342AC337D41C66B5CAC72BA83C76FD75D36C7B769CB2EFBFBEEAA5281F3B742C
  - EA03E727216A42B9A6CBAB3891949FAFA08193552E58271E1B6701EE381ABCA1
compatibility:
  backward_compatible_with:
    - H-L4D-08A-MEDIA-v1
    - H-L4D-08A-FIX-01-MEDIA-v1
    - H-L4D-REDIS-IOT-01-v1
    - H-L4D-15C-MEDIA-v1
  breaking_changes: false
  notes: >
    Redis DB2 ownership for media sessions/routes/mountpoints.
    TTL = ttl_sec+60 + refresh. Bootstrap restore on ingress start.
    Reconcile keeps redis_owned. Optional REDIS_URL (in-memory degrade).
    External lifecycle API unchanged. Empty pin serialized as '-'.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  unified_media_lifecycle_required: true
contract_payload:
  redis_database: 2
  redis_key_prefixes:
    session: "media:session:{session_id}"
    session_by_sn: "media:session-by-sn:{sn}"
    route: "media:route:{sn}"
    mountpoint: "media:mountpoint:{mountpoint_id}"
  ttl_policy:
    ownership: ttl_sec_plus_60_refresh
    signaling_next_step: 10_to_300_sec
  verification:
    unit_tests: 7/7
    lifecycle_suite: 9/9
    restart_soak: PASS
    keys_verified: true
    mountpoint_no_destroy_recreate: true
  live_evidence:
    restored_sessions: 3
    health_after_restart: active
    reconcile_orphans_cleaned: 0
    rtp_after_restart: live
    janus_mp_age_ms_continuous: true
supersedes: []
known_risks: []
consumers:
  - L4D-14-MB
  - L4D-16-MB
next_prompt_id: L4D-14-MB
```
