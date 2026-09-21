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
producer_commit: 220d12e
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
