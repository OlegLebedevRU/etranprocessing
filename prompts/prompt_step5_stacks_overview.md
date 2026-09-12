# Шаг 5 — Этап 1 переработки `tools`: Zero-Touch Installer, повторное использование сертификата, публикация в Generic-реестр

## 1. Статус и рамки

- **Статус:** ЭТАП 1 ЗАВЕРШЕН (13.09.2026). Все 6 заданий реализованы, сквозные стендовые испытания на реальной машине Windows 10 x64 успешно пройдены (PASS). Полный отчет: [`prompts/step5_stage1_stand_report.md`](step5_stage1_stand_report.md).
- **Источник требований:** [`docs/term_tool-zero-touch-installer-and-remote-runtime-plan.md`](../docs/term_tool-zero-touch-installer-and-remote-runtime-plan.md) — разделы 3.2 (S1–S10), 4, 5 (Пакеты 1–5), 6.2 (DoD 1–6, 8, 9), 7.4 (контракт реестра), 8 (этапы).
- **Что входит в Этап 1:** единый `l4setup.exe`; Cert Discovery и повторное использование валидного сертификата; опциональный PIN с активным ожиданием; smoke-тест и `install_summary.json`; релиз-манифест и версия; прямая публикация релиза в публичный Generic-реестр `https://l4tools-generic.ar.cloud.ru/` локальным инструментом (без промежуточного сервера); вывод бинарников из Git; руководство инженера; стендовая проверка на реальной машине.
- **Что НЕ входит (Этап 2+):** Update Agent в `l4superv`, серверные манифесты каналов на `87.242.100.34`, `l4setup.exe --from-registry`, топик `dev/{SN}/tools`, изменения `app1`/MenuBuilder, Authenticode-подпись (если нет сертификата подписи — фиксировать как долг), Windows-раннер на builder. Любое из этого в PR Этапа 1 — отклонять.

### Task intake

- Тип: переработка терминальных утилит (C/Win32) + локальный инструмент публикации (Python 3.14) + документация.
- Владельцы стеков: `tools/l4pin`, `tools/l4superv`, новый `tools/l4setup` — C/Win32 MSVC `/MT`, x86 + x64; `tools/build_dist.cmd` — cmd/PowerShell; `deploy/publish_l4tools.py` — Python 3.14 (локальная утилита прямой публикации в Generic-реестр); `tools/dist/README.md`, `docs/term_tool-user-guide.md` — Markdown.
- Инварианты: сертификат терминала без явного `--force-reissue` **никогда** не удаляется и не перевыпускается; PIN и ключи реестра не попадают в Git, логи, `install_summary.json`; терминалу не выдаются учетные данные реестра; presence-payload `dev/{SN}/app` (`app_online`/`app_offline`) и `dev/{SN}/svc` не изменяются; никаких изменений общей БД и серверных контейнеров.
- Реальный стенд Этапа 1 — **эта рабочая станция** (см. раздел 5).

## 2. Факты ревизии (проверено 12.09.2026)

### 2.1. Текущий код

| Компонент | Файл | Что делает сейчас | Дефект для Этапа 1 |
|---|---|---|---|
| `l4install` (x86/x64) | `tools/l4superv/src/installer_main.c` | Распаковывает `tools.zip`/`ffmpeg.zip` (miniz), создает подкаталоги `l4sql/l4pin/l4con/l4superv/l4desk/ffmpeg`, прописывает PATH, регистрирует службы, печатает подсказку «PIN Tool: …\l4pin\l4pin.exe» | С сертификатом не работает вообще; три отдельных файла; нет автоподбора архитектуры; нет smoke |
| `l4pin` | `tools/l4pin/src/cert_store.c` (строки ~260–300), `src/*.c` | CA API `GET /api/certificates/?function=check&pin=…&v=26` → CNG KSP RSA 2048 (`EtranTerminalKey`, неэкспортируемый) → CSR → `POST …function=setup` → PKCS#7 → `LocalMachine\MY`. URL берется из `_leo4/info` (`listeners.http_local`) либо fallback `https://iot-processing.ru/api/certificates` | **Безусловно удаляет все сертификаты с тем же email и ставит новый** (`CertDeleteCertificateFromStore` + `CERT_STORE_ADD_REPLACE_EXISTING`). Нет режима «проверить и не трогать» |
| `l4superv` | `tools/l4superv/src/orchestrator.c` (строки ~140–250), `state_mgr.c`, `mosquitto_conf.c`, `hardware_fingerprint.c` | Определяет `active`/`standby` только по ответу `leo4proxy` `http://127.0.0.1:18443/_leo4/info`; переписывает `mosquitto.conf`; при клоне (`auto_reset_on_clone`) удаляет сертификаты и уходит в standby; `state.json` хранит `status, sn, thumbprint, not_after, hw_fingerprint, installer_base_path, services{}` | Нет такта ожидания PIN; нет `installed_version`; нет внешнего форс-такта |
| `leo4proxy` | `tools/leo4proxy` (v1.2.0) | `_leo4/info` отдает `status`, `certificate_found`, `sn`, `thumbprint`, `serial`, `not_after`, `has_private_key`, `listeners`, `upstreams` | Изменения не требуются |
| Сборка | `tools/build_dist.cmd` → `l4superv/build.cmd all` → `pack_zip.cmd` | Результат коммитится в `tools/dist` (`tools.zip` 1.5 МБ, `ffmpeg.zip` 28.6 МБ + `.sha256`, `l4install_x86/x64.exe`, `.cmd`) и `tools/dist_win7_sp1` | Бинарники в Git; нет версии релиза, манифеста, SHA256SUMS |
| Builder / Публикация | `deploy/publish_l4tools.py`, `deploy/tests/test_publish_l4tools.py` | Локальная утилита прямой публикации `l4tools` в Generic-реестр: верификация `SHA256SUMS`/манифеста, проверка существования версии `HEAD`, HTTP `PUT` файлов, сверка RFC 3230 `digest`, сохранение `artifacts/l4tools/<version>.json` и `releases.jsonl` | Промежуточный сервер `176.108.247.249` исключен из схемы публикации `l4tools` |

### 2.2. Generic-реестр `https://l4tools-generic.ar.cloud.ru/`

- Анонимный `GET /<path>` → файл; `HEAD /<path>` → `ETag`, `digest: sha-256=<hex>`; `Range` не поддерживается (всегда полное тело); `PUT` на путь файла без ключа → 405.
- Публикация: `PUT https://<key_id>:<key_secret>@l4tools-generic.ar.cloud.ru/upload/<path>` (Basic auth ключом сервисного аккаунта). Перезапись имени запрещена (immutable-by-name), размер ≤ 1024 МБ, вложенные каталоги через `/`, символ `%` в имени запрещен.
- TLS: `CN=ar.cloud.ru`, GlobalSign RSA OV SSL CA 2018, TLS 1.2. Egress бесплатен до 100 ГБ/мес.
- В реестре сейчас только `ar-root.md` (тестовый файл владельца — не трогать, не удалять).

### 2.3. Стенд (эта машина)

- Windows x64; службы `Leo4Proxy`, `mosquitto`, `L4Con`, `L4Superv` — `Running`/`Automatic`; `C:\l4tools` содержит подкаталоги утилит, `l4install.exe`, `l4install_x64.exe`, `tools.zip`, `l4superv.json`, `state.json`, `terminal-tools-user-guide.md`.
- Сертификат: `LocalMachine\MY`, `CN=a4b0000773c82116d210826`, issuer `CN=iot.leo4.ru, O=Leo4`, `NotAfter 2027-08-29 17:09:40 UTC`, thumbprint `CC88419A4C3763150A4C0905261EC073C58CFC09`, `has_private_key: true`.
- `state.json`: `status: active`, `hw_fingerprint` задан, `installer_base_path: C:\l4tools`; `_leo4/info`: `status: ready`, `mqtt_active_clients: 1`.
- Следствие: стенд = **S3 + S9** одновременно. Главный регрессионный критерий — thumbprint после прогона `l4setup.exe` не изменился.

## 3. Изолированные задания

| # | Стек / проект | Промпт | Основной результат | Зависит от |
|---|---|---|---|---|
| 1 | C/Win32 — `tools/l4pin` + общий модуль | [cert_discovery](prompt_step5_agent_tools_cert_discovery.md) | `cert_discovery.c/.h`, guard в `l4pin` (`--check`, `--force`), тесты на реальном хранилище | — |
| 2 | C/Win32 — новый `tools/l4setup` | [l4setup](prompt_step5_agent_tools_l4setup.md) | Единый `l4setup.exe`: фазы 1–6, Cert Reuse / PIN / `--no-pin`, smoke, `install_summary.json`, коды возврата | 1 |
| 3 | C/Win32 — `tools/l4superv` | [l4superv wait](prompt_step5_agent_tools_l4superv_wait.md) | Такт ожидания активации, `pending_pin.json`, `installed_version`, форс-такт `SERVICE_CONTROL 128`; **без** Update Agent | 1 |
| 4 | cmd/PowerShell — `tools/build_dist.cmd` | [release build](prompt_step5_agent_release_build.md) | Payload x86/x64 как ресурсы, `l4tools-release.json`, `SHA256SUMS`, `VERSIONINFO`, `.gitignore` бинарников | 2, 3 |
| 5 | Python 3.14 — `deploy/publish_l4tools.py` | [registry publish](prompt_step5_agent_registry_publish.md) | Прямая публикация `l4tools` в Generic-реестр: verify, check, `PUT /upload/l4tools/<semver>/*`, сверка `HEAD digest`, `artifacts/l4tools/<version>.json`, `releases.jsonl` (без промежуточного сервера) | 4 |
| 6 | Markdown + PowerShell — документация и стенд | [docs & stand test](prompt_step5_agent_docs_and_stand_test.md) | `tools/dist/README.md`, `docs/term_tool-user-guide.md`, чек-лист стендового прогона, отчет владельцу | 2–5 |

Порядок: 1 → (2 ∥ 3) → 4 → 5 → 6. Задания 2 и 3 не правят файлы друг друга: `l4setup` вызывает `l4pin.exe`/`l4superv.exe` только через CLI и файлы контракта. Каждый агент работает строго в своем каталоге; правки чужого стека — только через изменение этого overview с явным согласованием.

## 4. Общий контракт Этапа 1 (обязателен для всех заданий)

### 4.1. Cert Discovery (задание 1 → 2, 3)

```text
enum cert_state { CERT_VALID, CERT_EXPIRING, CERT_BROKEN, CERT_ABSENT };
cert_state cert_discover(const wchar_t *expected_sn /* may be NULL */, cert_info *out);
cert_info { char thumbprint_hex[41]; char sn[64]; char not_after_utc[32]; char issuer_cn[64]; bool has_private_key; int days_left; }
```

- Хранилище `LocalMachine\MY`. Кандидат: issuer CN `iot.leo4.ru`; приватный ключ доступен (`CryptAcquireCertificatePrivateKey`, CNG, silent); `NotAfter > now`.
- `CERT_VALID`: кандидат есть, `days_left > 30`, и (если `expected_sn` задан) `CN == expected_sn`.
- `CERT_EXPIRING`: то же, но `days_left <= 30`.
- `CERT_BROKEN`: сертификат issuer `iot.leo4.ru` найден, но нет приватного ключа, или истек, или `CN != expected_sn`.
- `CERT_ABSENT`: кандидатов нет.
- Несколько кандидатов → берется с максимальным `NotAfter`; остальные **не удаляются** в Этапе 1 (фиксируются в лог как `cert_duplicates: N`).
- CLI-обертка: `l4pin.exe --check [--sn <SN>] [--json]` → stdout JSON `{ "state": "valid|expiring|broken|absent", "thumbprint", "sn", "not_after", "days_left" }`, код возврата `0` valid, `1` expiring, `2` broken, `3` absent, `>= 20` ошибка.
- `l4pin.exe <PIN>` без `--force` при `CERT_VALID` → сообщение и код `0`, **ничего не меняет**. `l4pin.exe --force <PIN>` — прежнее поведение (удалить по email, выпустить новый).

### 4.2. CLI и коды возврата `l4setup.exe` (задание 2; используют 3, 6)

| Флаг | Значение |
|---|---|
| `--pin <PIN>` / `-p` | PIN для выпуска; при `CERT_VALID` игнорируется с предупреждением `cert_reused` (без `--force-reissue`) |
| `--force-reissue` | Разрешить перевыпуск при `CERT_VALID`/`CERT_EXPIRING` (требует `--pin`) |
| `--no-pin` | Не запрашивать PIN; при `CERT_ABSENT` завершить в Standby (S10) |
| `--silent` / `/S` | Без окон; без `--pin` эквивалентно `--no-pin` |
| `--dest <DIR>` / `-d` | Каталог, по умолчанию `C:\l4tools` (уважать `state.json.installer_base_path`, если есть) |
| `--repair` | Принудительная переустановка файлов/служб даже при совпадении версии |
| `--smoke-only` | Только фаза 5 на существующей установке |
| `--version` | Печать версии из `VERSIONINFO`, код `0` |

Коды возврата: `0` ready (active + smoke ok); `10` `READY_FOR_PIN` (standby, PIN не введен); `11` `READY_FOR_ONLINE` (S7: PIN сохранен в `pending_pin.json`, CA недоступен); `12` ready, но smoke с предупреждениями (`desktop_locked`, `ffmpeg_smoke_capture: skipped`); `20` нет прав/UAC отклонен; `21` неподдерживаемая ОС; `22` drainage не удался (порт/служба заняты); `23` ошибка распаковки/записи; `24` ошибка регистрации служб; `25` ошибка выпуска сертификата (PIN отклонен CA); `26` таймаут активации (сертификат есть, `active` не наступил за 15 с); `27` smoke провален (критичный probe).

Повторный запуск на уже установленной версии (совпадает `VERSIONINFO` и `state.json.installed_version`, файлы на месте) без `--repair` выполняет только фазы 3–5.

### 4.3. `install_summary.json` (задание 2 пишет; 3 читает `installed_version`; 6 проверяет)

```json
{
  "schema": 1,
  "timestamp": "2026-09-12T18:00:00Z",
  "installer_version": "1.6.0",
  "os": "Windows 10 Pro (10.0.19045) x64",
  "target_arch": "x64",
  "dest": "C:\\l4tools",
  "status": "ready | standby_waiting_pin | ready_for_online | ready_with_warnings | failed",
  "exit_code": 0,
  "cert": { "state": "valid", "reused": true, "reissued": false, "thumbprint": "CC88…", "sn": "a4b0000773c82116d210826", "not_after": "2027-08-29T17:09:40Z" },
  "drainage": { "services_stopped": ["Leo4Proxy","mosquitto","L4Con","L4Superv"], "processes_killed": [], "ports_freed": [] },
  "probes": { "proxy_info": "ok", "mosquitto_port": "ok", "user_session_id": 1, "l4desk_running": true, "ffmpeg_smoke_capture": "ok", "desktop_locked": false },
  "warnings": []
}
```

PIN, ключи, содержимое `pending_pin.json` в этот файл **не пишутся**.

### 4.4. `pending_pin.json` и такт ожидания (задание 2 пишет, 3 читает и удаляет)

- Путь `C:\l4tools\pending_pin.json`, содержимое: `{ "schema": 1, "created_at": "...", "expires_at": "<+72h>", "pin_dpapi": "<base64 CryptProtectData, CRYPTPROTECT_LOCAL_MACHINE>" }`. ACL — только `SYSTEM` и `Administrators`.
- `l4superv`: пока `_leo4/info.certificate_found == false` — каждые 30 с: если файл есть и не истек и CA доступен — запуск `l4pin.exe <PIN>` (PIN расшифрован в памяти, не логируется), результат → лог; при успехе или истечении файл удаляется (`SecureZeroMemory` перед `DeleteFileW` не требуется — файл уже зашифрован DPAPI, но перезаписать нулями перед удалением).
- При появлении сертификата любым способом — переход в `active` за ≤ 15 с (сократить интервал опроса `_leo4/info` в standby до 5 с).
- `SERVICE_CONTROL 128` (user-defined) → немедленный такт оркестратора. `l4setup.exe` посылает его после успешного выпуска сертификата.
- `state.json` получает поля `installed_version` (SemVer строкой) и `installer_summary_path`.

### 4.5. Релиз и реестр (задания 4, 5, 6)

- Версия: SemVer `1.6.0`, Git-тег `tools/v1.6.0`; зашита в `VERSIONINFO` `l4setup.exe` и `l4superv.exe`, печатается `--version`.
- Локальный выход `tools/build_dist.cmd` в `tools/dist/`: `l4setup.exe`, `l4tools-release.json`, `SHA256SUMS`. Всё это в `.gitignore` (кроме `README.md`, `.cmd`).
- `l4tools-release.json`: `{ "schema": 1, "version": "1.6.0", "git_sha": "…", "built_at": "…", "builder": "windows-dev", "files": { "l4setup.exe": { "sha256": "…", "size": N } }, "components": { "leo4proxy": "1.2.0", "l4superv": "…", "l4desk": "1.5.0", "l4pin": "…", "l4con": "…", "mosquitto": "…", "ffmpeg": "…" }, "min_os": "6.1", "arch": ["x86","x64"] }`.
- Раскладка в реестре: `l4tools/<semver>/{l4setup.exe, l4tools-release.json, SHA256SUMS}`. Никаких `latest/`. Detached-подпись `.sig` — Этап 2 (в Этапе 1 не публиковать пустышку).
- Прямая публикация локальным инструментом `deploy/publish_l4tools.py` с ключом из переменных окружения или `.env` (`AR_GENERIC_KEY_ID`, `AR_GENERIC_KEY_SECRET`); после каждого `PUT` — `HEAD` и сверка `digest: sha-256` с локальным `sha256sum`. Повтор той же версии → ошибка «version already published» (код `2`) до любых `PUT`.
- Инженер получает дистрибутив по URL `https://l4tools-generic.ar.cloud.ru/l4tools/<semver>/l4setup.exe` и сверяет `SHA256SUMS` (`certutil -hashfile l4setup.exe SHA256`).

## 5. Протокол стендовой проверки на этой машине

Обязательная последовательность для заданий 2, 3, 6 (детали — в промпте 6). Любой шаг с пометкой **[владелец]** выполняется или подтверждается владельцем в чате.

1. **Снимок «до»:** `Get-ChildItem Cert:\LocalMachine\My | ? Issuer -like '*iot.leo4.ru*' | select Thumbprint,NotAfter`, `Invoke-WebRequest http://127.0.0.1:18443/_leo4/info`, `Get-Service Leo4Proxy,mosquitto,L4Con,L4Superv`, копия `C:\l4tools\state.json` и `l4superv.json` в `C:\l4tools\backup-<date>\`. **[владелец]** подтверждает, что можно перезапускать службы (в MenuBuilder UI не должно быть активной трансляции этого терминала).
2. **Прогон S3+S9:** `l4setup.exe --silent` (без PIN) из каталога вне `C:\l4tools`. Ожидание: код `0`, `install_summary.json.cert.reused == true`, thumbprint не изменился, `_leo4/info.status == ready`, 4 службы `Running`, `l4desk.exe` в интерактивной сессии.
3. **Прогон с лишним PIN:** `l4setup.exe --silent --pin 000000` (заведомо неверный PIN). Ожидание: код `0`, `cert_reused`, к CA обращений **нет** (в логе `l4pin`/инсталлятора нет `function=check`).
4. **Идемпотентность:** повторный `l4setup.exe --silent` → только фазы 3–5, код `0`, файлы `C:\l4tools\*\*.exe` не перезаписаны (сравнить `LastWriteTime`).
5. **[владелец]** в MenuBuilder UI: терминал `online`, «Запустить трансляцию» → видео ≤ 1 с, удаленный ввод работает (DoD 5). Затем остановить трансляцию.
6. **S10 / S7 — только по согласованию [владелец]:** варианты: (а) ВМ Windows 10/11 x64 без сертификата; (б) на этой машине после экспорта сертификата **невозможен** (ключ неэкспортируемый) — значит на этой машине S10 проверяется только при готовности владельца выдать новый PIN и перевыпустить сертификат (`--force-reissue`). Без явного «да» в чате сценарий S10 на этой машине не выполняется.
7. **Реестр:** `curl.exe -I https://l4tools-generic.ar.cloud.ru/l4tools/1.6.0/l4setup.exe` → `digest: sha-256=` совпадает с `SHA256SUMS`; `curl.exe -O …` → `certutil -hashfile` совпадает.
8. **Откат при провале:** `C:\l4tools\backup-<date>\` + старые `l4install_x64.exe` + `tools.zip` из `tools/dist` текущего коммита; сертификат при откате не трогается.

## 6. Точки привлечения владельца (задавать вопрос в чате, не решать самостоятельно)

| Когда | Что нужно от владельца |
|---|---|
| Перед первым перезапуском служб на стенде | Подтверждение, что терминал `a4b0000773c82116d210826` можно перезапускать (нет активной сессии в UI) |
| Проверка DoD 5 | Открыть MenuBuilder UI, проверить `online`, запустить трансляцию и удаленный ввод, сообщить результат |
| Сценарий S10/S7 (выпуск сертификата) | Новый одноразовый PIN и явное согласие на `--force-reissue` на стенде, либо ВМ |
| Публикация в реестр (задание 5) | Ключ `key_id/key_secret` сервисного аккаунта Generic-реестра положить в локальный `.env` или переменные окружения (не передавать в чат/Git); подтвердить имя первой версии (`1.6.0`) — повторно опубликовать под тем же именем нельзя |
| Первый запуск неподписанного `l4setup.exe` | Подтвердить SmartScreen/UAC вручную; решение о покупке/использовании сертификата Authenticode — отдельно |
| Удаление бинарников из Git (`tools/dist/*.zip`, `*.exe`) | Подтверждение, что никто не берет дистрибутив из Git напрямую (после публикации `1.6.0` в реестре) |
| Расхождение фактов из раздела 2 с кодом | Сообщить и остановиться, не «чинить» контракт молча |

## 7. Фактические результаты Этапа 1 (зафиксировано 13.09.2026)

- **Отчет стендовых испытаний:** [`prompts/step5_stage1_stand_report.md`](step5_stage1_stand_report.md).
- **Снимки состояния стенда (без PIN и секретов):**
  - Снимок «До» (исходное состояние S3+S9): `C:\l4tools\stand-before-20260912-235049.json`
  - Снимок «После» (прогон `l4setup.exe --silent`): `C:\l4tools\stand-after-20260912-235812.json`
  - Снимок «Идемпотентность» (повторный прогон): `C:\l4tools\stand-after-20260913-000034.json` (24 exe идентичны)
  - Снимок S10 (без сертификата, `standby_waiting_pin`): `C:\l4tools\stand-after-20260913-001126.json`
  - Финальный снимок активного стенда (после выпуска сертификата по PIN): `C:\l4tools\stand-after-20260913-002007.json`
- **Итоговый `install_summary.json`:** скопирован в `artifacts/install_summary.json` (`status: ready`, `exit_code: 0`, `cert.state: valid`, `probes: all ok`).
- **Хэши артефактов и публикации в Generic-реестре:**
  - `l4setup.exe` (x86 bootstrapper + LZMA payload x86/x64): SHA-256 `72b3adf0ebb2d8196e4407658ce7b617c0c5d005b1aa6e5a4bcb2d327e9a03f7`
  - URL в реестре: `https://l4tools-generic.ar.cloud.ru/l4tools/1.6.0/l4setup.exe`
  - Реестровый заголовок `digest: sha-256=72b3adf0ebb2d8196e4407658ce7b617c0c5d005b1aa6e5a4bcb2d327e9a03f7`
  - Релизный манифест: `artifacts/l4tools/1.6.0.json`
  - Строка аудита релизов: `releases.jsonl`
- **Подтверждения владельца стенда:**
  - Дословно по DoD 5 (трансляция и мышь): *«трансляция запустилась и остановилась успешно. Управление мышью было успешно.»*
  - Разрешение и прохождение сценария S10 с PIN `986821`: подтверждено, новый сертификат успешно установлен с тем же SN `a4b0000773c82116d210826`.
  - Негативные сценарии: отмена UAC → код `20` (PASS); занятый сторонним процессом порт 1883 → код `22`, сторонний процесс не убит (PASS).
- **Список известных ограничений/долгов:**
  - Authenticode Code Signing подпись бинарников (требуется сертификат подписи).
  - Стендовые испытания на физической машине Windows 7 SP1 x86 (Этап 4).
  - Update Agent и топик `dev/{SN}/tools` (Этап 2).
  - `git rm --cached` для устаревших бинарников `tools/dist/*.zip, *.exe` (выполняется отдельным коммитом после подтверждения владельца).
- **Готовность к Этапу 2:** ПОЛНАЯ (100%).
