# Журнал передачи контрактов каскада L4Capture (Contract Handoff Journal)

**Версия журнала:** `1.0.0`  
**Статус каскада:** `READY_FOR_L4C_06`  
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
| 6 | `L4C-06-MILESTONE-LIVE-VERIFY` | `tools/l4capture` + `tools/l4desk` | `H-L4C-05-v1` | `H-L4C-06-v1` | BLOCKED_OWNER_APPROVAL (awaiting НОРМ) |
| 7 | `L4C-07-DXGI-CAPTURE` | `tools/l4capture` | `H-L4C-06-v1` | `H-L4C-07-v1` | Заблокирован sequence gate |
| 8 | `L4C-08-MF-ENCODER` | `tools/l4capture` | `H-L4C-07-v1` | `H-L4C-08-v1` | Заблокирован sequence gate |
| 9 | `L4C-09-PROFILES-DEGRADE` | `tools/l4capture` | `H-L4C-08-v1` | `H-L4C-09-v1` | Заблокирован sequence gate |
| 10 | `L4C-10-WIN7-TELEMETRY` | `tools/l4capture` | `H-L4C-09-v1` | `H-L4C-10-v1` | Заблокирован sequence gate |
| 11 | `L4C-11-RELEASE-PACKAGE` | `tools/l4capture` | `H-L4C-10-v1` | `H-L4C-11-v1` | Заблокирован sequence gate |

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
status: BLOCKED_OWNER_APPROVAL
contract_kinds:
  - MILESTONE_LIVE_VERIFICATION
  - E2E_NATIVE_PIPELINE
  - FAULT_MATRIX_R1_R4
  - KIOSK_FOCUS_LIFECYCLE
producer_prompt_id: L4C-06-MILESTONE-LIVE-VERIFY
producer_scope_project: tools/l4capture + tools/l4desk
producer_report_path: docs/l4capture/handoffs/L4C-06-MILESTONE-LIVE-VERIFY-report.md
producer_branch: l4capture/l4c-06-milestone-live-verify
producer_commit: ad5c4dd
accepted_at_utc: null
contract_version: 1.0.0
schema_revision: N/A
artifact_version: 1.0.0
artifact_paths:
  - docs/l4capture/handoffs/L4C-06-MILESTONE-LIVE-VERIFY-report.md
artifact_sha256:
  - 89ba30db8979bc6c0a0c46ac47f624f5b04ffe1cfc272f3318558c5af89dc489
compatibility:
  backward_compatible_with:
    - H-L4C-01-v1
    - H-L4C-02-v1
    - H-L4C-03-v1
    - H-L4C-04-v1
    - H-L4C-05-v1
  breaking_changes: false
  notes: M-1 live verification. l4capture native pipeline verified on test terminal (Win10 x64). GDI+OpenH264+RTP, 82s stable. Three adapter integration defects fixed. All R1-R4 PASS. Awaiting owner НОРМ.
deployment_status: LIVE_VERIFIED_ON_TEST_TERMINAL
deployed_environment: test_terminal_win10_x64
feature_flags:
  l4capture_native_pipeline: live_verified
  l4capture_backend_adapter: live_verified
contract_payload:
  e2e_pipeline: GDI → Scale(854x480) → I420(BT.601) → OpenH264(CBP 3.1) → RTP(127.0.0.1:5004)
  process_stability: 82s+ running, 34MB WS, 102 handles, clean stop
  fault_matrix: R1 PASS, R2 PASS, R3 PASS, R4 PASS
  owner_verdict: PENDING
supersedes: []
known_risks:
  - R1-R4 all verified PASS on test terminal
consumers:
  - L4C-07-DXGI-CAPTURE
  - ALL_FOLLOWING
next_prompt_id: L4C-07-DXGI-CAPTURE
```
<!-- HANDOFF:H-L4C-06-v1:END -->
