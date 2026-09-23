# Реестр промптов каскада L4Capture

Каталог содержит исполняемые изолированные промпты реализации нативного модуля видеозахвата `l4capture.exe` согласно архитектурному релизу `../l4capture_arch_final.md`.

---

## 1. Регламент выполнения и запуска

1. **Последовательный запуск:** Промпты выполняются строго последовательно от шага 1 к шагу 11. Параллельный запуск запрещён.
2. **Изоляция каталогов:**
   - Промпты, документация, контроллер каскада и handoff-журнал: `docs\l4capture\`.
   - **Запрещено использовать `l4desk-service` для размещения файлов проекта `l4capture`.**
   - Код, скрипты и результаты: `tools\l4capture\`; для полного релиза L4C-11 разрешён только дополнительный allowlist §2 его промпта.
3. **Единый журнал передачи контрактов:** Единственным журналом фиксации результатов является `contract-handoff.md` в текущей папке.
4. **Контроллер каскада:** Приёмка результатов шагов и внесение записей в журнал осуществляется через регламент `HANDOFF-CONTROLLER-PROMPT.md`.
5. **Milestone M-1:** Шаг L4C-06 является контрольной вехой. До получения личного вердикта владельца «НОРМ» переход к фазе 2 заблокирован.
6. **Релиз 1.8.0:** L4C-11 выпускает полный tools suite через существующий установщик и публикует его в Artifact Registry. Юридические вопросы исключены из каскада решением владельца; технические проверки, SBOM и лицензии в поставке сохраняются. Подготовка промпта не запускает релиз и не меняет статусы журнала.

---

## 2. Последовательность шагов каскада (11 шагов / 4 фазы)

| № | Шаг / Промпт | Назначение | Scope проекта | Входной Handoff | Выходной Handoff |
|---|---|---|---|---|---|
| 1 | `L4C-01-FRAMEWORK` | Каркас проекта, C-контракты, IPC framing, deadline/safety, тесты | `tools/l4capture` | `READY_FOR_L4C_01` | `H-L4C-01-v1` |
| 2 | `L4C-02-GDI-CAPTURE` | GDI захват экрана и курсора, масштабирование, I420 | `tools/l4capture` | `H-L4C-01-v1` | `H-L4C-02-v1` |
| 3 | `L4C-03-OPENH264-CODEC` | Статический OpenH264 (C API), I420 -> H.264 Baseline 3.1, IDR | `tools/l4capture` | `H-L4C-02-v1` | `H-L4C-03-v1` |
| 4 | `L4C-04-RTP-SENDER` | Конвейер GDI -> OpenH264 -> RTP 1200 B, FU-A, RTCP | `tools/l4capture` | `H-L4C-03-v1` | `H-L4C-04-v1` |
| 5 | `L4C-05-AGENT-ADAPTER` | Адаптер l4desk, Job Object, pipes, lease, диагностика | `tools/l4desk` + `tools/l4capture` | `H-L4C-04-v1` | `H-L4C-05-v1` |
| 6 | `L4C-06-MILESTONE-LIVE-VERIFY` | Сквозная веха M-1: fault-матрица R1–R4, ШЛЮЗ: вердикт владельца «НОРМ» | `tools/l4capture` + `tools/l4desk` | `H-L4C-05-v1` | `H-L4C-06-v1` |
| 7 | `L4C-07-DXGI-CAPTURE` | DXGI Desktop Duplication, staging, rotation, GDI fallback | `tools/l4capture` | `H-L4C-06-v1` | `H-L4C-07-v1` |
| 8 | `L4C-08-MF-ENCODER` | Media Foundation hardware MFT probe, OpenH264 fallback | `tools/l4capture` | `H-L4C-07-v1` | `H-L4C-08-v1` |
| 9 | `L4C-09-PROFILES-DEGRADE` | Профили 540p/720p, алгоритм деградации качества при перегрузке | `tools/l4capture` | `H-L4C-08-v1` | `H-L4C-09-v1` |
| 10 | `L4C-10-WIN7-TELEMETRY` | Расширенная матрица Win7/Embedded, инвентарная телеметрия | `tools/l4capture` | `H-L4C-09-v1` | `H-L4C-10-v1` |
| 11 | [L4C-11-RELEASE-PACKAGE](L4C-11-RELEASE-PACKAGE.md) | Полный tools suite **1.8.0**, x86/x64 payload, установка/откат, SBOM, публикация и проверка скачивания | `tools/l4capture` + релизный allowlist §2 | `H-L4C-10-v1` | `H-L4C-11-v1` |
