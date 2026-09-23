# L4C-09-PROFILES-DEGRADE — Отчёт исполнителя

**Prompt ID:** `L4C-09-PROFILES-DEGRADE`  
**Фактический статус:** `BLOCKED_TESTS`  
**Ветка:** `l4capture/l4c-09-profiles-degrade`  
**HEAD на момент отчёта:** `a64b96d8e6f0a4d6362675ff6d0b6d364b2f6d25` (изменения **не закоммичены**; commit требует отдельного разрешения)  
**Дата:** 2026-09-23  

---

## 0. Резюме

Реализация **профилей 480p/540p/720p**, **контроллера монотонной деградации** (§7) и их интеграции в `tools/l4capture` выполнена. Локальная сборка x86/x64 и полный `test_runner` — **103 passed, 0 failed**.

Статус **`BLOCKED_TESTS`**: обязательные стендовые доказательства §6.1 (реальный hardware MFT, browser decode на переходах, внешний input-gate в обход UI, Win7 SP1 runtime smoke с новыми бинарниками, 100-цикловый memory baseline) **на текущем стенде не воспроизводимы**. Unit-тесты не выдаются за их замену. Успешный candidate `H-L4C-09-v1` **не формируется**.

---

## 1. Git baseline и Contract Gate

### 1.1. Baseline

| Параметр | Значение |
|---|---|
| Ветка до шага | `l4capture/l4c-08-mf-encoder` |
| Ветка реализации | `l4capture/l4c-09-profiles-degrade` (создана по разрешению пользователя) |
| HEAD | `a64b96d8e6f0a4d6362675ff6d0b6d364b2f6d25` |
| Чужие staged/unstaged изменения (MenuBuilder, l4desk-service, l4media, L4C-07/08) | **не тронуты** |

### 1.2. Вход `H-L4C-08-v1` (из принятого блока журнала)

| Путь | Ожидаемый SHA-256 | Фактический | Результат |
|---|---|---|---|
| `docs/l4capture/handoffs/L4C-08-MF-ENCODER-report.md` | `3183333e9c127fe3f17c5323932c3ff946bd956e1ff0f29c95b553e95e76c589` | совпадает | OK |
| `tools/l4capture/include/l4capture/mf_encoder.h` | `29d8e626089b3d09db116ed5778f251c6ca4939f1754cb15f122df7583750059` | совпадает | OK |
| `tools/l4capture/src/encoder/mf_encoder.c` | `5e29ba64e1caef5c2cec918057a47e0e43b61408e8299f3f620e6dbb50ac1ead` | совпадает | OK |
| `tools/l4capture/bin/x86/l4capture.exe` | `5b5ec58ee7bc621f3deba11bea4367b8d0de61b19f20bb559ca4ee9f6c6262c5` | `9dcbeead258ac19a874f2372a067cf9b679f4be10eeac3f8f7b4e418cd88d38a` | **MISMATCH** |
| `tools/l4capture/bin/x64/l4capture.exe` | `ac5047699e4be74f91d6ff055391a73a71b90921f8ebbfcb251685b0e89ca76d` | `2431972cfec997be74fcda914c84caea10d340f681fc94678decf6d5aeb3b250` | **MISMATCH** |

**Решение пользователя (gate):** mismatch exe игнорировать, считать входом фактические байты на диске (совпадают с candidate L4C-08). Расхождение зафиксировано здесь; журнал не редактировался.

Дополнительно: заголовок журнала `Статус каскада: READY_FOR_L4C_08` расходится с таблицей шага 9 («Готов к запуску»). Самостоятельная правка журнала запрещена.

### 1.3. IPC / telemetry mapping (producer → serializer → consumer)

| Ключ | Producer | Wire / local | Consumer |
|---|---|---|---|
| `video_profile_requested` | `CMD_START.profile_id` → `l4c_profile_parse_request` | local + start | l4desk adapter (L4C-05) |
| `video_profile_actual` | `EVENT_READY.actual_width/height` | wire `l4c_ready_t` (существующие поля) | l4desk adapter |
| `video_degradation_state` | `EVENT_DEGRADED.degrade_state` | wire `degraded.degrade_state` (существующий u16) | l4desk adapter |
| `video_fallback_reason` / `HIGH_LOAD` | `EVENT_DEGRADED.reason = L4C_HIGH_LOAD (4)` | wire `degraded.reason` | l4desk adapter |
| `video_stream_fps` / `video_stream_bitrate` | `EVENT_METRICS.fps/bitrate_kbps` | wire `l4c_metrics_t` | l4desk adapter |
| processing_p95 / floor-streak / window numerators | degrade_controller state | **local-only** (нет extension-полей v1) | local report |
| input-gate | **не в l4capture** | внешний контракт L4C-05 | `tools/l4desk` |

Wire-схема, `CMD_*`/`EVENT_*`-коды, SDP, RTP PT **не изменялись**. Новых UI-профилей нет.

---

## 2. Состав изменений (только scope шага)

### Создано (`tools/l4capture/`)

| Файл | SHA-256 |
|---|---|
| `include/l4capture/video_profile.h` | `8e7e76885c5fe72b068de4357c9c048c096c67a59200c26a12d8a90273d51274` |
| `include/l4capture/degrade_controller.h` | `3fc37cd35fc36a84ae5fac1f2692444540a4e749f1782f16856087838947cbcc` |
| `src/pipeline/video_profile.c` | `6639d82e469e8964edbeb2aa3b7dc66cf9c38b730731351a2926be1ac0643678` |
| `src/pipeline/degrade_controller.c` | `97d6d36712a8f7f42af0241a7f41f42b167c039119d2609d192413d273c739a3` |
| `tests/test_profiles_degrade.c` | (unit-набор, 24 теста) |

### Изменено (`tools/l4capture/`)

- `src/main.c` — resolve профиля, динамический растр/FPS, окна детектора, apply D0–D3/STOP, reconfigure D2/D3, `HIGH_LOAD`
- `build.cmd`, `test.cmd` — новые translation units
- `tests/test_runner.c` — регистрация тестов profiles/degrade

### Собрано

| Артефакт | SHA-256 |
|---|---|
| `bin/x86/l4capture.exe` | `e40586523c239bfaa70d6f50239822eaa15013def3ce8c97b3787a72bcbd65ff` |
| `bin/x64/l4capture.exe` | `27b6700ef8ede7ae466a19eb28d194aa5c51cd445e2ac364476438888b079e54` |

Чужие изменения (L4C-07/08, MenuBuilder, l4desk-service, l4media) **себе не приписываются**.

---

## 3. Компиляция и тесты (локально)

| Проверка | Команда | Результат |
|---|---|---|
| Сборка x86 `/MT /W4` Win7 subsystem 6.01 | `cmd /c "build.cmd all"` | **OK**, 0 errors, 0 warnings |
| Сборка x64 `/MT /W4` | `cmd /c "build.cmd all"` | **OK**, 0 errors, 0 warnings |
| Test runner (весь набор) | `cmd /c "test.cmd"` (x86) | **103 passed, 0 failed, 103 total** |
| `dumpbin /dependents` x86/x64 | KERNEL32, USER32, GDI32, WS2_32, ole32, OLEAUT32 | без msvcrt/vcruntime/mfplat |
| `dumpbin /imports` x86/x64 | те же system DLL | без статических MF/DXGI |

Новые тесты (24): `test_profile_params_table`, `test_profile_resolve_*`, `test_input_gate_*`, `test_overload_detector_two_windows`, `test_degrade_ladder_monotonic_single_step`, `test_no_oscillation_*`, `test_raster_down_fps_not_up`, `test_config_change_clears_pending_and_first_idr`, `test_stop_on_persistent_overload_at_480p`, `test_bitrate_cut_requires_delivery_or_measured_exceed`, `test_degrade_does_not_break_input_gate`, `test_safety_stop_latency_preserved_under_degrade`, `test_detector_*`, `test_p95_from_samples`, `test_window_boundary_rejects_partial`, `test_holdoff_blocks_second_action`, `test_start_720p_10_skips_d1`, `test_nodata_resets_streaks`, `test_reference_timeline_480p_stop_21s` — **PASS**.

**Эталон fake-clock (§4.2.2):** 720p/15: D0@6s → D1@12s → D2@18s → D3@24s; 480p/10: D0@6s → HIGH_LOAD@21s — подтверждены `test_degrade_ladder_monotonic_single_step` и `test_reference_timeline_480p_stop_21s`.

Каталог tests: чистый контроллер + fake clock, без Sleep/GPU.

---

## 4. Матрица A1–A13

| № | Критерий | Тест / команда | Факт | Evidence |
|---|---|---|---|---|
| A1 | Профили §7.1, fail-closed, 540p internal | `test_profile_params_table`, `test_profile_resolve_*` | PASS | unit |
| A2 | Forced overload D0→D3, detector bounds | `test_degrade_ladder_*`, `test_detector_*` | PASS | unit / fake clock |
| A3 | Монотонность FPS/растр | `test_raster_down_fps_not_up`, `test_no_oscillation_*` | PASS | unit |
| A4 | Один шаг + hold-off 6s, 2 fresh windows | `test_holdoff_blocks_second_action`, ladder | PASS | unit |
| A5 | Анти-осцилляция | `test_no_oscillation_upgrade_forbidden` | PASS | unit |
| A6 | Первый IDR/SPS/PPS, browser decode | `test_config_change_clears_pending_and_first_idr` (partial) | **PARTIAL** | reinit/IDR — unit; **browser decode на переходах — НЕТ стенда** |
| A7 | Внешний input-gate | helper unit PASS | **PARTIAL** | **внешняя попытка ввода в обход UI — НЕТ стенда** |
| A8 | Floor stop 5×3s после D0/D3 | `test_stop_on_persistent_overload_at_480p`, `test_reference_timeline_480p_stop_21s` | PASS | unit |
| A9 | Битрейт-срез policy | `test_bitrate_cut_requires_delivery_or_measured_exceed` | PASS | unit (measured 10s / delivery gate) |
| A10 | Safety/ресурсы | `test_safety_stop_latency_*` (API-level) | **PARTIAL** | **stop latency ≤500 мс во время reinit на живом тракте — НЕТ** |
| A11 | Сборка/тесты/стенды | `build.cmd` / `test.cmd` / `dumpbin` | **PARTIAL** | local PASS; **MFT/browser/Win7 smoke — НЕТ** |
| A12 | Scope | git status | PASS | только `tools/l4capture` + этот отчёт |
| A13 | DETACHED_V1 + commit | — | **BLOCKED** | **изменения не закоммичены**; candidate не выпущен |

---

## 5. Непройденные обязательные проверки §6.1 (владелец / требование)

| # | Проверка | Почему не выполнена | Что нужно |
|---|---|---|---|
| 1 | Forced overload на реальном **hardware MFT** (≥3 раза) | На стенде `l4c_mf_encoder_is_supported() == false` (probe ~1.6–4.3 с, hardware MFT не найден) | Стенд Win8+ с Intel QSV / NVIDIA NVENC / AMD AMF |
| 2 | Browser decode 720→540→480 (framesDecoded, видимый растр, без backlog) | Нет согласованного медиатракта/Janus-стенда в scope шага | Изолированный media-стенд + браузер |
| 3 | Внешний input-gate в обход UI (default запрещён на каждой ступени) | Gate живёт в `tools/l4desk`; запуск вне scope без стенда | Согласованный стенд l4desk + capture |
| 4 | Win7 SP1 x86/x64 runtime smoke **новыми** exe | Нет Win7-образа на этой машине; `/imports` не замена запуску | Win7 SP1 x86/x64 VM/terminal |
| 5 | 100 циклов: baseline handles/Private Bytes, возврат к baseline | Нет harness live-процесса на стенде с принудительной перегрузкой | Стенд + fault harness |
| 6 | Real OpenH264 encode transitions with SPS crop for 540p/720p | Частично покрыто codec unit-тестами на 480p; 540p/720p crop/IDR — только через live encode | Локальный real-codec harness 540p/720p |

**Блокер:** `BLOCKED_TESTS` — без пунктов 1–5 шаг не может быть объявлен завершённым и **успешный `H-L4C-09-v1` не выпускается**.

Дополнительно для A13 требуется **отдельное разрешение на git commit** (сейчас HEAD не содержит изменений шага).

---

## 6. Поведение реализации (кратко)

- **Матрица старта §4.1:** `low`→480p/10; `default`+Win7→480p/10; `default`+MFT→720p/15; `default`+OpenH264→720p/10; `default` без 720p→480p (`refused_premium`). 540p не стартует.
- **Лестница:** D0 (однократно) → D1 (15→10, без reinit) → D2 (720→540, reinit+IDR) → D3 (540→480, reinit+IDR) → 5 bad-окон на 480p → `HIGH_LOAD` terminal stop.
- **Детектор:** неполные 3s-окна, классы raw/encoder/transport порознь, drops строго >20%, `processing_p95 * fps > 1000`, 2 bad-окна, hold-off 6000 мс, no-data сбрасывает streaks.
- **Input helper:** true только (`low`,`base_480p`); авторизация — вне `l4capture`.
- **Монотонность:** FPS/растр не растут; `notify_applied` подавляет повышение FPS.

---

## 7. Итог

| Поле | Значение |
|---|---|
| Статус | **`BLOCKED_TESTS`** |
| Local build / unit | PASS (103/103, x86+x64, imports чистые) |
| Stand evidence §6.1 | **НЕТ** (MFT, browser, input-gate E2E, Win7 smoke, 100 cycles) |
| Candidate `H-L4C-09-v1` | **не выпущен** |
| Следующий шаг владельца | 1) обеспечить стенды §6.1; 2) разрешить commit; 3) повторить §6.1; затем candidate → контроллер |

Журнал не изменялся. L4C-10 не запускается.
