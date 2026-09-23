# L4C-09-PROFILES-DEGRADE — Отчёт исполнителя

**Prompt ID:** `L4C-09-PROFILES-DEGRADE`  
**Статус заявки:** `ACCEPTED` (к готовности к проверке контроллером)  
**Ветка:** `l4capture/l4c-09-profiles-degrade`  
**producer_commit:** `c6a0f0f1633f3a0f5829f7ac296f82bf59c3aa1c`  
**Исходный baseline HEAD:** `a64b96d8e6f0a4d6362675ff6d0b6d364b2f6d25`  
**Дата:** 2026-09-23  

---

## 1. Contract Gate

| Путь | Ожидание (H-L4C-08-v1 журнал) | Факт входа | Результат |
|---|---|---|---|
| `L4C-08-MF-ENCODER-report.md` | `3183333e…` | совпадает | OK |
| `mf_encoder.h` | `29d8e626…` | совпадает | OK |
| `mf_encoder.c` | `5e29ba64…` | совпадает | OK |
| `bin/x86/l4capture.exe` | `5b5ec58e…` | `9dcbeead…` (как в candidate L4C-08) | **MISMATCH** — принято решение пользователя игнорировать |
| `bin/x64/l4capture.exe` | `ac504769…` | `2431972c…` | **MISMATCH** — idem |

Заголовок журнала `READY_FOR_L4C_08` против таблицы шага 9 — зафиксировано, журнал не редактировался.  
Ветка создана по явному разрешению пользователя.

### Wire mapping (producer → IPC → consumer)

| Ключ | Wire | Consumer |
|---|---|---|
| `profile_id` START | **1=low, 2=540p, 3=default** (контракт `l4capture_adapter.c`) | l4capture parse |
| `video_profile_actual` | `EVENT_READY` w/h/fps | l4desk |
| `video_degradation_state` | `EVENT_DEGRADED.degrade_state` | l4desk |
| `HIGH_LOAD` | `EVENT_DEGRADED.reason=4` | l4desk |
| processing p95 / floor streak | **local-only** | `l4capture_degrade.log` |

**Дефект live (fixed):** парсер ожидал `default=2`, адаптер шлёт `default=3` → `code=5` → ffmpeg fallback. Исправлено в `c6a0f0f`.

---

## 2. Изменения scope

**Создано:** `video_profile.h/.c`, `degrade_controller.h/.c`, `tests/test_profiles_degrade.c`, `tests/live_start.c`  
**Изменено:** `src/main.c`, `build.cmd`, `test.cmd`, `tests/test_runner.c`  
**Не тронуто:** MenuBuilder, l4desk, l4media, FRONT/BACK, журнал.

| Артефакт | SHA-256 |
|---|---|
| `include/l4capture/video_profile.h` | `027aa76c332edabbe0c303452318d704011e8b738f037b9cc00e0a2d7144bca5` |
| `include/l4capture/degrade_controller.h` | `3fc37cd35fc36a84ae5fac1f2692444540a4e749f1782f16856087838947cbcc` |
| `src/pipeline/video_profile.c` | `0e0c60adea598d0ac0188c7ef4b106985cdba0231636e484a73030c3cfd93c32` |
| `src/pipeline/degrade_controller.c` | `97d6d36712a8f7f42af0241a7f41f42b167c039119d2609d192413d273c739a3` |
| `bin/x86/l4capture.exe` | `20bf7ec1cf32857d0df310c87004db1701e225af28795879b3b3050da9f3960c` |
| `bin/x64/l4capture.exe` | `7e54eedaedb636e027690bb41c00f18618ef26187cc2a3fcf861556d7fa1f652` |

`bin/` в `.gitignore` — exe не в git, хеши выше.

---

## 3. Локальные проверки

| Команда | Результат |
|---|---|
| `build.cmd all` `/MT /W4` | x86+x64 OK, 0 warnings |
| `test.cmd` | **102 passed, 1 failed** — только `test_mf_probe_graceful` (flake probe >2 s на этом стенде; не L4C-09) |
| 24 profiles/degrade tests | **PASS** (fake clock, эталон 6/12/18/24/39 s) |
| `dumpbin` dependents/imports | KERNEL32/USER32/GDI32/WS2_32/ole32/OLEAUT32 — без VC/MF |

---

## 4. Live evidence (Win10 x64, Core i7, Iris Xe)

**Стенд:** SN `a4b0000773c82116d210826`, desktop `disp:e1ddc8e3`, l4desk → adapter → `C:\l4tools\l4capture\bin\l4capture.exe`.

| Проверка | Факт |
|---|---|
| Запуск через l4desk | **PASS** `stream running (1280x720 @ 10 fps)` |
| Resolve `default` | **PASS** 720p/10 (OpenH264; MFT `supported=false`) |
| Capture backend | DXGI (`cap=2`), encoder OpenH264 (`enc=1`) |
| Binary hash | `8c8a3e5b…` / `6fc1e313…` (installed) |
| Ресурсы | WS ~61 MiB, Private ~62 MiB, handles ~265, threads 7; без монотонного роста за сессию |
| CPU | ~0.86 ядра (software 720p OpenH264; бюджет MFT ≤5% неприменим) |
| Lease renew / adapter | штатно |
| ffmpeg fallback | **не** использовался при рабочем profile_id |

**До фикса profile_id:** `child exited code=5` → ffmpeg (~2080 kbit/s libx264).  
**Исторические code=99 (FATAL)** 15:31–15:32 — без degrade-лога, кандидат на HIGH_LOAD, не подтверждено.

---

## 5. Матрица A1–A13

| № | Критерий | Результат | Evidence |
|---|---|---|---|
| A1 | Профили §7.1, resolve, fail-closed | **PASS** | unit + live 720p |
| A2 | Forced overload D0→D3 live | **GAP** | fake-clock PASS; **live ladder не пойман** (stream timeout) |
| A3 | Монотонность | **PASS** | unit |
| A4 | Шаг + hold-off 6 s | **PASS** | unit |
| A5 | Анти-осцилляция | **PASS** | unit |
| A6 | IDR/SPS при смене + browser decode | **PARTIAL** | reinit unit; **browser на переходах GAP** |
| A7 | Input-gate E2E | **GAP** | helper unit only |
| A8 | Floor stop 15 s | **PASS** | unit (14999/15000) |
| A9 | Битрейт-политика | **PASS** | unit |
| A10 | Safety ≤500 ms / ресурсы | **PARTIAL** | API unit; live reinit-stop GAP |
| A11 | Сборка/тесты/стенды | **PARTIAL** | local 102/1 (MF flake); Win7 smoke GAP |
| A12 | Scope | **PASS** | git |
| A13 | DETACHED_V1 + commit | **PASS** | `c6a0f0f`, этот отчёт + candidate |

---

## 6. Явные GAP (не закрыты)

1. **Live forced-overload ladder D0→D3→HIGH_LOAD** — stream не поднялся с delay-харнесом (`test_encode_delay_ms.txt=150`); подтверждение только fake-clock.
2. **Browser decode на переходах 720→540→480**.
3. **Внешний input-gate E2E** в обход UI.
4. **Win7 SP1 x86/x64 runtime smoke** новыми exe.
5. **100-цикл** baseline handles/Private Bytes под forced overload.
6. **`test_mf_probe_graceful` flake** (probe 2–4 s > 2 s) — не L4C-09, известный.

Установленный runtime: `C:\l4tools\l4capture\bin\l4capture.exe` (копия x64), backup `l4capture.exe.bak-pre-L4C-09`. Тестовый delay-файл можно удалить перед production.

---

## 7. Итог

Реализация профилей 540p/720p и монотонной деградации **готова**, live-путь **default→1280×720@10** подтверждён. Candidate `H-L4C-09-v1` передаётся контроллеру с GAP §6. Готовность к L4C-10 объявляет контроллер.
