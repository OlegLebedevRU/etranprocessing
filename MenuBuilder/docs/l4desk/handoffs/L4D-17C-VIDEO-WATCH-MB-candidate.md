# Candidate Handoff Package: L4D-17C-VIDEO-WATCH-MB (DETACHED_V1)

<!-- HANDOFF:H-L4D-17C-VIDEO-WATCH-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-17C-VIDEO-WATCH-MB-v1
status: CANDIDATE
contract_kinds:
  - API
  - EVENT
  - DEPLOYMENT
producer_prompt_id: L4D-17C-VIDEO-WATCH-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-17C-VIDEO-WATCH-MB-report.md
producer_branch: l4desk/l4d-17c-video-watch-mb
producer_commit: 48236d58d809438a448b3c87ae6aa9017d176be3
report_commit: 5020aa3c568b966db29073c8c4e00f4d26b59fca
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: 2026-09-26-v1
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-17C-VIDEO-WATCH-MB-candidate.md
artifact_paths:
  - MenuBuilder/backend/app/config.py
  - MenuBuilder/backend/app/routers/video_control.py
  - MenuBuilder/backend/test_video_watch_ws.py
  - MenuBuilder/backend/tests/test_video_watch.py
  - MenuBuilder/frontend/src/routes/video-surveillance.tsx
  - MenuBuilder/docs/l4desk/handoffs/L4D-17C-VIDEO-WATCH-MB-report.md
artifact_sha256:
  - 71ea749442cb9c791863fc15e86633d49c3d36d87064ef9cc677b6254686f018
  - 315b483955f3aa959c874a70ee3ca7448025d3c266c8fff51a2083bc6dc2ab8a
  - 3035751d3955d6a49d93c5bf979bb15725f8fa1bfdaeab25ab852601ab204dd0
  - 7faae5f9028067c0825aed56cf5b058c2882e36aa9bcf299986e9d3811c33dd1
  - d276a49c076a4acf5e5c934fd1cc7ac0ec93dee0ed06820591ac201865a63ebb
  - a1dbafd9b70e51493fe6f6a84e4f466cae8620ac3fbbb3dfc40ae5ba06af6001
compatibility:
  backward_compatible_with:
    - H-L4D-17C-VIDEO-WATCH-IOT-01-v1
    - H-L4D-17D-MEDIA-v1
  breaking_changes: false
  notes: "Browser receives invalidate only; authoritative state remains in REST status. Existing control and lease APIs are unchanged."
deployment_status: DEPLOYED
deployed_environment: dev.leo4.ru:3000
feature_flags:
  web_concurrency: "1"
contract_payload:
  registration_id: R-L4D-17C-VIDEO-WATCH-MB-v1
  upstream_watch: /api/internal/v1/remote-input/ws/watch/{sn}
  browser_watch: /api/v1/video/devices/{device_id}/watch/ws
  browser_event: '{"type":"invalidate"}'
  authoritative_status:
    - /api/v1/video/devices/{device_id}/control/status
    - /api/v1/video/devices/{device_id}/stream/state
  reconnect: "BFF reauthorizes at 60 seconds; browser reconnects with 1-15 second backoff and REST resnapshot"
  fallback: "REST status pair every 5 seconds while WS is unavailable"
  security: "cookie and video:view; tenant/device ownership; same-origin; no browser JWT or internal key in URL/payload; read-only; no lease mutation"
  polling_measurement: "36 status GET/min before; approximately 1.9/min with live WS; approximately 95% reduction"
  runtime_image: sha256:e9559f30045479dbc1a4affc6b87ad9dcff7699c0249c28b18d6082ccdaf82ee
  runtime_line_endings: "backend files are CRLF copies of Git/raw LF bytes; normalization matches exactly"
supersedes: []
known_risks:
  - "Browser fallback under an actual WS network outage was not reproduced; source-level controlled failure passed."
  - "Next regular MenuBuilder release must include deployed config, route, and frontend versions from this report."
consumers:
  - L4D-17E-MB
  - L4D-17F-DOCS
next_prompt_id: L4D-17E-MB
```
<!-- HANDOFF:H-L4D-17C-VIDEO-WATCH-MB-v1:END -->
