# H-L4D-13-MB-FIX-01-v1 — Candidate (DETACHED_V1)

```yaml
handoff_id: H-L4D-13-MB-FIX-01-v1
status: CANDIDATE
contract_kinds:
  - API
  - DEPLOYMENT
  - UX_NAVIGATION
  - TERMINAL_IDENTITY
producer_prompt_id: L4D-13-MB-FIX-01
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-13-MB-FIX-01-report.md
producer_branch: l4desk/l4d-13-mb-fix-01
producer_commit: 45bd645a1f468a74e4cbfb705caccf309bfd1162
report_commit: a5be39c319c92b6a2937ff017a033f84fae4ca0d
registration_id: R-L4D-13-MB-FIX-01-v1
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-13-MB-FIX-01-candidate.md
contract_version: 1.1.0
schema_revision: '027'
artifact_version: 1.1.0
artifact_paths:
  - MenuBuilder/backend/app/services/terminal_creation_service.py
  - MenuBuilder/backend/app/services/terminal_onboarding_service.py
  - MenuBuilder/backend/app/routers/admin_terminals.py
  - MenuBuilder/backend/app/schemas/__init__.py
  - MenuBuilder/frontend/src/routes/l4desk/L4DeskTerminalsPage.tsx
  - MenuBuilder/frontend/src/components/OnboardingWizardModal.tsx
  - MenuBuilder/frontend/src/routes/layout.tsx
  - MenuBuilder/frontend/src/App.tsx
  - MenuBuilder/frontend/src/routes/console/ConsolePage.tsx
  - MenuBuilder/frontend/src/routes/settings-layout.tsx
  - MenuBuilder/frontend/src/routes/settings/TerminalsSettingsPage.tsx
  - MenuBuilder/frontend/src/api/settings.ts
  - MenuBuilder/frontend/src/tests/l4desk-profile-navigation.test.ts
  - MenuBuilder/backend/tests/test_terminal_creation_service.py
  - MenuBuilder/backend/tests/test_terminal_onboarding.py
  - MenuBuilder/backend/tests/test_admin.py
  - MenuBuilder/CHANGELOG.md
  - MenuBuilder/docs/l4desk/handoffs/L4D-13-MB-FIX-01-report.md
artifact_sha256:
  - 963436ea7f35ab4d8869d840ffc5dca424bd4cb8c4bd546a4e080f6870bc0ee6
  - f218c9337c8c56719eaa1ee6f33fa93337fc79070459d23a587708312b27d0f9
  - 57012055e139e21838a4d2fc9cda8d7acb4ed592a7ef8c790f230b8bd50a7bef
  - 19b7dae418f5d216fb11f7036e1d1b2185516786a860ed5c2e16cb8dd3705af3
  - 8c2200fbabc968e5a9ee26218ad2a6874abbacabb5408b9a90eabeca0f3d5c49
  - 68cdf4b8d4d3b59a394f9d3f8f4a3bfb3dfe18573bfb8606ba86f47428e3fa63
  - 930f9590ee8d4f37b0c775bb4373b5c17421ee9c5ddea1da317b8f364fcc479e
  - 9fe0438a784effe2833c858a4f33b167c6ace9ac4cd706b8625727a497717f0f
  - 87993119e88718436a5402fa2f427c2e581423037fb4492950c573ee4be6b4a0
  - 9dccc20c871653c21cf129e471ba6ed42090a111f65127f96f4fb105ed5747e0
  - 40e3609d482a386601775589c6816c2e9865db01deec97ad45d224a9c29f2f7d
  - ac6d7b939c986ebc794a91d5220d6f58a47a6fe9872a15be126beeb4d4dcfcb9
  - 82d1feddea6a6b76cac6cff6eb63ba35b684f5e5276a7e782b0ad2a9820c1403
  - 14bb44f249ba5b03bd087c2d08ad8f49029088db03cfcde1dae8b8663a798e33
  - dd1805f2acbc20cf138050e96cb553a0b167dbd3df48a382a79c344068600dbe
  - 61c12e74d373f7c12e6c4b942c48e3dde996cd691ac01f3a73b88abd404419bd
  - c9a32f621e67756a809cbfb26293d0c53a0040de8acaf552b5a702a6f5a28457
  - 3a848d1126aabb9eac57f268cb4b9b138a6f72d91651fa73023e8fc2abd2ab46
compatibility:
  backward_compatible_with:
    - H-L4D-13-MB-v1
    - H-L4D-12-MB-v1
    - H-L4D-08B-MB-v1
    - H-L4D-06C-MB-v1
  breaking_changes: false
  notes: >
    Corrective step replaces the UX portion of the previous L4D-13 result
    (H-L4D-13-MB-v1): root L4Desk section «Терминалы» above «Видеонаблюдение»,
    single canonical server-side terminal creation use case, server-owned SN
    and device_id in range 1000001…1999999. Classic profile is unchanged.
    Provisioning no longer overwrites issued SN/device_id. Idempotent L4Desk
    create still returns the original identifiers.
  supersedes_ux_of:
    - H-L4D-13-MB-v1
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  l4desk_terminal_onboarding_enabled: unchanged
  l4desk_enabled: unchanged
  l4desk_navigation_profile_enabled: unchanged
  l4desk_ui_enabled: unchanged
contract_payload:
  navigation_sections:
    - terminals: /terminals
    - video: /video
    - console: /console
    - settings: /settings
    - mcp: /mcp
    - licenses: /licenses
  terminal_identity:
    sn_formula: a4b<7-digit device_id>c<5-digit random>d<DDMMYY>
    sn_source: server_only
    device_id_source: server_only
    device_id_range: [1000001, 1999999]
    user_supplied_sn: rejected_ignored
    user_supplied_device_id: rejected_ignored
  create_use_case: app.services.terminal_creation_service.create_terminal_business_record
  provisioning_mutation_of_identity: prohibited
  console_terminal_creation: prohibited
  classic_profile_changes: none
supersedes: []
known_risks:
  - "test_step6_quick_actions::test_stream_start_504_terminal_timeout_not_swallowed fails 502 vs 504 in media lifecycle path (outside this prompt scope)."
  - "Legacy device_id values outside 1000001…1999999 remain in DB and are not migrated."
consumers:
  - L4D-14-MB
  - L4D-17E-MB
  - L4D-18E-MB
next_prompt_id: L4D-14-MB
```
