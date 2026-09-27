# Detached candidate — MB media credential rotation

<!-- HANDOFF:H-L4D-18D-AUTH-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-18D-AUTH-MB-v1
status: ACCEPTED
candidate_format: DETACHED_V1
contract_kinds: [REPORT]
producer_prompt_id: L4D-18D-AUTH-MB-FIX-01
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-18D-AUTH-MB-FIX-01-report.md
producer_branch: l4desk/l4d-18d-mb-auth
producer_commit: 04560ca396f5590a8d25542382ebd4641d8e7ccf
report_commit: 7af9517dfea82d01a1cc6f62c8eb176e70353b97
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: media_management_openapi_1.0.0
artifact_version: 1.0.0
artifact_paths:
- MenuBuilder/docs/l4desk/handoffs/L4D-18D-AUTH-MB-FIX-01-report.md
artifact_sha256:
- 61d74d4ff286f8d31c3d842b04bcedd0b96224425e7c1fe2f711137c00c0d010
artifact_commits:
- 7af9517dfea82d01a1cc6f62c8eb176e70353b97
compatibility:
  backward_compatible_with: [H-L4D-18D-MEDIA-AUTH-CONTRACT-01-v1]
  breaking_changes: false
  notes: API unchanged; private token rotated in both affected consumers.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags: {}
contract_payload:
  verification_status: VERIFIED
  consumer_env_key: L4MEDIA_SERVICE_TOKEN
  consumers_recreated: [menubuilder-backend, l4desk-e2e-test-backend-1]
  both_authorized_metrics_http: 200
  missing_wrong_old_token_http: 401
  unrelated_container_ids_unchanged: true
  application_images_unchanged: true
  secrets_exported: false
supersedes: []
known_risks:
- Full media/browser/archive acceptance remains owned by 18D media step.
consumers: [L4D-18D-MEDIA-FIX-01]
next_prompt_id: L4D-18D-MEDIA-FIX-01
```
<!-- HANDOFF:H-L4D-18D-AUTH-MB-v1:END -->
