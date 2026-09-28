```yaml
handoff_id: H-L4C-05-v1
status: CANDIDATE
contract_kinds:
  - MEDIA_BACKEND_ADAPTER
  - PROCESS_JOB_ISOLATION
  - IPC_SUPERVISOR_CHANNEL
  - LEASE_DEADLINE_STRICT
  - INPUT_GATE_POLICY
  - BOUNDED_RECOVERY_LOOP
  - KIOSK_FOCUS_MANAGEMENT
  - KIOSK_LIFECYCLE_BACKEND
producer_prompt_id: L4C-05-AGENT-ADAPTER
producer_scope_project: tools/l4desk + tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-05-AGENT-ADAPTER-report.md
producer_branch: l4capture/l4c-05-agent-adapter
producer_commit: 752d1e8
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-05-AGENT-ADAPTER-report.md
  - tools/l4desk/include/media_backend.h
  - tools/l4desk/include/l4capture_adapter.h
  - tools/l4desk/include/input_gate.h
  - tools/l4desk/include/kiosk_focus.h
  - tools/l4desk/include/kiosk_lifecycle.h
  - tools/l4desk/bin/x86/l4desk.exe
  - tools/l4desk/bin/x64/l4desk.exe
artifact_sha256:
  - 1fa941a25690d2cf4c2ef4e9bf82a98c810f1c1eee63b424a06c21e8dfa13e52
  - 1e4f64c1e896045fbe9cf2580650571838baf5f5bfb1feee5e847fa712731a49
  - 3b2e332362b4063a4515e9da67d6c20f8ffb8170a894017852d818d8d96b5d12
  - 72c9aab3ce1dfff2e583dabc91255a31ec79c08a7dcae6f3b03a517b45906b44
  - 43985d880cd282c36bf7d7d35cbb8c02b1d35239423dc671b4895dc12899df20
  - 13964c471f6db0d5aca6a67651520003567cc633ae7e67216fa95ea49fb5537a
  - b6ee0ca025b75b3559e5991e297ebd73adbf067ed42d1983f94599cab403e25a
  - ce2cd720eef69147216a708ae0d055f2df92aa7f6ce37898b0a2c80a52ae4969
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
  breaking_changes: false
  notes: Adapter l4capture backend in l4desk. Job Object (KILL_ON_JOB_CLOSE) + handle allowlist, binary IPC, lease deadline no grace (<= 500 ms), strict Input Gate (deny default, allow low 480p), Kiosk Focus Management (3 modes + adaptive fallback, AttachThreadInput refocus, NACK kiosk_focus_lost), Kiosk Lifecycle Backend (kiosk_start/stop/restart/status), bounded recovery (5 attempts / 600s).
deployment_status: LOCAL_BUILD_VERIFIED
deployed_environment: local_development
feature_flags:
  l4capture_backend_adapter: enabled
  l4capture_input_gate: enabled
  l4desk_kiosk_focus_guard: enabled
  l4desk_kiosk_lifecycle_backend: enabled
contract_payload:
  process_management:
    job_object_flags: JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    job_handle_leak_prevented: true
    handle_allowlist: true (PROC_THREAD_ATTRIBUTE_HANDLE_LIST)
    session_0_check: rejected
    single_process_enforced: true
  lease_safety:
    grace_period_ms: 0 (strict deadline enforcement)
    stop_latency_ms: <= 500 ms
    drain_permitted: false
    renew_validation: monotonic deadline check, dedup enabled
  input_gate:
    permitted_profile: low (strictly base_480p)
    denied_profile: default (strictly denied, even if degraded to 480p)
    input_release_on_safety_events: true (input_release_all)
  kiosk_focus_management:
    modes_supported:
      - kiosk_mode (strict foreground guard & refocus)
      - adaptive_generic_fallback (automatic transparent desktop input when kiosk absent)
      - generic_desktop_mode (permanent transparent input routing)
    configuration_hierarchy:
      primary: CLI --kiosk-process <name>
      fallback: manifest or l4desk.json
      default: generic_desktop_mode
    refocus_mechanism: AttachThreadInput + SetForegroundWindow + ASFW bypass (Alt-key trick)
    fail_safe_input_guard: reject keyboard input with NACK kiosk_focus_lost only when kiosk running but unfocused
  kiosk_lifecycle_backend:
    scope: tools_only
    commands:
      - kiosk_start
      - kiosk_stop (two-phase: WM_CLOSE + TerminateProcess)
      - kiosk_restart
      - kiosk_status (IsHungAppWindow)
    security_allowlist: executable name strictly matched
  recovery_loop:
    max_restarts: 5
    window_sec: 600
    backoff: exponential with jitter
    cancel_on_stop_or_expiry: true
supersedes: []
known_risks:
  - R1: UAC/Lock — fail-closed <= 500 ms + input_release_all()
  - R3: Child hang — Job Object guarantees cleanup
  - R4: Focus hijack — AttachThreadInput refocus + fail-closed input block
  - R5: Kiosk hang — two-phase kiosk_stop with TerminateProcess fallback
consumers:
  - L4C-06-MILESTONE-LIVE-VERIFY
  - ALL_FOLLOWING
next_prompt_id: L4C-06-MILESTONE-LIVE-VERIFY
```
