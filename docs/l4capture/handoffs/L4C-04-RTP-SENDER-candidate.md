<!-- HANDOFF:H-L4C-04-v1:BEGIN -->
```yaml
handoff_id: H-L4C-04-v1
status: ACCEPTED
contract_kinds:
  - RTP_SENDER
  - RFC_6184_PACKETIZER
  - FUA_FRAGMENTATION
  - RTCP_SENDER_REPORT
  - UDP_LOOPBACK_TRANSPORT
  - E2E_MEDIA_PIPELINE
producer_prompt_id: L4C-04-RTP-SENDER
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-04-RTP-SENDER-report.md
producer_branch: l4capture/l4c-04-rtp-sender
producer_commit: bff8018
accepted_at_utc: 2026-09-22T10:00:00Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-04-RTP-SENDER-report.md
  - tools/l4capture/include/l4capture/rtp_sender.h
  - tools/l4capture/include/l4capture/rtp_packetizer.h
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - ed6ced854282cc5f6aaaee7b657f91abf6b1958d2e0bb8979dff3f1b4c1f47f0
  - 757339ad278499e06de168c6a9c9ed1f0de0cf16147d945271038c768dcd5922
  - a9ef3deb4007c32e558901a162e7e3101d192bc5988043a68b80e0fc11d727fa
  - d6bbfc7e9bb5c53a768bd487e9324992839661416e7c642d94bd1ec233899138
  - 19e4a50743f84048b5d62bd1ebb9582ddfa8bde6fab24f73e720d0b557ba7ada
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
  breaking_changes: false
  notes: Реализация передачи H.264 по RTP/RTCP (RFC 3550, RFC 6184 FU-A). Неблокирующий UDP loopback 127.0.0.1:5004/5005, маркер AU, каденция RTCP SR/SDES раз в 1.0 с, drop AU при переполнении сокета с немедленным force-IDR.
deployment_status: LOCAL_BUILD_VERIFIED
deployed_environment: local_development
feature_flags:
  l4capture_native_pipeline: enabled
  l4capture_openh264_encoder: enabled
  l4capture_rtp_transport: enabled
contract_payload:
  rtp_transport:
    protocol: RTP/UDP (RFC 3550, RFC 6184)
    destination: 127.0.0.1
    rtp_port: 5004
    rtcp_port: 5005
    payload_type: 96
    clock_rate_hz: 90000
    socket_mode: non-blocking (FIONBIO)
    sndbuf_bytes: 262144
    sio_udp_connreset_disabled: true
  packetization:
    mode: 1 (Non-interleaved)
    max_payload_bytes: 1200
    single_nal_max_bytes: 1200
    fu_a_max_payload_bytes: 1198
    fu_a_indicator_type: 28
    marker_bit_semantics: 1 strictly on last packet of Access Unit
    timestamp_semantics: identical for all packets of the same AU
  rtcp_feedback:
    sender_report_interval_ms: 1000
    sdes_cname_present: true
    bye_on_destroy: true (best effort)
    pli_guard: rate limited <= 1 per 500ms
  resilience_and_safety:
    socket_error_policy: drop remaining AU, increment transport_drops, trigger force_idr
    queue_policy: zero queue accumulation, bounded buffers
    memory_allocation: zero dynamic allocations during streaming
supersedes: []
known_risks:
  - R2: Нарушение доставки/потери — периодический IDR каждые 2.0 с и force_idr при сбоях восстанавливают поток
  - R3: Переполнение сетевых буферов — неблокирующий сокет с мгновенным сбросом AU исключает рост задержки
consumers:
  - L4C-05-AGENT-ADAPTER
  - L4C-06-MILESTONE-LIVE-VERIFY
  - ALL_FOLLOWING
next_prompt_id: L4C-05-AGENT-ADAPTER
```
<!-- HANDOFF:H-L4C-04-v1:END -->
