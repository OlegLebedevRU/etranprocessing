# L4C-11-RELEASE-PACKAGE — Отчёт исполнителя

**Prompt ID:** `L4C-11-RELEASE-PACKAGE`  
**Статус заявки:** `ACCEPTED` (кандидатура контроллеру; residual gaps — owner-accepted, см. §8)  
**Ветка:** `l4capture/l4c-11-release-package`  
**producer_commit:** `cd1f58f78d937f540594f67464cf094e7de88a58` (release-scope only)  
**Source:** `git_sha=cd1f58f…`, **`dirty=true`** (чужой worktree; owner override `--allow-dirty` 2026-09-23)  
**Версия suite:** `1.8.0`  
**Дата:** 2026-09-23  
**legal_review:** `OUT_OF_SCOPE_BY_OWNER_DECISION`  
**`PRODUCTION_DEPLOYED`:** `false`  
**`PUBLISHED_TO_REGISTRY`:** **true**  
**`DOWNLOAD_VERIFIED`:** **true**

**Owner-решения (2026-09-23):**
1. Коммит **только release-scope**; чужой dirty (MenuBuilder / l4desk-service / l4media / docs) не трогать.  
2. **G4 (Win7/Embedded) и G10 (2h soak) — после публикации 1.8.0.**  
3. **`--allow-dirty`** — явный override (publisher блокирует dirty; без флага publish невозможен).

---

## 1. Contract Gate (H-L4C-10-v1)

| Проверка | Результат |
|---|---|
| `H-L4C-10-v1` ACCEPTED | да |
| `READY_FOR_L4C_11` / шаг 11 | да |
| SHA-256 7 входных артефактов | **7/7 OK** |
| Открытые пробелы H-L4C-10 (Win7, 2h soak) | **перенесены owner’ом на post-release** |

---

## 2. Scope / commit

**Commit `cd1f58f` (13 файлов, release-scope only):**  
`build_dist.cmd`, `version.txt`, `release/{Build-StagingPayloads,New-ReleaseManifest,Test-L4CapturePackage}.ps1`, `l4superv/pack_zip.cmd`, `l4desk` adapter + test, `l4capture/{OPENH264_LICENSE,NOTICE-OpenH264,SBOM,ROLLBACK}`.

**Не тронуто / не закоммичено:** MenuBuilder, l4media, l4desk-service, FRONT/BACK, wire IPC, MQTT/input, `contract-handoff.md`.

Completeness gate: **6 FAIL → 6 PASS**.  
Consumer-fix: `EVENT_METRICS` `gdi_handles` при `plen>=30` (было `>=32`) + unit test.

---

## 3. LOCAL_BUILD 1.8.0

| Артефакт | Size | SHA-256 |
|---|---|---|
| `tools/dist/l4setup.exe` | 29 385 216 | `4c388e529af7a86b2d71d83d6fcdec0b2d8e4d75ec5526db89ae2c812d7b93c8` |
| `tools/dist/l4tools-release.json` | 1 322 | `0ece6a9f0cd7316985006c18090eca34b9d2f9e98dcf7316b246152d9d97697c` |
| `tools/dist/SHA256SUMS` | 165 | `c9f791321f399c985ca8b9712f5e99c67a6d1268ebdd46a78d0bc6d2f96ccd50` |

- PE ProductVersion **1.8.0**, Authenticode **NotSigned**, `PAYLOAD_X86`/`PAYLOAD_X64` в exe  
- `components`: leo4proxy 1.2.0, l4superv 1.7.6, l4desk 1.7.6, l4pin 1.7.2, l4con 1.7.2, l4sql 1.0.0, **l4capture 1.0.0**, mosquitto 2.1.2, ffmpeg 9.0  
- Payload содержит `l4capture\bin\l4capture.exe` + SBOM/license/rollback + `term_tool-user-guide.md` + FFmpeg  
- Путь установки: `C:\l4tools\l4capture\bin\l4capture.exe` (= lookup l4desk `%BASE%\l4capture\bin`)

Тесты: **l4capture 115/115**, **adapter 21/21**, completeness **PASS**.

---

## 4. PUBLISHED_TO_REGISTRY (G11)

```
https://l4tools-generic.ar.cloud.ru/l4tools/1.8.0/l4setup.exe
https://l4tools-generic.ar.cloud.ru/l4tools/1.8.0/SHA256SUMS
https://l4tools-generic.ar.cloud.ru/l4tools/1.8.0/l4tools-release.json
```

| Шаг | Результат |
|---|---|
| `check 1.8.0` до publish | 404 OK |
| dry-run | OK (order: setup → SHA256SUMS → manifest) |
| `publish … --allow-dirty` | **UPLOADED** |
| `check 1.8.0` после | **ALREADY published** |
| Download ×3 + SHA-256 | **3/3 MATCH** |
| `artifacts/l4tools/1.8.0.json` | записан (`registry_digest_verified: true`) |
| `releases.jsonl` append | записан (append-only) |

| Файл | Local SHA = Downloaded SHA | Size |
|---|---|---|
| `l4setup.exe` | `4c388e52…` = `4c388e52…` | 29 385 216 |
| `SHA256SUMS` | `c9f79132…` = `c9f79132…` | 165 |
| `l4tools-release.json` | `0ece6a9f…` = `0ece6a9f…` | 1 322 |

Evidence: `tools/l4capture/evidence/1.8.0-download/`.  
Аудит: `artifacts/l4tools/1.8.0.json` sha256 `260187c2d37f5ca98ade068dd0406cc00330ad202030130c5bf1ccad4f677553`.  
`latest` / каналы обновлений / production — **не менялись**.

---

## 5. Этапы (§8)

| Этап | Факт |
|---|---|
| `LOCAL_BUILD` | **DONE** |
| `INSTALL_VERIFIED` | **PARTIAL** — payload extract + path contract; полный clean/upgrade/rollback **post-release** |
| `PUBLISHED_TO_REGISTRY` | **true** |
| `DOWNLOAD_VERIFIED` | **true** |
| `PRODUCTION_DEPLOYED` | **false** |

---

## 6. Матрица G1–G11

| Gate | Статус | Комментарий |
|---|---|---|
| G1 source | **OVERRIDDEN** | dirty=true + `--allow-dirty` (owner); release-scope в `cd1f58f`; G4/G10 перенесены |
| G2 build | **PASS** | 1.8.0, tests 0 fail |
| G3 package | **PASS** | l4capture отдельной папкой, notices/SBOM/rollback, FFmpeg, без секретов |
| G4 Win7 | **POST_RELEASE** | owner 2026-09-23 |
| G5 install/rollback | **POST_RELEASE** | |
| G6 media E2E | **PARTIAL** | local RTP L4C-09/10; browser/ingress post-release |
| G7 safety R1–R4 | **PARTIAL** | unit ok; полная fault-матрица post-release |
| G8 delivery | **POST_RELEASE** | |
| G9 metrics §13 | **PARTIAL** | real METRICS; browser budgets post-release |
| G10 2h soak | **POST_RELEASE** | owner 2026-09-23 |
| G11 publish | **PASS** | upload + download 3/3 + audit |

---

## 7. Install / backend settings

- `C:\l4tools\l4capture\bin\l4capture.exe`  
- `media_backend=l4capture` (fallback ffmpeg); камеры всегда FFmpeg  
- Не служба: child только от l4desk, интерактивная сессия + lease  
- Upgrade/rollback: `tools/l4capture/ROLLBACK.md` (в payload)  
- Consumer METRICS 30B: `gdi_handles` при `plen>=30`

---

## 8. Residual (owner-accepted / post-release)

1. **G4** Win7 SP1 x86/x64, WES7, POSReady — после 1.8.0  
2. **G10** 2h soak (drift ≤5 МиБ) — после 1.8.0  
3. **G5–G9** install/upgrade/browser/fault matrix — после 1.8.0  
4. **dirty=true** в манифесте (чужой worktree) — override  
5. Authenticode NotSigned  
6. `l4desk/run_tests.cmd` без `tests/fake_ffmpeg.c`  
7. Производственный деплой / mass upgrade / канал `latest` — **вне задания**

---

## 9. DETACHED_V1

Отчёт: `docs/l4capture/handoffs/L4C-11-RELEASE-PACKAGE-report.md`  
Candidate: `docs/l4capture/handoffs/L4C-11-RELEASE-PACKAGE-candidate.md` (`H-L4C-11-v1`)  
`contract-handoff.md` не редактировался.  
`legal_review: OUT_OF_SCOPE_BY_OWNER_DECISION`.
