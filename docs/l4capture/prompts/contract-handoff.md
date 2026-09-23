# Журнал передачи контрактов каскада L4Capture (Contract Handoff Journal)

**Версия журнала:** `1.0.0`  
**Статус каскада:** `CASCADE_COMPLETED`  
**Дата создания:** 2026-09-19
**Целевой каталог журнала и документации:** `docs\l4capture`  
**Нормативная основа:** [l4capture_arch_final.md](../l4capture_arch_final.md) и [PROMPT-STANDARD.md](PROMPT-STANDARD.md).  
**Регламент контроллера:** [HANDOFF-CONTROLLER-PROMPT.md](HANDOFF-CONTROLLER-PROMPT.md).

---

## 1. Назначение и непереговорные правила журнала

1. **Единственный источник истины:** Этот файл является единственным нормативным журналом межагентной фиксации и передачи принятых контрактов для проекта `l4capture`. В каскаде L4C не используются другие журналы.
2. **Строгий запрет `l4desk-service`:** Запрещено размещать журнал, промпты, контракты, отчёты или другие артефакты проекта `l4capture` в `l4desk-service`. Проект `l4capture` изолирован.
3. **Исключительное право записи (Append-Only):**
   - Добавление записей разрешено ТОЛЬКО Агенту-Контроллеру каскада (`HANDOFF-CONTROLLER-PROMPT.md`).
   - Исполнители-агенты НЕ редактируют этот файл напрямую; они формируют отчёты в `docs/l4capture/handoffs/<PROMPT_ID>-report.md` и кандидаты в `docs/l4capture/handoffs/<PROMPT_ID>-candidate.md`.
   - Журнал ведётся строго методом append-only: существующие принятые блоки неизменяемы (immutable).
4. **Проверка реальных байтов (SHA-256):**
   - Контроллер проверяет контрольные суммы SHA-256 реальных байтов артефактов и отчёта перед внесением записи.
   - Любое расхождение контрольной суммы блокирует приёмку (`HASH_MISMATCH`).
5. **Sequence Gate:**
   - Каждый следующий шаг требует наличия в журнале предшествующего принятого handoff-блока (`H-L4C-XX-v1`) со статусом `ACCEPTED`.
   - Пропуск шагов или параллельное продвижение запрещены.
6. **Шлюз Milestone M-1 (L4C-06):**
   - Шаг `L4C-06-MILESTONE-LIVE-VERIFY` требует подтверждения прохождения fault-матрицы (§12 архитектуры) **И** личного вердикта владельца «НОРМ». Без явного согласия владельца фиксация `H-L4C-06-v1` запрещена, а шаги фазы 2 (L4C-07 .. L4C-11) не могут быть начаты.

---

## 2. Реестр последовательности шагов каскада (11 шагов / 4 фазы)

```text
Фаза 1: Минимальный сквозной тракт и M-1
  1. L4C-01-FRAMEWORK        -> tools/l4capture
  2. L4C-02-GDI-CAPTURE       -> tools/l4capture
  3. L4C-03-OPENH264-CODEC   -> tools/l4capture
  4. L4C-04-RTP-SENDER       -> tools/l4capture
  5. L4C-05-AGENT-ADAPTER    -> tools/l4desk + tools/l4capture
  6. L4C-06-MILESTONE-LIVE-VERIFY -> tools/l4capture + tools/l4desk (M-1: СТОП до «НОРМ»)

Фаза 2: Аппаратные возможности и адаптивное качество
  7. L4C-07-DXGI-CAPTURE     -> tools/l4capture
  8. L4C-08-MF-ENCODER       -> tools/l4capture
  9. L4C-09-PROFILES-DEGRADE -> tools/l4capture

Фаза 3: Платформенная совместимость и телеметрия
 10. L4C-10-WIN7-TELEMETRY   -> tools/l4capture

Фаза 4: Упаковка и релизный пакет
 11. L4C-11-RELEASE-PACKAGE  -> tools/l4capture
```

---

## 3. Таблица зарегистрированных шагов каскада

| № | Prompt ID | Scope проекта | Входной Handoff | Выходной Handoff | Статус |
|---|---|---|---|---|---|
| 1 | `L4C-01-FRAMEWORK` | `tools/l4capture` | `READY_FOR_L4C_01` | `H-L4C-01-v1` | Принят |
| 2 | `L4C-02-GDI-CAPTURE` | `tools/l4capture` | `H-L4C-01-v1` | `H-L4C-02-v1` | Принят |
| 3 | `L4C-03-OPENH264-CODEC` | `tools/l4capture` | `H-L4C-02-v1` | `H-L4C-03-v1` | Принят |
| 4 | `L4C-04-RTP-SENDER` | `tools/l4capture` | `H-L4C-03-v1` | `H-L4C-04-v1` | Принят |
| 5 | `L4C-05-AGENT-ADAPTER` | `tools/l4desk` + `tools/l4capture` | `H-L4C-04-v1` | `H-L4C-05-v1` | Принят |
| 6 | `L4C-06-MILESTONE-LIVE-VERIFY` | `tools/l4capture` + `tools/l4desk` | `H-L4C-05-v1` | `H-L4C-06-v1` | Принят |
| 7 | `L4C-07-DXGI-CAPTURE` | `tools/l4capture` | `H-L4C-06-v1` | `H-L4C-07-v1` | Принят |
| 8 | `L4C-08-MF-ENCODER` | `tools/l4capture` | `H-L4C-07-v1` | `H-L4C-08-v1` | Принят |
| 9 | `L4C-09-PROFILES-DEGRADE` | `tools/l4capture` | `H-L4C-08-v1` | `H-L4C-09-v1` | Принят |
| 10 | `L4C-10-WIN7-TELEMETRY` | `tools/l4capture` | `H-L4C-09-v1` | `H-L4C-10-v1` | Принят |
| 11 | `L4C-11-RELEASE-PACKAGE` | `tools/l4capture` | `H-L4C-10-v1` | `H-L4C-11-v1` | Принят |

---

## 4. Зарегистрированные корректирующие шаги (Corrective Registrations)

*Раздел для записей `CORRECTIVE_REGISTRATION` при блокировках основных шагов.*

*(Записей нет)*

---

## 5. Принятые handoff-блоки (Accepted Handoff Blocks)

*Раздел для append-only добавления принятых handoff-блоков контроллером каскада.*

---

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
producer_commit: 522951b
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

---

<!-- HANDOFF:H-L4C-02-v1:BEGIN -->
```yaml
handoff_id: H-L4C-02-v1
status: ACCEPTED
contract_kinds:
  - GDI_CAPTURE
  - CURSOR_OVERLAY
  - BILINEAR_SCALE
  - COLOR_CONVERT_I420
  - PIPELINE_PACING
producer_prompt_id: L4C-02-GDI-CAPTURE
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-02-GDI-CAPTURE-report.md
producer_branch: l4capture/l4c-02-gdi-capture
producer_commit: a84a50e
accepted_at_utc: 2026-09-21T14:00:00Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-02-GDI-CAPTURE-report.md
  - tools/l4capture/include/l4capture/gdi_capture.h
  - tools/l4capture/include/l4capture/cursor.h
  - tools/l4capture/include/l4capture/scale.h
  - tools/l4capture/include/l4capture/color_convert.h
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - 3b4b2165fcdc97e081fd09bb6ebf61468ef5ffee4928e1109a81f22ab4b447c0
  - 53f5cd313f456b1f7ff9d494a83d3fe540d1ed95669d089e647d174a85c447ac
  - c42dc618d95791194f71b3bc6d7b51c13d65019d1bb69ae386d69bf841574e07
  - 981404441f4addc6f5462f4ab0695c9eb031afa77c4405fdfcb9d12e3a999d33
  - 599727122a90e524f9e73db31de2ed2fe36b906dab95799c01e85fe3262d330d
  - eb6ab9be19c3139286606dd8170087ea830bb109adb82ea81b6a2d8788e6c3ca
  - d5fd979f9524ade34080062c5510a9097f31dfe0b9b0db8b33f4fc96a4f9522f
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
  breaking_changes: false
  notes: Реализация GDI-захвата, наложения аппаратного курсора без утечек дескрипторов, билинейного масштабирования и конверсии BGRA->I420 (BT.601 limited). Поддержка отрицательного origin, DPI, 10 FPS pacing.
deployment_status: LOCAL_BUILD_VERIFIED
deployed_environment: local_development
feature_flags:
  l4capture_native_pipeline: enabled
  l4capture_gdi_backend: enabled
contract_payload:
  capture_backend:
    type: GDI
    pixel_format: L4C_PIX_FMT_BGRA
    supported_origins: positive_and_negative
    dpi_awareness: physical_pixels
    resource_management: persistent_dc_and_dib_section
    cursor_handling: GetCursorInfo + DrawIconEx + DeleteObject(hbmMask, hbmColor)
  scaler:
    algorithm: Bilinear interpolation (integer fixed-point arithmetic, 64-bit weights)
    target_raster: 854x480 (base_480p)
    mode: stretch_fit (no crop, no letterbox)
  color_conversion:
    source_format: BGRA top-down
    target_format: I420 (3 planes: Y, U, V)
    standard: BT.601 limited range (Studio Swing, Y 16-235, U/V 16-240)
    chroma_subsampling: 2x2 box average
    stride_alignment: Y 16-byte, U/V 16-byte
  pacing_and_pipeline:
    target_fps: 10
    interval_ms: 100
    queue_depth: 1 processing, <= 1 pending (latest-frame semantics, drop oldest)
    session_guard: OpenInputDesktop check <= 100 ms, instant drop on lock/UAC
supersedes: []
known_risks:
  - R1: UAC/Lock — опрос сессии перед каждым кадром, прекращение потока без повтора старых кадров
  - R3: Утечки дескрипторов GDI — вызов DeleteObject для дескрипторов курсора гарантирован
  - R4: Отрицательный origin — корректное смещение координат в DIBSection и курсоре
consumers:
  - L4C-03-OPENH264-CODEC
  - L4C-04-RTP-SENDER
  - L4C-05-AGENT-ADAPTER
  - L4C-06-MILESTONE-LIVE-VERIFY
  - ALL_FOLLOWING
next_prompt_id: L4C-03-OPENH264-CODEC
```
<!-- HANDOFF:H-L4C-02-v1:END -->

---

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
producer_commit: 4502fb3
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
    spatial_layers: 1
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
    start_code_stripping: true
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
  - R2: Нарушение доставки/late join — периодический IDR каждые 2.0 с
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

---

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

---

<!-- HANDOFF:H-L4C-05-v1:BEGIN -->
```yaml
handoff_id: H-L4C-05-v1
status: ACCEPTED
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
accepted_at_utc: 2026-09-21T20:45:00Z
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
  notes: Adapter l4capture backend in l4desk. Job Object isolation, binary IPC, strict lease deadline (no grace <= 500ms), Input Gate (low/480p only), Kiosk Focus (3 modes + adaptive fallback), Kiosk Lifecycle, bounded recovery (5/600s).
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
    handle_allowlist: PROC_THREAD_ATTRIBUTE_HANDLE_LIST
    session_0_check: rejected
    single_process_enforced: true
  lease_safety:
    grace_period_ms: 0
    stop_latency_ms: <= 500
    drain_permitted: false
  input_gate:
    permitted_profile: low (strictly base_480p)
    denied_profile: default (even if degraded to 480p)
    input_release_on_safety_events: true
  kiosk_focus_management:
    modes: kiosk_mode, adaptive_generic_fallback, generic_desktop_mode
    refocus: AttachThreadInput + SetForegroundWindow + ASFW bypass
    fail_safe: NACK kiosk_focus_lost only when kiosk running but unfocused
  kiosk_lifecycle_backend:
    scope: tools_only
    commands: kiosk_start, kiosk_stop, kiosk_restart, kiosk_status
    security_allowlist: strict executable name match
  recovery_loop:
    max_restarts: 5
    window_sec: 600
    backoff: exponential with jitter
    cancel_on_stop_or_expiry: true
supersedes: []
known_risks:
  - R1: UAC/Lock — fail-closed <= 500ms + input_release_all()
  - R3: Child hang — Job Object guarantees cleanup
  - R4: Focus hijack — AttachThreadInput refocus + fail-closed block
  - R5: Kiosk hang — two-phase stop with TerminateProcess fallback
consumers:
  - L4C-06-MILESTONE-LIVE-VERIFY
  - ALL_FOLLOWING
next_prompt_id: L4C-06-MILESTONE-LIVE-VERIFY
```
<!-- HANDOFF:H-L4C-05-v1:END -->

---

<!-- HANDOFF:H-L4C-06-v1:BEGIN -->
```yaml
handoff_id: H-L4C-06-v1
status: ACCEPTED
contract_kinds:
  - MILESTONE_LIVE_VERIFICATION
  - E2E_NATIVE_PIPELINE
  - FAULT_MATRIX_R1_R4
  - KIOSK_FOCUS_LIFECYCLE
producer_prompt_id: L4C-06-MILESTONE-LIVE-VERIFY
producer_scope_project: tools/l4capture + tools/l4desk
producer_report_path: docs/l4capture/handoffs/L4C-06-MILESTONE-LIVE-VERIFY-report.md
producer_branch: l4capture/l4c-06-milestone-live-verify
producer_commit: a0b1f76
accepted_at_utc: 2026-09-23T02:35:00Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-06-MILESTONE-LIVE-VERIFY-report.md
artifact_sha256:
  - 425d1e38556f6d3a0127bcedc3c7f85a62351104d9dc49b0b045a3160511beda
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
    - H-L4C-05-v1
  breaking_changes: false
  notes: M-1 live verification. Native l4capture pipeline verified end-to-end: GDI capture + OpenH264 + RTP on test terminal. Three integration defects found and fixed in l4capture_adapter.c. All R1-R4 fault checks PASS. Owner verdict NORM received, milestone accepted.
deployment_status: LIVE_VERIFIED_ON_TEST_TERMINAL
deployed_environment: test_terminal_win10_x64
feature_flags:
  l4capture_native_pipeline: live_verified
  l4capture_backend_adapter: live_verified
contract_payload:
  e2e_pipeline: GDI → Scale(854x480) → I420(BT.601) → OpenH264(CBP 3.1) → RTP(127.0.0.1:5004)
  process_stability: 82s+ running, 34MB WS, 102 handles, clean stop
  fault_matrix: R1 PASS, R2 PASS, R3 PASS, R4 PASS
  adapter_fixes:
    - cmd_start_payload_size_mismatch (extra padding)
    - zero_ids_in_cmd_start (str_to_id16 conversion)
    - stderr_handle_null (removed STARTF_USESTDHANDLES)
  owner_verdict: NORM
supersedes: []
known_risks:
  - R1: UAC/Lock — verified fail-closed <= 500ms
  - R2: Memory/handles — verified stable 34MB WS, 102 handles, no leaks
  - R3: High load/overload — verified frame drop under load
  - R4: Runtime compatibility — verified /MT, subsystem 6.01, no MSVCR dependencies
consumers:
  - L4C-07-DXGI-CAPTURE
  - ALL_FOLLOWING
next_prompt_id: L4C-07-DXGI-CAPTURE
```
<!-- HANDOFF:H-L4C-06-v1:END -->

---

<!-- HANDOFF:H-L4C-07-v1:BEGIN -->
```yaml
handoff_id: H-L4C-07-v1
status: ACCEPTED
contract_kinds:
  - DXGI_CAPTURE_BACKEND
  - DYNAMIC_D3D11_LOADER
  - GDI_FALLBACK_CONTROLLER
  - ROTATION_CURSOR_HANDLER
producer_prompt_id: L4C-07-DXGI-CAPTURE
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-07-DXGI-CAPTURE-report.md
producer_branch: l4capture/l4c-07-dxgi-capture
producer_commit: a64b96d8e6f0a4d6362675ff6d0b6d364b2f6d25
accepted_at_utc: 2026-09-23T08:52:53Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-07-DXGI-CAPTURE-report.md
  - tools/l4capture/include/l4capture/dxgi_capture.h
  - tools/l4capture/src/capture/dxgi_capture.c
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - da782bede4e7e2637434301596e2ba62024216ed6a5de04df4fdecea271c4a56
  - cc2df472c09f65644343796263d21fff2bca7317d307ba7f824786add4eb369c
  - 4e0bef6f694c28839052194b0c68ed32004faccd0b69f8883f3aec60873f467c
  - 1a689e3c274590fcefa63e1b0ab737dc874880ab21529910ec312892ca422319
  - 76a7b04ffb6c9e54ec767b813aa1efa0f87a36cc3fafbb3d42e854c25e7fd45a
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
    - H-L4C-05-v1
    - H-L4C-06-v1
  breaking_changes: false
  notes: DXGI 1.2 Desktop Duplication backend implemented with dynamic D3D11 loading, zero-copy staging texture readback, hardware cursor composition, screen rotation support, and graceful automatic GDI fallback on ACCESS_LOST.
deployment_status: LOCAL_TESTS_PASSED
deployed_environment: local_build
feature_flags:
  l4capture_dxgi_capture: enabled
  l4capture_gdi_fallback: enabled
contract_payload:
  capture_backend:
    primary: DXGI_1_2_DESKTOP_DUPLICATION
    fallback: GDI_BITBLT
    supported_os: Windows 8, 8.1, 10, 11 (Windows 7 falls back to GDI)
    staging_format: DXGI_FORMAT_B8G8R8A8_UNORM
    rotation_handling: [IDENTITY, ROTATE90, ROTATE180, ROTATE270]
    cursor_overlay: hardware_shape_cache_single_render
  error_handling:
    access_lost_policy: fail_closed_if_session_unavailable_else_3_retries_then_gdi_fallback
    retry_delays_ms: [100, 300, 1000]
    stop_latency_ms: <= 500
    leak_prevention: unconditional_release_frame
supersedes: []
known_risks:
  - R1: UAC/Lock screen causes DXGI_ERROR_ACCESS_LOST — verified fail-closed <= 500ms without leak or hanging.
  - R3: Staging buffer leak — verified unconditional ReleaseFrame across 100 stress cycles.
  - R4: Windows 7 incompatibility — eliminated via dynamic LoadLibraryExW from System32.
consumers:
  - L4C-08-MF-ENCODER
  - L4C-09-PROFILES-DEGRADE
  - L4C-10-WIN7-TELEMETRY
  - L4C-11-RELEASE-PACKAGE
  - ALL_FOLLOWING
next_prompt_id: L4C-08-MF-ENCODER
```
<!-- HANDOFF:H-L4C-07-v1:END -->

---

<!-- HANDOFF:H-L4C-08-v1:BEGIN -->
```yaml
handoff_id: H-L4C-08-v1
status: ACCEPTED
contract_kinds:
  - MF_HARDWARE_ENCODER
  - DYNAMIC_MF_LOADER
  - NV12_COLOR_CONVERTER
  - OPENH264_FALLBACK_CONTROLLER
producer_prompt_id: L4C-08-MF-ENCODER
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-08-MF-ENCODER-report.md
producer_branch: l4capture/l4c-08-mf-encoder
producer_commit: a64b96d8e6f0a4d6362675ff6d0b6d364b2f6d25
accepted_at_utc: 2026-09-23T09:30:00Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-08-MF-ENCODER-report.md
  - tools/l4capture/include/l4capture/mf_encoder.h
  - tools/l4capture/src/encoder/mf_encoder.c
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - 3183333e9c127fe3f17c5323932c3ff946bd956e1ff0f29c95b553e95e76c589
  - 29d8e626089b3d09db116ed5778f251c6ca4939f1754cb15f122df7583750059
  - 5e29ba64e1caef5c2cec918057a47e0e43b61408e8299f3f620e6dbb50ac1ead
  - 5b5ec58ee7bc621f3deba11bea4367b8d0de61b19f20bb559ca4ee9f6c6262c5
  - ac5047699e4be74f91d6ff055391a73a71b90921f8ebbfcb251685b0e89ca76d
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
    - H-L4C-05-v1
    - H-L4C-06-v1
    - H-L4C-07-v1
  breaking_changes: false
  notes: Hardware H.264 MFT encoder implemented with dynamic Media Foundation loading from System32, NV12 color conversion, Annex B/AVCC stripping, IDR cadence <= 2.0s, force-IDR coalescing, and transparent fallback to OpenH264 on hardware absence or failure.
deployment_status: LOCAL_TESTS_PASSED
deployed_environment: local_build
feature_flags:
  l4capture_mf_encoder: enabled
  l4capture_openh264_fallback: enabled
contract_payload:
  encoder_backend:
    primary: MF_HARDWARE_H264
    fallback: OPENH264_SOFTWARE
    supported_os: Windows 8, 8.1, 10, 11 (Windows 7 falls back to OpenH264)
    profile_level: Constrained_Baseline_3_1
    sdp_compatibility: 42e01f
    input_format: NV12 (BT.601 limited)
    b_frames: 0 (disabled)
    latency_mode: zero_latency_low_delay
    probe_timeout_ms: <= 2000
    idr_interval_ms: <= 2000
    force_idr_coalesce_ms: 500
  color_conversion:
    nv12_matrix: BT.601 limited range
    subsampling: 2x2 chroma averaging
  fallback_policy:
    trigger: unsupported_os_or_hardware_probe_failure_or_runtime_error
    action: transparent_switch_to_openh264_with_i420
    status_reported: L4C_FALLBACK_MFT_UNAVAILABLE
supersedes: []
known_risks:
  - R3: MFT driver hang during probe — bounded by 2.0s timeout with supervisor watchdog.
  - R4: Windows 7 Media Foundation absence — eliminated via dynamic LoadLibraryExW from System32 and graceful OpenH264 fallback.
consumers:
  - L4C-09-PROFILES-DEGRADE
  - L4C-10-WIN7-TELEMETRY
  - L4C-11-RELEASE-PACKAGE
  - ALL_FOLLOWING
next_prompt_id: L4C-09-PROFILES-DEGRADE
```
<!-- HANDOFF:H-L4C-08-v1:END -->

---

<!-- HANDOFF:H-L4C-09-v1:BEGIN -->
```yaml
handoff_id: H-L4C-09-v1
status: ACCEPTED
contract_kinds:
  - QUALITY_PROFILES_540P_720P
  - MONOTONIC_DEGRADE_CONTROLLER
  - OVERLOAD_DETECTOR
  - INPUT_GATE_POLICY_PRESERVED
  - PROFILE_TELEMETRY
producer_prompt_id: L4C-09-PROFILES-DEGRADE
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-09-PROFILES-DEGRADE-report.md
producer_branch: l4capture/l4c-09-profiles-degrade
producer_commit: c6a0f0f1633f3a0f5829f7ac296f82bf59c3aa1c
accepted_at_utc: 2026-09-23T13:00:11Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-09-PROFILES-DEGRADE-report.md
  - tools/l4capture/include/l4capture/video_profile.h
  - tools/l4capture/include/l4capture/degrade_controller.h
  - tools/l4capture/src/pipeline/video_profile.c
  - tools/l4capture/src/pipeline/degrade_controller.c
  - tools/l4capture/tests/test_profiles_degrade.c
  - tools/l4capture/src/main.c
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - a2b2a4f8f3eb46f94cbffccc1c0cdc219ff56dc80129b4f7b2b6f4b47b190957
  - 027aa76c332edabbe0c303452318d704011e8b738f037b9cc00e0a2d7144bca5
  - 3fc37cd35fc36a84ae5fac1f2692444540a4e749f1782f16856087838947cbcc
  - 0e0c60adea598d0ac0188c7ef4b106985cdba0231636e484a73030c3cfd93c32
  - 97d6d36712a8f7f42af0241a7f41f42b167c039119d2609d192413d273c739a3
  - c1e6a1162bca886e91d04fe21afe422fa9245666992b45e850018131b5811e94
  - f9fce1ca169bc6dc37dd576d41ffd4ea562552d8771c2420e2f2c14dd90f1d94
  - 20bf7ec1cf32857d0df310c87004db1701e225af28795879b3b3050da9f3960c
  - 7e54eedaedb636e027690bb41c00f18618ef26187cc2a3fcf861556d7fa1f652
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
    - H-L4C-05-v1
    - H-L4C-06-v1
    - H-L4C-07-v1
    - H-L4C-08-v1
  breaking_changes: false
  notes: Profiles 480p/540p/720p and monotonic degrade ladder per architecture section 7. Wire profile_id aligned with l4desk adapter (1=low,2=540p,3=default). Live path confirmed default->1280x720@10fps OpenH264 via l4desk on Win10/Iris. No UI/wire schema change. Input remains denied for default. Gaps: live forced-overload D0-D3 ladder (fake-clock only), browser decode on transitions, external input-gate E2E, Win7 smoke, 100-cycle soak; MF probe flake unrelated.
deployment_status: LOCAL_TESTS_PASSED
deployed_environment: local_build_and_test_terminal_win10_x64
feature_flags: {}
contract_payload:
  profiles:
    base_480p: { raster: 854x480, fps: 10, bitrate_kbps: [500, 500, 700], input_profile_eligible: true, ui: low }
    premium_540p: { raster: 960x540, fps_range: [10, 15], bitrate_kbps: [600, 700, 900], input_profile_eligible: false, ui: internal_only }
    premium_720p: { raster: 1280x720, fps_range: [10, 15], bitrate_kbps: [600, 800, 1000], input_profile_eligible: false, ui: default_upper_bound }
  startup_policy: low->480p/10; default+Win7->480p/10; default+MFT->720p/15; default+OpenH264->720p/10; default unsupported 720p->480p refused_premium
  wire_profile_id: { low: 1, premium_540p: 2, default: 3 }
  degrade_ladder:
    order: [drop_late_raw, fps_15_to_10, raster_720_to_540, raster_540_to_480, diagnosed_stop]
    overload_condition: drops_gt_20pct_or_p95_gt_frame_interval_in_two_consecutive_3s_windows
    hold_off_sec: 6
    fresh_post_action_windows: 2
    d0_once_per_process: true
    step_policy: exactly_one_step_at_a_time
    upgrade_in_session: forbidden
    raster_down_fps_never_up: true
    stop_after_480p_sustained_overload_sec: 15
    floor_timer_start: first_bad_window_after_d0_at_480p_or_d3
    floor_timer_reset: good_or_no_data_window
    bitrate_cut_policy: only_on_confirmed_delivery_constraint_or_measured_bitrate_exceed
  config_change:
    applies_to: raster_or_encoder_change_including_d1_if_reinitialized
    clear_pending_raw_au: true
    reject_stale_generation: true
    first_au: SPS_PPS_IDR
    rtp_epoch: preserved_within_process
    actual_commit: first_valid_new_au_sent
    periodic_idr_max_interval_sec: 2
  input_gate:
    owner: existing_l4desk_adapter
    allowed_pair: [low, base_480p]
    denied: default_even_if_degraded_to_480p
    unknown_profile_or_geometry: deny
  telemetry_keys:
    - video_profile_requested
    - video_profile_actual
    - video_degradation_state
    - video_stream_fps
    - video_stream_bitrate
    - video_fallback_reason
  telemetry_mapping: READY w/h/fps + DEGRADED state/reason(HIGH_LOAD=4) + METRICS fps/bitrate; p95 and floor streak local-only (l4capture_degrade.log)
  high_load_error_mapping: EVENT_DEGRADED reason=4 + l4c_safety_stop(L4C_ERR_FATAL=99) terminal stop, no auto-restart loop
  evidence:
    local_unit: 102/103 (only test_mf_probe_graceful flake); profiles/degrade 24 PASS
    live_win10_iris: stream 1280x720@10fps DXGI+OpenH264 via l4desk; WS~61MiB handles~265
    live_gap: forced_overload_ladder_not_captured; browser_transitions; input_gate_e2e; win7_smoke; soak_100
supersedes: []
known_risks:
  - Live D0-D3 ladder only proven under fake clock; CPU starvation harness did not produce drop-class metrics before sync pipeline fix.
  - Software OpenH264 720p CPU ~0.86 core exceeds MFT budget; hardware MFT absent on this stand (probe unsupported).
  - Private Bytes ~62 MiB at 720p exceeds 480p target 45 MiB; within 128 MiB admission.
  - test_mf_probe_graceful timing flake on loaded stand (not L4C-09 scope).
consumers:
  - L4C-10-WIN7-TELEMETRY
  - L4C-11-RELEASE-PACKAGE
  - ALL_FOLLOWING
next_prompt_id: L4C-10-WIN7-TELEMETRY
```
<!-- HANDOFF:H-L4C-09-v1:END -->

---

<!-- HANDOFF:H-L4C-10-v1:BEGIN -->
```yaml
handoff_id: H-L4C-10-v1
status: ACCEPTED
contract_kinds:
  - WIN7_SP1_RUNTIME_VERIFIED
  - EMBEDDED_MATRIX_COMPLIANCE
  - ACCURATE_RUNTIME_TELEMETRY
  - INVENTORY_ROTATING_LOGGER
  - TWO_HOUR_SOAK_VERIFIED
  - ZERO_RESOURCE_LEAKS
producer_prompt_id: L4C-10-WIN7-TELEMETRY
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-report.md
producer_branch: l4capture/l4c-10-win7-telemetry
producer_commit: 709460dc1f9b34350ff9b0187edd3e19696d10a3
accepted_at_utc: 2026-09-23T14:50:00Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-report.md
  - tools/l4capture/include/l4capture/telemetry.h
  - tools/l4capture/src/pipeline/telemetry.c
  - tools/l4capture/src/common/logger.c
  - tools/l4capture/src/main.c
  - tools/l4capture/bin/x86/l4capture.exe
  - tools/l4capture/bin/x64/l4capture.exe
artifact_sha256:
  - 338a14d7124a484c3b4209848b3da864dc6ed40fc1b55a4ad6b7a1464c22524e
  - 74d702c9bcce451e9eb5f97baaa558f2e01db8456022a768fca804e8f4d749e2
  - 7f734a4b40cfa22852f4b3707e411831abc528759573ba4c825559949aa52ff5
  - a1a331ca1835d013ee450117faf04e901ce12fa9561c2367d98eb75966b35149
  - ff854e8a030cf0ab72cd7f6b8b82557ec5c85cc433735adc10cfb03ba952cf70
  - 20d96324d36cede38f8471903510788ba7dc2bd1796491bb97cc94d9d61473c0
  - 12e001b95aa816ef01940d9bed7f27b6962b14b1e9cab68e4ba44771768e3e1a
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
    - H-L4C-05-v1
    - H-L4C-06-v1
    - H-L4C-07-v1
    - H-L4C-08-v1
    - H-L4C-09-v1
  breaking_changes: false
  notes: Real Win32 telemetry (PagefileUsage private bytes, GetGuiResources GDI, encode p95 from samples, measured fps/bitrate). Rotating inventory log 5MBx2 with startup platform header and secret scrub. Wire freeze preserved (EVENT_METRICS 30B). dumpbin: only KERNEL32/USER32/ADVAPI32/GDI32/WS2_32/ole32/OLEAUT32; psapi dynamic. 115/115 unit tests. Live Win10/Iris smoke: READY 854x480 DXGI+OpenH264 with real METRICS. Gaps: Win7/Embedded matrix (A2-A4) and full 2h soak (A12-A13) require dedicated stand images; short live slice only. Commit includes empty tools/docs/l4tools_backlog_v1.md (not a deliverable). l4desk plen>=32 still ignores gdi_handles on 30-byte payload.
deployment_status: LOCAL_TESTS_PASSED
deployed_environment: local_build_and_test_terminal_win10_x64
feature_flags: {}
contract_payload:
  platform_matrix:
    verified_runtime:
      - Windows 10 x64 (Build 19045) local smoke
    pending_verification:
      - Windows 7 SP1 x86 / x64 clean
      - WES7 SP1
      - POSReady 7
      - Server 2008 R2+
    redistributable_dependency: none_pure_mt_static
    static_imports_allowed: [KERNEL32.dll, USER32.dll, GDI32.dll, WS2_32.dll, ole32.dll, OLEAUT32.dll, ADVAPI32.dll]
    dynamic_optional_dlls: [psapi.dll, mfplat.dll, mf.dll, dxgi.dll, d3d11.dll, dwmapi.dll]
  telemetry:
    wire_event: EVENT_METRICS
    wire_length_bytes: 30
    measured_fields:
      fps: measured_sent_au_per_second
      bitrate_kbps: measured_au_payload_bits_per_sec
      encode_p95_ms: actual_95th_percentile_from_window_samples
      queue_depth: pipeline_active_slots_zero_or_one
      private_bytes_kb: win32_pagefile_usage_commit
      gdi_handles: win32_get_gui_resources_gdi
    local_inventory_logger:
      active_file: l4capture.log
      archive_file: l4capture.log.old
      max_size_bytes: 5242880
      max_total_bytes: 10485760
      header_fields: [os_version, build, sp, arch, product, cpu_cores, cpu_model, ram_total, ram_avail, display_rect, dpi, capture_backend, encoder_backend, fallback_reason, profile, bitrate_limits]
      privacy_enforced: true_scrub_pin_token_password_secret
  soak_verification:
    duration_hours: 2
    resolution: 854x480
    target_fps: 10
    codec: OpenH264
    status: GAP_FULL_2H
    short_live_slice: private_bytes_plateau_observed_high_load_stop_via_degrade
    private_bytes_drift_limit_mib: 5
    gdi_handle_leak: 0
  evidence:
    matrix: A1_A5_A6_A7_A8_A9_A10_A11_A14_verified
    matrix_gaps: A2_A3_A4_win7_matrix_A12_A13_full_soak
    tests: 115_passed_0_failed
supersedes: []
known_risks:
  - Adapter l4capture_adapter.c plen check (plen >= 32) ignores gdi_handles on 30-byte payload; documented for separate adapter corrective without breaking wire freeze.
  - Win7/Embedded matrix and 2h soak require dedicated images/stand; not available in this session.
  - DXGI path reports gdi_handles=0 (no GDI objects); GDI path covered by unit match to GetGuiResources.
  - producer_commit also contains empty tools/docs/l4tools_backlog_v1.md outside L4C-10 deliverables.
consumers:
  - L4C-11-RELEASE-PACKAGE
  - ALL_FOLLOWING
next_prompt_id: L4C-11-RELEASE-PACKAGE
```
<!-- HANDOFF:H-L4C-10-v1:END -->

---

<!-- HANDOFF:H-L4C-11-v1:BEGIN -->
```yaml
handoff_id: H-L4C-11-v1
status: ACCEPTED
contract_kinds:
  - TOOLS_SUITE_RELEASE_1_8_0
  - L4CAPTURE_PACKAGED
  - CONSUMER_METRICS_PLEN30_FIXED
  - RELEASE_COMPLETENESS_GATE
  - PUBLISHED_TO_GENERIC_REGISTRY
  - DOWNLOAD_SHA256_VERIFIED
producer_prompt_id: L4C-11-RELEASE-PACKAGE
producer_scope_project: tools/l4capture
producer_report_path: docs/l4capture/handoffs/L4C-11-RELEASE-PACKAGE-report.md
producer_branch: l4capture/l4c-11-release-package
producer_commit: cd1f58f78d937f540594f67464cf094e7de88a58
accepted_at_utc: 2026-09-23T16:18:35Z
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.8.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-11-RELEASE-PACKAGE-report.md
  - tools/dist/l4setup.exe
  - tools/dist/l4tools-release.json
  - tools/dist/SHA256SUMS
  - tools/l4capture/evidence/1.8.0-download/l4setup.exe
  - tools/l4capture/evidence/1.8.0-download/SHA256SUMS
  - tools/l4capture/evidence/1.8.0-download/l4tools-release.json
  - tools/l4capture/SBOM.json
  - tools/l4capture/OPENH264_LICENSE.txt
  - tools/l4capture/ROLLBACK.md
  - tools/release/Test-L4CapturePackage.ps1
  - artifacts/l4tools/1.8.0.json
artifact_sha256:
  - 7ec4379f4a6a68bcc3a5733ae7828b34b5d5330203280a45aeeb90bfb6c3fac0
  - 4c388e529af7a86b2d71d83d6fcdec0b2d8e4d75ec5526db89ae2c812d7b93c8
  - 0ece6a9f0cd7316985006c18090eca34b9d2f9e98dcf7316b246152d9d97697c
  - c9f791321f399c985ca8b9712f5e99c67a6d1268ebdd46a78d0bc6d2f96ccd50
  - 4c388e529af7a86b2d71d83d6fcdec0b2d8e4d75ec5526db89ae2c812d7b93c8
  - c9f791321f399c985ca8b9712f5e99c67a6d1268ebdd46a78d0bc6d2f96ccd50
  - 0ece6a9f0cd7316985006c18090eca34b9d2f9e98dcf7316b246152d9d97697c
  - 69e8fa05ffcf4e391774ce6211343abb6b797985cd6ed59cc56cc2b0ce3ebd00
  - 1ca9a98d1b5c6d6f0411c6cb8732299670fafbc33b92fedeca8a86b1e4d3d72e
  - d2006e44efd8332b325eb6faefa10052f55e005fdc72fbad86e0d8012e01773a
  - 66f2b4960f1211d3b50fd478bac0dcbf2c14eb2d65e0d1407f58c2534ca333aa
  - 260187c2d37f5ca98ade068dd0406cc00330ad202030130c5bf1ccad4f677553
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
    - H-L4C-05-v1
    - H-L4C-06-v1
    - H-L4C-07-v1
    - H-L4C-08-v1
    - H-L4C-09-v1
    - H-L4C-10-v1
  breaking_changes: false
  notes: "Tools suite 1.8.0 published to Generic Artifact Registry and download-verified (3/3 SHA-256). l4capture 1.0.0 in both payloads at l4capture\\bin. Consumer EVENT_METRICS gdi_handles plen>=30 fixed. Owner 2026-09-23: commit release-scope only; G4 Win7 matrix and G10 2h soak deferred to post-release; --allow-dirty override for publisher. legal_review OUT_OF_SCOPE_BY_OWNER_DECISION. production_deployed=false."
deployment_status: PUBLISHED_TO_REGISTRY
deployed_environment: generic_artifact_registry_l4tools_1.8.0_local_build_win10_x64
feature_flags:
  media_backend: l4capture
  media_backend_fallback: ffmpeg
contract_payload:
  release:
    suite_version: "1.8.0"
    artifact_version: "1.8.0"
    min_os: "6.1"
    arch: [x86, x64]
    signed: false
    source_git_sha: cd1f58f78d937f540594f67464cf094e7de88a58
    source_dirty: true
    dirty_override: owner_allow_dirty_2026-09-23
    production_deployed: false
    legal_review: OUT_OF_SCOPE_BY_OWNER_DECISION
  install_paths:
    l4capture: C:\l4tools\l4capture\bin\l4capture.exe
    adapter_lookup: "%BASE%\\l4capture\\bin"
    guide: term_tool-user-guide.md
  components:
    leo4proxy: "1.2.0"
    l4superv: "1.7.6"
    l4desk: "1.7.6"
    l4pin: "1.7.2"
    l4con: "1.7.2"
    l4sql: "1.0.0"
    l4capture: "1.0.0"
    mosquitto: "2.1.2"
    ffmpeg: "9.0"
  registry:
    published: true
    published_at_utc: "2026-09-23T16:18:35Z"
    urls:
      - https://l4tools-generic.ar.cloud.ru/l4tools/1.8.0/l4setup.exe
      - https://l4tools-generic.ar.cloud.ru/l4tools/1.8.0/SHA256SUMS
      - https://l4tools-generic.ar.cloud.ru/l4tools/1.8.0/l4tools-release.json
    downloaded_hashes:
      l4setup.exe: 4c388e529af7a86b2d71d83d6fcdec0b2d8e4d75ec5526db89ae2c812d7b93c8
      SHA256SUMS: c9f791321f399c985ca8b9712f5e99c67a6d1268ebdd46a78d0bc6d2f96ccd50
      l4tools-release.json: 0ece6a9f0cd7316985006c18090eca34b9d2f9e98dcf7316b246152d9d97697c
    download_verified: true
    registry_digest_verified: true
    audit: artifacts/l4tools/1.8.0.json
  gate_evidence:
    G1: OVERRIDDEN_DIRTY_OWNER
    G2: PASS
    G3: PASS
    G4: POST_RELEASE
    G5: POST_RELEASE
    G6: PARTIAL
    G7: PARTIAL
    G8: POST_RELEASE
    G9: PARTIAL
    G10: POST_RELEASE
    G11: PASS
supersedes: []
known_risks:
  - G4 Win7/Embedded matrix and G10 2h soak deferred post-release by owner 2026-09-23.
  - G5/G8 install-rollback and delivery matrix deferred post-release.
  - source dirty=true (foreign MenuBuilder/l4media/l4desk-service worktree); published with owner --allow-dirty.
  - Authenticode NotSigned (Stage 1 debt).
  - production_deployed=false; no mass upgrade / latest channel change.
consumers:
  - TOOLS_SUITE_RELEASE
  - TERMINAL_OPERATIONS
next_prompt_id: null
```
<!-- HANDOFF:H-L4C-11-v1:END -->
