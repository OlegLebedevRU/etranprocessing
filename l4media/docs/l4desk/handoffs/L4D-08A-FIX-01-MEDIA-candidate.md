# H-L4D-08A-FIX-01-MEDIA-v1 — Candidate (DETACHED_V1)

```yaml
handoff_id: H-L4D-08A-FIX-01-MEDIA-v1
status: CANDIDATE
contract_kinds:
  - MEDIA_LIFECYCLE
  - JANUS_MOUNTPOINT_STABILITY
  - RECONCILE_RTP_AWARENESS
producer_prompt_id: L4D-08A-FIX-01-MEDIA
producer_scope_project: l4media
producer_report_path: l4media/docs/l4desk/handoffs/L4D-08A-FIX-01-MEDIA-report.md
producer_branch: l4desk/l4d-15c-media
registration_id: R-L4D-08A-FIX-01-MEDIA-v1
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: l4media/docs/l4desk/handoffs/L4D-08A-FIX-01-MEDIA-candidate.md
contract_version: 1.0.0
schema_revision: '027'
artifact_version: 1.0.0
artifact_paths:
  - l4media/ingress/src/media_lifecycle.h
  - l4media/ingress/tests/test_media_lifecycle.py
artifact_sha256:
  - 9863AED91A74B6C3DF53E59C4775D2EE04C35332A7D8675C19EB70FF4EFCD633
  - 871720259FBD1CEA15669D86943C0C984F990021D45F6E6DF12A290D78BAD4E7
compatibility:
  backward_compatible_with:
    - H-L4D-08A-MEDIA-v1
    - H-L4D-15C-MEDIA-v1
  breaking_changes: false
  notes: >
    A: janus_mountpoint_ensure reuses healthy mountpoint (ports+pin via Janus info);
    never destroy+recreate on already-exists. E: reconcile keeps orphan mountpoint/route
    while RTP is fresh (20s grace). External lifecycle API unchanged.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  unified_media_lifecycle_required: true
contract_payload:
  ownership:
    l4media_ingress:
      - Janus mountpoint ensure/reuse
      - reconcile RTP freshness grace
      - media session / route lifecycle (unchanged API)
  reconnect_resilience:
    mountpoint_reuse_on_idempotent_start: true
    no_destroy_under_live_watcher: true
    reconcile_keeps_fresh_rtp: true
    true_orphan_still_pruned: true
  live_evidence:
    suite: 9/9 PASS test_media_lifecycle.py
    signatures:
      - 'healthy, reusing (ports 6010/6011)'
      - 'healthy, reusing (ports 6046/6047)'
      - 'keep route for SN test-ae-rtp-1790285305 rtp_fresh'
    no_destroy_recreate: true
supersedes: []
known_risks: []
consumers:
  - L4D-14-MB
  - L4D-16-MB
next_prompt_id: L4D-14-MB
```
