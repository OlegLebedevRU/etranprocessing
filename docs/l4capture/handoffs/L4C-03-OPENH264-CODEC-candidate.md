<!-- HANDOFF:H-L4C-03-v1:BEGIN -->
```yaml
handoff_id: H-L4C-03-v1
status: ACCEPTED
contract_kinds:
  - OPENH264_CODEC
  - I420_ENCODER
  - AVC_BASELINE_3_1
  - NAL_NORMALIZATION
  - IDR_CADENCE
  - ENCODER_ONLY_LINKAGE
producer_prompt_id: L4C-03-OPENH264-CODEC
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-03-OPENH264-CODEC-report.md
producer_branch: l4capture/l4c-03-openh264-codec
producer_commit: 974dc49
accepted_at_utc: 2026-09-21T17:45:00Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-03-OPENH264-CODEC-report.md
  - tools/l4capture/include/l4capture/openh264_encoder.h
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - b84a3b8f130bc1f3ec2953a1fb8d3c53d1d698bb841fcd496c9058d78d1abdb8
  - 8cec838fdb3883a5424829e7a0558679512c320c0b8c9d943fd81f326272e63e
  - 490ae08441742bc0fb497f050df26145e1282f549584a47b37c505f32b59faca
  - 8425d0401df9d0c482430e8b758c54e232d52f62701eb159545e57d703fdfda3
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
  breaking_changes: false
  notes: Реализация программного видеоэнкодера OpenH264 v2.6.0 через C API. Constrained Baseline Level 3.1 (SDP 42e01f), нормализация NAL без Annex B префиксов, каденция IDR (периодическая 2.0 с, force-IDR 500 мс), encoder-only линковка.
deployment_status: LOCAL_BUILD_VERIFIED
deployed_environment: local_development
feature_flags:
  l4capture_native_pipeline: enabled
  l4capture_openh264_encoder: enabled
contract_payload:
  encoder_backend:
    library: OpenH264 v2.6.0 (CVE-2025-27091 fix)
    linkage: static (/MT, encoder-only in production binary)
    c_interface: IEncoderBackend (init, encode, force_idr, release_au, destroy)
    input_format: L4C_PIX_FMT_I420 (planar Y, U, V)
    output_format: AccessUnit (l4c_access_unit_t with l4c_nal_desc_t array)
  h264_profile:
    profile: Constrained Baseline (PRO_BASELINE, profile_idc=66)
    level: Level 3.1 (LEVEL_3_1, level_idc=31)
    sdp_compatibility: 42e01f
    entropy_coding: CAVLC (iEntropyCodingModeFlag=0)
    spatial_layers: 1 (single layer, no SVC)
    b_frames: forbidden
    lookahead: none
  rate_control:
    mode: RC_BITRATE_MODE
    target_raster: 854x480 (base_480p)
    target_fps: 10
    target_bitrate_kbps: 500
    max_bitrate_kbps: 700
    frame_skip: enabled (handled as non-fatal encoder_drops)
  nal_and_au:
    start_code_stripping: true (Annex B stripped, raw NAL payload)
    max_au_size: 2097152 (2 MB cap)
    first_frame_structure: SPS (7) + PPS (8) + IDR (5)
  keyframe_cadence:
    periodic_idr_interval_ms: 2000
    sps_pps_repeat: before each IDR
    force_idr_coalescing_ms: 500
  supply_chain_and_licensing:
    version: v2.6.0
    cve_fixed: CVE-2025-27091
    license: BSD-2-Clause
    avc_patent_notice: Commercial distribution requires explicit AVC patent authorization
supersedes: []
known_risks:
  - R2: Нарушение доставки/late join — периодический IDR каждые 2.0 с обеспечивает восстановление
  - R3: Внутренняя перегрузка — frame skip не крашит процесс, предвыделенные буферы AU (2 МБ)
  - R4: Несовместимость формата — Constrained Baseline Level 3.1 гарантирует декодирование
consumers:
  - L4C-04-RTP-SENDER
  - L4C-05-AGENT-ADAPTER
  - L4C-06-MILESTONE-LIVE-VERIFY
  - ALL_FOLLOWING
next_prompt_id: L4C-04-RTP-SENDER
```
<!-- HANDOFF:H-L4C-03-v1:END -->
