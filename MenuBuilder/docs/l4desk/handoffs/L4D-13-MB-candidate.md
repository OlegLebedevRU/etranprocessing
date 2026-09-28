<!-- HANDOFF:H-L4D-13-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-13-MB-v1
status: CANDIDATE
contract_kinds:
  - API
  - DEPLOYMENT
producer_prompt_id: L4D-13-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-13-MB-report.md
producer_branch: l4desk/l4d-13-mb
producer_commit: pending
report_commit: pending
contract_version: 1.0.0
schema_revision: '027'
artifact_version: 1.0.0
candidate_format: DETACHED_V1
detached_candidate_approved: true
candidate_path: MenuBuilder/docs/l4desk/handoffs/L4D-13-MB-candidate.md
artifact_paths:
  - MenuBuilder/backend/app/main.py
  - MenuBuilder/backend/app/routers/auth.py
  - MenuBuilder/backend/app/routers/mcp_waitlist.py
  - MenuBuilder/backend/tests/test_mcp_waitlist_and_role5.py
  - MenuBuilder/frontend/src/App.tsx
  - MenuBuilder/frontend/src/api/finance.ts
  - MenuBuilder/frontend/src/api/mcpWaitlist.ts
  - MenuBuilder/frontend/src/components/OnboardingWizardModal.tsx
  - MenuBuilder/frontend/src/components/RefusalReasonCard.tsx
  - MenuBuilder/frontend/src/routes/console/ConsolePage.tsx
  - MenuBuilder/frontend/src/routes/devices/DeviceConsoleTab.tsx
  - MenuBuilder/frontend/src/routes/layout.tsx
  - MenuBuilder/frontend/src/routes/licenses/LicensesPage.tsx
  - MenuBuilder/frontend/src/routes/mcp/McpPromoPage.tsx
  - MenuBuilder/frontend/src/routes/settings/TerminalsSettingsPage.tsx
  - MenuBuilder/frontend/src/routes/video-surveillance.tsx
  - MenuBuilder/frontend/src/tests/l4desk-accessibility-responsive.test.ts
  - MenuBuilder/frontend/src/tests/l4desk-licenses-mcp.test.ts
  - MenuBuilder/frontend/src/tests/l4desk-profile-navigation.test.ts
  - MenuBuilder/frontend/src/tests/l4desk-refusal-reasons.test.ts
  - MenuBuilder/frontend/src/utils/navigationProfile.ts
  - nginx-configs/port_3000.conf
  - MenuBuilder/docs/l4desk/handoffs/L4D-13-MB-report.md
artifact_sha256:
  - 296e0ef944a9e1abcda254a28f19a77b4741e97b6c82b8a01c1379b1313cad06
  - 0857ae0b33a4d591e319f97f2e2ced8a779b168ad64363b6a864d9e3f75e51a1
  - 6f2bfdf626572eb4cfccbe12c7424bb426aefea365f447ded5e0876174a43095
  - e486e24d5d95eec1cc2c1037a4aae342b877216c96d0b6f0385749420a9f96fb
  - 04856fceb7af3951c448516fb78aad912dd155ca7247fb6d99632139ada9575d
  - aaafeecbe1d0157ce0e06b2aa211a0ae87676592ef625f1f3b8c7cddde296c54
  - d1201e858e13306e8d93d70611ce1b9fbaaad9fa0b01793bee988a6ae46e872b
  - d6497e7df398b0c755071099fa86a45e29fdebe594d61db0e2259f62416b6275
  - d63dee75b4eea4e9c5ef1311a3ec767431d1010edfc410ab9858132a1cfee12c
  - 4167364fa0454f01aaf6fb5a4059a885be757fe56a4e59e392d6eb9f19606516
  - f7ae07658d3024100708242bb399ea87aaa97c63cffa5f2e9f274d677694a797
  - a858ca421328747205c73a759cfb13088f317523f46d5524ac994b89b3a065ee
  - 13da06cba68c032efd5372f6881668487d6feeb0eec3369139589b6724725541
  - 928e63d6b713c9db4c98f615a51a7239d11db96fc581afa8d9fb8ff6ce7ecca5
  - 4d686b27bf504011da0df285383ff45114e19ebd0523a1526f77af65b8cbcb67
  - 376b6a5884185a2ab6d912b9a8be8c4fee0db9d3789ef8b2163c4e12e7b9475d
  - 2515d12f032e3e4c31e92b69232a4a62b21a474335d1c00b8f85e39a90f8debb
  - 4232e6d90806b8413f465de1682c0422120e0e3f8440c33a1aa887827c21d3fc
  - 80b11223a85e23fe1456914a8280b098a48a495a1a7ec4b7067b1ca71353035e
  - 95bc280c549c10e7354932bcfff576c660f0a686de26fe9b4cb098a542efa506
  - bbbf70d9eed9d8e2c2e84960a2195f301a1ba7e1ec0d76662ae8452aee4f2f3d
  - e76037bdee1d28411ff843d6ee8dc3e0e8dff3c95382e2e972566afda1f6ef0d
  - b9027374de01a8e01b2c56e7cc30467747fbc35a8aae646ac6a6eadbe7886c1b
compatibility:
  backward_compatible_with:
    - H-L4D-12-MB-v1
    - H-L4D-06C-MB-v1
    - H-L4D-08B-MB-v1
  breaking_changes: false
  notes: "Unified L4Desk navigation profile and onboarding/licenses UX in single SPA without duplicating console/video components. Classic MenuBuilder profile remains functional and untouched for existing users."
deployment_status: STAGED
deployed_environment: staging
feature_flags:
  l4desk_navigation_profile_enabled: true
  l4desk_ui_enabled: true
contract_payload:
  navigation_sections:
    - video: /video
    - settings: /settings
    - console: /console
    - mcp: /mcp
    - licenses: /licenses
  rejection_reasons:
    - session_conflict
    - offline
    - provisioning_pending
    - free_quota_exhausted
    - grace_blocked
  onboarding_wizard_steps:
    - step_0: create_terminal
    - step_1: pin_and_agent_download
    - step_2: readiness_online_polling
    - step_3: launch_single_session
  mcp_endpoints:
    join_waitlist: POST /api/v1/mcp/waitlist
    waitlist_status: GET /api/v1/mcp/waitlist/status
supersedes: []
known_risks: []
consumers:
  - L4D-14-MB
next_prompt_id: L4D-14-MB
```
<!-- HANDOFF:H-L4D-13-MB-v1:END -->
