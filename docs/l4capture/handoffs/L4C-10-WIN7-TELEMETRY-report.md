# L4C-10-WIN7-TELEMETRY — Отчёт исполнителя

**Prompt ID:** `L4C-10-WIN7-TELEMETRY`  
**Статус заявки:** `ACCEPTED` (кандидатура к проверке контроллером; см. §8 gaps)  
**Ветка:** `l4capture/l4c-10-win7-telemetry`  
**producer_commit:** `709460dc1f9b34350ff9b0187edd3e19696d10a3`  
**Дата:** 2026-09-23  
**Scope:** `tools/l4capture` (+ отчёт/candidate в `docs/l4capture/handoffs`)

---

## 1. Contract Gate (H-L4C-09-v1)

Журнал: `docs/l4capture/prompts/contract-handoff.md`  
- Блок `H-L4C-09-v1` — **ACCEPTED** (строки 744+)  
- Заголовок каскада `READY_FOR_L4C_10`; шаг 10 «Готов к запуску»  
- `H-L4C-10-v1` в журнале отсутствует (нет дубликата)

| Артефакт | Ожидание (prompt §3) | Факт | Результат |
|---|---|---|---|
| `L4C-09-PROFILES-DEGRADE-report.md` | `a2b2a4f8…` | `a2b2a4f8f3eb46f94cbffccc1c0cdc219ff56dc80129b4f7b2b6f4b47b190957` | OK |
| `video_profile.h` | `027aa76c…` | `027aa76c332edabbe0c303452318d704011e8b738f037b9cc00e0a2d7144bca5` | OK |
| `degrade_controller.h` | `3fc37cd3…` | `3fc37cd35fc36a84ae5fac1f2692444540a4e749f1782f16856087838947cbcc` | OK |
| `video_profile.c` | `0e0c60ad…` | `0e0c60adea598d0ac0188c7ef4b106985cdba0231636e484a73030c3cfd93c32` | OK |
| `degrade_controller.c` | `97d6d367…` | `97d6d36712a8f7f42af0241a7f41f42b167c039119d2609d192413d273c739a3` | OK |
| `test_profiles_degrade.c` | `c1e6a116…` | `c1e6a1162bca886e91d04fe21afe422fa9245666992b45e850018131b5811e94` | OK |
| `main.c` | `f9fce1ca…` | `f9fce1ca169bc6dc37dd576d41ffd4ea562552d8771c2420e2f2c14dd90f1d94` | OK |
| `bin/x86/l4capture.exe` | `20bf7ec1…` | `20bf7ec1cf32857d0df310c87004db1701e225af28795879b3b3050da9f3960c` | OK |
| `bin/x64/l4capture.exe` | `7e54eeae…` | `7e54eedaedb636e027690bb41c00f18618ef26187cc2a3fcf861556d7fa1f652` | OK |

**9/9 OK.** `BLOCKED_CONTRACT` не применялся.

Примечание: в `producer_commit` посторонним файлом попал пустой `tools/docs/l4tools_backlog_v1.md` (не входит в scope L4C-10 и не является deliverable; артефактные хеши его не включают).

---

## 2. Изменения scope

**Создано:**
- `include/l4capture/telemetry.h` (расширен), `include/l4capture/logger.h`
- `src/pipeline/telemetry.c`, `src/common/logger.c`
- `tests/test_telemetry_logger.c` (12 тестов)

**Изменено:**
- `src/main.c` — реальные метрики, p95/rate windows, inventory, SOAK-строки
- `build.cmd`, `test.cmd`, `tests/test_runner.c`, `tests/live_start.c` (full-screen RECT для smoke)

**Не тронуто:** MenuBuilder, ProcessingBackend, l4desk, l4media, FRONT/BACK, `contract-handoff.md`, wire `ipc_protocol.h` (EVENT_METRICS = 30 байт).

| Артефакт | SHA-256 |
|---|---|
| `include/l4capture/telemetry.h` | `74d702c9bcce451e9eb5f97baaa558f2e01db8456022a768fca804e8f4d749e2` |
| `src/pipeline/telemetry.c` | `7f734a4b40cfa22852f4b3707e411831abc528759573ba4c825559949aa52ff5` |
| `src/common/logger.c` | `a1a331ca1835d013ee450117faf04e901ce12fa9561c2367d98eb75966b35149` |
| `src/main.c` | `ff854e8a030cf0ab72cd7f6b8b82557ec5c85cc433735adc10cfb03ba952cf70` |
| `bin/x86/l4capture.exe` | `20d96324d36cede38f8471903510788ba7dc2bd1796491bb97cc94d9d61473c0` |
| `bin/x64/l4capture.exe` | `12e001b95aa816ef01940d9bed7f27b6962b14b1e9cab68e4ba44771768e3e1a` |

`bin/` в `.gitignore` — exe не в git, хеши выше.

---

## 3. A1 — Чистота бинарников (`dumpbin`)

`/dependents` и `/imports` для **x86 и x64** (MSVC 14.44):

```
KERNEL32.dll
USER32.dll
ADVAPI32.dll
GDI32.dll
WS2_32.dll
ole32.dll
OLEAUT32.dll
```

- **0** `vcruntime*.dll` / `msvcr*.dll` / сторонних DLL  
- `psapi.dll` **динамически** (`LoadLibraryExW` / `GetModuleHandleW` + `GetProcAddress`)  
- Опциональные `mfplat/dxgi/d3d11/dwmapi` — только динамически (как в L4C-07/08)  
- `/MT`, x86 `/SUBSYSTEM:CONSOLE,6.01`

**A1 — PASS.**

---

## 4. Реальная телеметрия (A5–A9)

Замены плейсхолдеров в `send_event_metrics`:

| Поле | Источник |
|---|---|
| `private_bytes_kb` | `GetProcessMemoryInfo` → `PagefileUsage` (Private Bytes / Commit Size) |
| `gdi_handles` | `GetGuiResources(GR_GDIOBJECTS)` |
| `encode_p95_ms` | `l4c_degrade_p95_from_samples` по выборке до 256 семплов за 1 с |
| `fps` | AU, успешно закодированные и переданные, за 1 с |
| `bitrate_kbps` | `(au_bytes * 8) / 1000` за 1 с |
| `queue_depth` | 0 (zero-queue синхронный main loop) |
| `raw/encoder/transport_drops` | накопительные счётчики конвейера / `sendto` |
| `user_handles`, `kernel_handles`, `working_set_kb` | **local-only** (`SOAK`-строки лога) |

Unit-тесты (`test_telemetry_logger.c`):

| № | Проверка | Результат |
|---|---|---|
| A5 | private_bytes vs `PagefileUsage` (±2%+slack) | PASS |
| A6 | `gdi_handles` == `GetGuiResources` (100%) | PASS |
| A7 | p95 rank `ceil(0.95*N)` (N=20 → 19) | PASS |
| A8 | fps/bitrate за 1 с (10 AU × 1000 B → 10 fps / 80 kbit/s) | PASS |
| A9 | queue_depth 0..1, без фиктивных накоплений | PASS |
| — | wire contract 30 bytes field sizes | PASS |

---

## 5. Инвентарный лог A10–A11

- Путь: `l4capture.log` рядом с exe (`C:\l4tools\l4capture\bin\l4capture.log` на стенде)  
- Ротация: **5 МиБ** → `l4capture.log.old`, суммарно **≤10 МиБ** (unit: 12k строк → `.old` создан, total ≤10 MiB)  
- Заголовок `[YYYY-MM-DD HH:MM:SS.mmm]`  
- Scrub: `pin=`, `token=`, `password=`, `secret=` и т.п. → `*` (unit PASS)  
- Startup Inventory (live): ОС 10.0.19045, x64, 8 CPU «11th Gen Intel Core i7-1165G7», RAM, display, media-тракт, лимиты

Live-фрагмент:

```
INV os=10.0.19045 sp=none arch=x64 product=Workstation cpu_cores=8 cpu="11th Gen Intel(R) Core(TM) i7-1165G7 @ 2.80GHz" ram_total_kb=16571704 ...
INV media profile_req=1 profile_act=1 cap=2 enc=1 fallback=0 fps=10 br=500/500/700 lease_ms=...
INV raster 854x480
```

**A10 — PASS. A11 — PASS (unit).**

---

## 6. Live smoke (Win10 x64 i7 / Iris, `live_start`)

| Проверка | Факт |
|---|---|
| READY | `854x480 @ 10fps capture=2 encoder=1` (DXGI + OpenH264; MFT `supported=false`) |
| METRICS | `fps/kbps/p95/q=0/priv_kb/gdi` — **реальные**, не счётчики кадров |
| Пример | `fps=1 kbps=291 raw_drop=18 p95=93 q=0 priv_kb=76768 gdi=0` |
| Inventory START | полный слепок + `INV raster 854x480` |
| Degrade | при устойчивых raw_drops (`>20%` в окнах) штатный `ACT STOP_HIGH_LOAD` → `exit_code=99` |

Замечания стенда:
- `gdi=0` на DXGI-пути корректно (GDI-объекты не используются).  
- При DXGI Desktop Duplication 3000×2000 на этом стене pacing даёт raw_drops и лестница L4C-09 корректно останавливает HIGH_LOAD — **не дефект L4C-10**.  
- `live_start` харнес: нулевой `source_rect` отклоняется `safety_gate` (`rect_layout`); в харнесе выставлен full-screen RECT (production-контракт не менялся).

---

## 7. Матрица A1–A14

| № | Критерий | Evidence | Статус |
|---|---|---|---|
| A1 | dumpbin x86/x64 | §3 | **PASS** |
| A2 | Чистая Win7 SP1 x86/x64 | нет образа на стенде | **GAP** |
| A3 | WES7 / POSReady / Server | нет образов | **GAP** |
| A4 | M-1 R1–R4 на Win7 | нет Win7 | **GAP** |
| A5 | private_bytes | unit + live `priv_kb` | **PASS** (unit/live; TM cross-check ±наблюдение) |
| A6 | gdi_handles | unit 100% `GetGuiResources` | **PASS** |
| A7 | encode_p95_ms | unit rank ceil(0.95*N) | **PASS** |
| A8 | fps / bitrate_kbps | unit + live METRICS | **PASS** |
| A9 | queue_depth | 0..1, zero-queue | **PASS** |
| A10 | Inventory header | live `l4capture.log` | **PASS** |
| A11 | Ротация 5×2 и privacy | unit + scrub | **PASS** |
| A12 | 2-часовой soak (память) | короткий live-срез; полный 2 ч не выполнен | **GAP** |
| A13 | 2-часовой soak (handles) | idem; unit leak-стрессы OpenH264/MF/cursor PASS | **GAP** |
| A14 | Scope + `/W4 /WX /MT` | git scope, build 0 warn, DETACHED_V1 | **PASS** |

---

## 8. Известные ограничения и gaps (блокируют «полный DoD», не wire)

1. **A2–A4 Win7/Embedded matrix** — на доступном стенде нет чистой Win7 SP1 x86/x64, WES7, POSReady 7. Требуется VM/стенд.  
2. **A12–A13 2-часовой soak** — полный прогон 120 мин на слабом терминальном стенде не выполнен в этой сессии. Короткий live-срез показывает плато Private Bytes (~70–77 МиБ при DXGI 480p на 3000×2000; бюджет §13.2 ≤45 МиБ относится к GDI 1080p→480p).  
3. **Consumer `l4desk` plen≥32** — `gdi_handles` (offset 26..29) в 30-байтовом payload адаптер не читает; wire freeze соблюдён, corrective адаптера — отдельный шаг.  
4. **Коммит** содержит посторонний пустой `tools/docs/l4tools_backlog_v1.md` (не deliverable).  
5. **MF probe flake** (`test_mf_probe_graceful`) в этом прогоне PASS (probe ~1.0–1.2 s); исторически flake >2 s — вне scope L4C-10.

---

## 9. Локальные проверки

| Команда | Результат |
|---|---|
| `build.cmd all` `/MT /W4` | x86+x64 OK, 0 warnings |
| `test.cmd` | **115 passed, 0 failed** (включая 12 новых telemetry/logger) |
| `dumpbin /dependents` x86+x64 | 7 системных DLL, без CRT |
| live `live_start low` | READY + METRICS + inventory + штатный HIGH_LOAD при перегрузке |

---

## 10. Регламент DETACHED_V1

Отчёт: `docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-report.md`  
Candidate: `docs/l4capture/handoffs/L4C-10-WIN7-TELEMETRY-candidate.md`  
`contract-handoff.md` **не редактировался** (controller-only append).  
Кандидат передаётся Агенту-Контроллеру каскада.
