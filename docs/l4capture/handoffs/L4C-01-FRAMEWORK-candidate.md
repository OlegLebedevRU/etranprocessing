<!-- HANDOFF:H-L4C-01-v1:BEGIN -->
```yaml
handoff_id: H-L4C-01-v1
status: ACCEPTED
contract_kinds:
  - FRAMEWORK_FOUNDATION
  - C_INTERFACES
  - IPC_PROTOCOL
  - SAFETY_GATE
producer_prompt_id: L4C-01-FRAMEWORK
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-01-FRAMEWORK-report.md
producer_branch: l4capture/l4c-01-framework
producer_commit: b01a255
accepted_at_utc: 2026-09-21T12:00:00Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-01-FRAMEWORK-report.md
  - tools/l4capture/include/l4capture/types.h
  - tools/l4capture/include/l4capture/capture_backend.h
  - tools/l4capture/include/l4capture/encoder_backend.h
  - tools/l4capture/include/l4capture/ipc_protocol.h
  - tools/l4capture/include/l4capture/safety_gate.h
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - ae06777610960c5fb4636eb21273332824de6abfe5d20815b34e7e17961c81c7
  - 97495e4e38cd99a7a2fcf70cf6db4e338aadda6e66e3bd6bb46d1f01266a08a1
  - 4c032b47bd80d8e3db07da785a03e4e0c4837f45f117234a5cbf8df017855c85
  - bc9f9a795e3779a89166e55f6ae0c1b1913e0e0c2cc4b500781de872db182986
  - 1f9970e01df1afbd85ed82dfd839b470543038981313d2fb16a3b3dec3a1e009
  - 7b0d133e0578f2ff70f72aa9f986f929895cc057526642e3df483b57aa7d342e
  - eb3154cca97966a114bac756de7a75513b0affee247d18ec9d7c53371e6d053e
  - 8994459f794435b46d94431011489e8a6e0f6ff9c201b4124c4f6ba090ace729
compatibility:
  backward_compatible_with: []
  breaking_changes: false
  notes: Стартовый каркас нативного модуля l4capture.exe. Базовые C-интерфейсы, бинарный IPC, deadline/safety-контроль, чистый C99/C11, /MT, поддержка Windows 7 SP1 (x86/x64).
deployment_status: LOCAL_BUILD_VERIFIED
deployed_environment: local_development
feature_flags:
  l4capture_native_pipeline: enabled
contract_payload:
  identifiers: lease_id (UUID), stream_id (UUID), request_seq (u64), geometry_generation (u64)
  c_interfaces:
    - ICaptureBackend (init, acquire_frame, release_frame, destroy)
    - IEncoderBackend (init, encode, force_idr, release_au, destroy)
    - FrameView (BGRA top-down, physical_rect, monotonic pts_ms)
    - AccessUnit (NAL descriptors, is_idr, pts_ms)
  ipc_protocol:
    wire_format: Little-Endian, 16-byte header (version:u16, type:u16, payload_len:u32, seq:u64), payload <= 4096 bytes
    commands: CMD_START (0x0001), CMD_RENEW_LEASE (0x0002), CMD_STOP (0x0003), CMD_FORCE_IDR (0x0004)
    events: EVENT_READY (0x0101), EVENT_METRICS (0x0102), EVENT_DEGRADED (0x0103), EVENT_ERROR (0x0104)
    transport: 2 anonymous inheritable pipes (--pipe-in, --pipe-out)
  safety_invariants:
    monotonic_clock: GetTickCount64, no grace period
    rtp_stop_timeout_ms: <= 500 ms upon stop/expiry/session_lock
    desktop_availability_poll_ms: <= 100 ms
    memory_admission_cap_mb: 128 MB (max 4K raster 8294400 px, max AU 2 MB)
    process_binding: JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, Session 0 rejected
supersedes: []
known_risks:
  - R1: Потеря состояния при UAC/Lock — парируется жестким safety gate <= 500 мс
  - R3: Перегрузка/утечки — парируется жесткими лимитами растра (4K) и отсутствием неограниченных очередей
  - R4: Несовместимость рантайма Win7 — парируется статической компоновкой /MT и subsystem 6.01
consumers:
  - L4C-02-GDI-CAPTURE
  - L4C-03-OPENH264-CODEC
  - L4C-04-RTP-SENDER
  - L4C-05-AGENT-ADAPTER
  - ALL_FOLLOWING
next_prompt_id: L4C-02-GDI-CAPTURE
```
<!-- HANDOFF:H-L4C-01-v1:END -->
