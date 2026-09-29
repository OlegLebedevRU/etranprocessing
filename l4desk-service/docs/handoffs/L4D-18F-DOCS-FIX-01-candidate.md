# L4D 18F restricted release candidate — DETACHED_V1

<!-- HANDOFF:H-L4D-18F-DOCS-v1:BEGIN -->
```yaml
handoff_id: H-L4D-18F-DOCS-v1
status: CANDIDATE
contract_kinds: [REPORT, SEQUENCE_GATE]
producer_prompt_id: L4D-18F-DOCS-FIX-01
producer_scope_project: l4desk-service
producer_report_path: l4desk-service/docs/handoffs/L4D-18F-DOCS-FIX-01-report.md
producer_branch: l4desk/l4d-18f-docs
producer_commit: 1a06f6c13091b985f1e0596d4d36f385c35bb9f8
report_commit: 8b2670dd6cadc3c20c5b1c8abda398d98d01e170
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: '028'
artifact_version: 18F-restricted-1
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: l4desk-service/docs/handoffs/L4D-18F-DOCS-FIX-01-candidate.md
artifact_commit: 8b2670dd6cadc3c20c5b1c8abda398d98d01e170
artifact_paths:
  - l4desk-service/docs/handoffs/L4D-18F-DOCS-FIX-01-report.md
artifact_sha256:
  - c0053003f9af42dd9699ba9634df97e33f11e4b95d16bc4cf49dc22d25ad5a5c
compatibility:
  backward_compatible_with:
    - H-L4D-18A-SHARED-v1
    - H-L4D-18B-PB-v1
    - H-L4D-18C-IOT-v1
    - H-L4D-18D-MEDIA-v1
    - H-L4D-18E-MB-v1
  breaking_changes: false
  notes: 'Restricted software release with post-18E video UX, l4tools 1.9.0 and app1 shared-lease correction; no new wire contract.'
deployment_status: DEPLOYED_DISABLED
deployed_environment: production
activation_scope: restricted
feature_flags:
  l4desk_enabled: false
  l4desk_registration_enabled: false
  l4desk_billing_enabled: false
  l4desk_ui_enabled: false
  l4desk_policy_enforcement_enabled: false
  l4desk_policy_shadow_mode: true
  iot_consumer_enabled: false
  l4desk_terminal_onboarding_enabled: true
  l4desk_financial_core_enabled: true
  l4desk_entitlement_worker_enabled: false
  l4desk_metering_close_worker_enabled: false
  effective_yookassa: false
contract_payload:
  registration_id: R-L4D-18F-DOCS-FIX-01-v1
  processing_backend: '7da3e0762fbd1f28a8a479ad64f3af9a7cec2a10@sha256:bc0e5b131b4e47ec43e0695e7f87d8cc3e158bb26756bc2fb88023aa76a75a00'
  menubuilder_backend: '434620d5367cb59999b136dfb41eaa0afbdcaa91@sha256:6622d49a03570b626c2206783f0c95fbda425d5971164ab12453ba75513356d7'
  menubuilder_frontend: '6ddc97af056db42e2b413fb404e9947fdbfa6e78@sha256:95c10e7df95f0de188390eb13cfd3b8a3b7719c9d9afae490eb3522f193655ea'
  frontend_index_sha256: 147d95bee5cda0b279600099b30c5c07002717101d9b36f69685a50f66bcd3c8
  iot_app1: 'bb661bb1f429f01d75a2a1d016c2dc8c2a7d5621@sha256:957b08ce2b3a6ec44514f99c05d55c4e3d1ff4f9f885b86a9da7eadf3bffa36d'
  l4mcp: '7da3e0762fbd1f28a8a479ad64f3af9a7cec2a10@sha256:7dcc28d19286d8ce99aedbdd4ccd2c4492ab0f7d7163416e9d9f947813069f4e'
  media_ingress: '801186d8699f293c70bd1e4d65f4f7e9b91a9a7e@sha256:1b9242b290d975e769acd3a35eb5d16a286f91e71b08356458643863c58b1cd7'
  media_nginx: '7da3e0762fbd1f28a8a479ad64f3af9a7cec2a10@sha256:fb137647e25bed749f1cf87cc0398eb8f072a900b7dfa124222cb9655fe911bd'
  media_janus: 'sha256:93265665a92482ec1de2c9571a08d27c87b1dee06efc000f717ed42dfe2438ae'
  l4tools_1_9_0_installer_sha256: ddf7d930f21b56a9b03a48226d94351ecd373c07fb4011fe8a47157daf129719
  browser_smoke: '1000009 HD: video, input detach, fresh RTP and keepalive for >40s, normal stop; Medium/HD and F8 checked separately.'
  commercial_activation: 'Not authorized; five missing tenant 1000 source hashes remain.'
  archive: 'Synthetic checksum/restore evidence only; no production archive mount/backup/restore or purge claim.'
supersedes: []
known_risks:
  - 'Five historical source hashes for tenant 1000 sessions 476-480 are missing; no global commercial activation or global mismatch=0 claim.'
  - 'Production archive volume is not mounted; backup/restore, hot retention and purge readiness are outside this restricted release.'
  - 'No live YooKassa charge, Windows 7/POSReady x86 runtime or three-year technical retention observation is claimed.'
  - 'Package 1.9.0 is published for x86/x64, but only l4capture x64 was installed on test terminal 1000009; installer has no Authenticode signature.'
  - 'Transient real-login 502 observed in prior 18F read-only check; later valid login and video smoke succeeded. Issuer root cause not established.'
consumers: []
next_prompt_id: NONE
```
<!-- HANDOFF:H-L4D-18F-DOCS-v1:END -->

This candidate is not an accepted handoff. Independent controller review and
append-only journal acceptance are required before `CLOSED_ACCEPTED`.
