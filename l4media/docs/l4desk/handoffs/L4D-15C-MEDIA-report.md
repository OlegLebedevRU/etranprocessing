# Handoff Report: Реализация архива media technical details (L4D-15C-MEDIA)

## Candidate H-L4D-15C-MEDIA-v1

```yaml
<!-- HANDOFF:H-L4D-15C-MEDIA-v1:BEGIN -->
handoff_id: H-L4D-15C-MEDIA-v1
status: CANDIDATE
contract_kinds:
  - API
  - EVENT
  - DEPLOYMENT
producer_prompt_id: L4D-15C-MEDIA
producer_scope_project: l4media
producer_report_path: l4media/docs/l4desk/handoffs/L4D-15C-MEDIA-report.md
producer_branch: l4desk/l4d-15c-media
producer_commit: fe9dcd10f6071869e5d482ffbb7c66746ef1712a
created_at_utc: '2026-09-21T02:20:00Z'
contract_version: 1.0.0
schema_revision: '1.0.0'
artifact_version: 1.0.0
artifact_paths:
  - l4media/docs/l4desk/handoffs/L4D-15C-MEDIA-report.md
  - l4media/archive/pipeline.py
  - l4media/archive/canonical.py
  - l4media/archive/guards.py
  - l4media/archive/store.py
  - l4media/archive/restore.py
  - l4media/archive/worker.py
  - l4media/archive/cli.py
  - l4media/tests/test_archive_canonical.py
  - l4media/tests/test_archive_guards.py
  - l4media/tests/test_archive_pipeline.py
  - l4media/tests/test_archive_restore.py
  - l4media/tests/test_archive_cli.py
artifact_sha256:
  - 0861ca2710bec5fc264e42240e1f48a845edeae89e29c389863fa1fb2ea0ec61
  - 4bd8220a53a7087cc509ca6aa84767e6d91ca0386dc8524cfaa80e8fc70deeac
  - 5338bed9d45db61af7d437852110d95c4e6f7e907e9b97324a39313d9ef9df8c
  - 45481ad3e2aa3747b436784a88c0f3a0895e2a3736596446ffe628a6c6cf0633
  - 920a48c522c577c2fe26d5ff8ba77534fa7874a047c809229f5c0d5c7d031dfd
  - 4ade6750a64994e3c38759df43af07a1467f1f715e70a944a282247300c6e8e6
  - c0b5e62e3f2fda16150740de6c52d526b4a6b6a19475a75284ea6cc0b5ddda68
  - 4a1ff169e32479afed6796d78c22889b9c9d97ee4176430cd645476ea16937fa
  - d1a0f2a72922046d0cbb2eaf965d7b744d5f9c9b65e6709095ed5303ab7014df
  - 1d34ee90a0f9e9ad8bc4ead390d9d2eb4e62e233d3cf90ffb591c35c994b2c3f
  - 0290e210512687df727fc4db3c1d929c4d602369e0eeee915db93525b7932da0
  - 44a3275bb5dbbb913c9db375280a1896c253edc78312613626c37168f23285db
compatibility:
  backward_compatible_with:
    - H-L4D-15A-DOCS-v1
    - H-L4D-15B-IOT-v1
    - H-L4D-08A-MEDIA-v1
  breaking_changes: false
  notes: "Full implementation of l4media deterministic archive lifecycle for media_stream_samples and media_quality_events under Archive Manifest Contract v1. Invariants: No-Purge-On-Mismatch, hot route and session summaries retention, 3-year retention rules, active streams protection, and safe disabled worker deployment."
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  archive_worker_enabled: false
  archive_dry_run_default: false
contract_payload:
  manifest_version: "1.0.0"
  owner_project: "l4media"
  record_types:
    - "media_stream_samples"
    - "media_quality_events"
  purge_rules:
    financial_records_purged: false
    route_configurations_purged: false
    session_summaries_purged: false
    bounded_chunk_size: 1000
    pre_purge_verification_required: true
    recheck_active_records: true
  storage_rules:
    volume_root_configurable: true
    traversal_symlink_protected: true
    staging_prefix: ".tmp_"
    atomic_promotion: "os.replace"
    compression: "gzip (mtime=0.0)"
supersedes: []
known_risks: []
consumers:
  - L4D-16-MB
next_prompt_id: L4D-16-MB
<!-- HANDOFF:H-L4D-15C-MEDIA-v1:END -->
```

---

## 1. Резюме шага и контекст выполнения

Шаг **`L4D-15C-MEDIA`** выполнен строго в рамках изолированного репозиторного каталога `l4media`.

### Цель шага
Реализовать в `l4media` помесячный архив высокообъёмных технических подробностей (`media_stream_samples`, `media_quality_events`) по общему **Archive Manifest Contract v1** (`H-L4D-15A-DOCS-v1`). Обеспечить строгое соблюдение инвариантов:
- Защита горячих данных: таблицы маршрутов Ingress, сводки сессий (`MediaSession` / `tb_media_session_summaries`), данные активных потоков и реконсиляции никогда не удаляются.
- Временная граница: архивируются только закрытые месяцы старше 3 полных календарных месяцев.
- Детерминированный жизненный цикл: staging (`.tmp_`) -> gzip (`mtime=0.0`) -> `checksum.sha256` -> re-read verify -> atomic rename (`os.replace`) -> bounded chunk purge.
- Инвариант `No-Purge-On-Mismatch`: при любом расхождении хэшей, счётчиков, сбое валидации или активных сессиях очистка строго запрещена.
- Инструменты восстановления (`restore tooling`) и аудит 3-летнего хранения без удаления реальных архивов.
- Безопасный деплой: воркер архивации отключён по умолчанию (`L4MEDIA_ARCHIVE_WORKER_ENABLED=false`).

---

## 2. Проверка входных контрактов (Input Gates)

| Поле | H-L4D-15A-DOCS-v1 | H-L4D-15B-IOT-v1 | H-L4D-08A-MEDIA-v1 | Статус |
|---|---|---|---|---|
| `status` | `ACCEPTED` | `ACCEPTED` | `ACCEPTED` | Валидно |
| `producer_scope_project` | `l4desk-service` | `iot-rpc-rest-app` | `l4media` | Валидно |
| `contract_version` | `1.0.0` | `1.0.0` | `1.0.0` | Совместимо |
| `consumer` | Содержит `l4media` / `L4D-15C-MEDIA` | Содержит `L4D-15C-MEDIA` | Содержит `l4media` | Адресовано текущему шагу |

Контракты приняты; канонические JSON-схемы `archive-manifest.schema.json` и `schemas.json`, а также фикстура `media-archive-purged` из `examples.json` верифицированы тестами.

---

## 3. Спецификация архивного пакета и гарантии

### 3.1. Структура архивного пакета на томе
```text
<volume_root>/<year>/<month>/l4media/<archive_batch_id>/
  ├── samples.jsonl.gz     # Семплы потока (RecordEnvelope: media_stream_samples)
  ├── quality.jsonl.gz     # События качества (RecordEnvelope: media_quality_events)
  ├── checksum.sha256      # Контрольные суммы файлов архива
  └── manifest.json        # Канонический манифест пакета (ArchiveManifest v1.0.0)
```

### 3.2. Типы данных
1. **`media_stream_samples`**:
   Периодические технические метрики потока от терминала: `rtp_packets`, `rtcp_packets`, `bytes`, `bitrate_kbps`, `packet_loss_pct`, `jitter_ms`, `rtt_ms`, `fps`, `frame_width`, `frame_height`.
2. **`media_quality_events`**:
   Дискретные события качества: `event_type` (`stream_degraded`, `packet_loss_spike`, `freeze_detected`, `resolution_change`), `metric_value`, `reason`.
3. **Обёртка `RecordEnvelope`**:
   `record_id`, `record_type`, `occurred_at_utc`, `source_project: "l4media"`, `sn`, `session_id`, `cursor`, `payload`.

### 3.3. Ключевые архитектурные инварианты
1. **Строгое сохранение оперативных данных (Hot Retention):**
   Маршруты Ingress (`routes.conf`, runtime epoll routing table) и сводные строки сессий (`tb_media_session_summaries`) хранятся независимо и **никогда не удаляются архиватором**.
2. **Защита от очистки свежих данных (Retention Boundary):**
   Допустимы к архивации только месяцы со смещением более 3 полных календарных месяцев. Попытка архивации месяца `<= 3 месяцев` отклоняется с кодом `HOT_RETENTION_VIOLATION`.
3. **Защита от активных потоков (Active Streams Guard):**
   При наличии активных сессий или незавершённых медиафайлов за архивируемый период операция прерывается с ошибкой `ACTIVE_RECORDS_DETECTED`, удаление горячих данных не производится.
4. **Инвариант No-Purge-On-Mismatch:**
   Фаза purge запускается только при статусе пакета `verified` после полного перечитывания и совпадения контрольных сумм и счётчиков. При любых расхождениях манифест переводится в `state: "failed"`, а горячие данные не трогаются.
5. **Требование 3-летнего хранения:**
   Манифест фиксирует `retention_years: 3`, `retain_until_utc: created_at + 3 года`, `backup_required: true`.

---

## 4. Реализованные модули подсистемы `l4media/archive`

1. **`l4media/archive/canonical.py`**:
   - Датаклассы: `ArchiveManifest`, `FileEntry`, `VerificationResult`, `PurgeResult`, `ArchiveError`, `RetentionPolicy`, `StorageLayout`, `RecordEnvelope`.
   - Детерминированная запись `write_deterministic_jsonl_gz` с флагом `mtime=0.0`.
   - Потоковое вычисление SHA-256.
   - Валидация манифеста и RecordEnvelope против JSON Schema Draft 2020-12 (`archive-manifest.schema.json`, `schemas.json`).
2. **`l4media/archive/guards.py`**:
   - `RetentionGuard`: проверка смещения > 3 закрытых месяцев.
   - `PathSecurityGuard`: проверка допустимости путей (защита от path traversal, запрет symlink, проверка свободного места на томе).
   - `ActiveStreamsGuard`: проверка открытых сессий и очистка старых временных папок.
   - `VerificationGuard`: полное перечитывание файлов, сверка SHA-256, проверка счётчиков и выборочная декомпрессия (sample restore).
3. **`l4media/archive/store.py`**:
   - SQLite-репозиторий оперативной телеметрии `TelemetryStore` с таблицами `tb_media_stream_samples`, `tb_media_quality_events` и строго горячей таблицей `tb_media_session_summaries`.
   - Порционная очистка `purge_records_chunked` (пачками по 1000 записей).
4. **`l4media/archive/pipeline.py`**:
   - Координатор жизненного цикла: `run_archive(source_month, dry_run, purge)`.
   - Промежуточная стадия `.tmp_<batch_id>_<timestamp>`.
   - Атомарное перемещение `os.replace`.
5. **`l4media/archive/restore.py`**:
   - `inspect_batch`: проверка целостности архива и контрольных сумм.
   - `restore_batch`: извлечение и валидация записей в целевой TelemetryStore или JSONL.
   - `check_retention_and_backups`: read-only аудит 3-летнего хранения и видимости бэкапов.
6. **`l4media/archive/worker.py`**:
   - Фоновый воркер с поддержкой feature flags (`L4MEDIA_ARCHIVE_WORKER_ENABLED=false` по умолчанию).
7. **`l4media/archive/cli.py`**:
   - Интерфейс командной строки: команды `status`, `archive`, `verify`, `restore`, `check-retention`.

---

## 5. Результаты верификации и тестирования

### 5.1. Модульные и интеграционные тесты
В каталоге `l4media/tests/` реализован полный набор тестов (21 тест):
* `test_archive_canonical.py`:
  - Генерация и валидация `archive_batch_id`.
  - Детерминированный gzip (`mtime=0.0`) и совпадение SHA-256.
  - Валидация канонической фикстуры `media-archive-purged` из `examples.json` против схемы Draft 2020-12.
  - Валидация RecordEnvelope и негативные сценарии (неверный owner, заниженный retention).
* `test_archive_guards.py`:
  - Проверка границы 3 месяцев (`RetentionGuard`).
  - Проверка путей и свободного места (`PathSecurityGuard`).
  - Проверка активных сессий (`ActiveStreamsGuard`).
  - Обнаружение несовпадения SHA-256, несовпадения счётчиков и отставания курсоров (`VerificationGuard`).
* `test_archive_pipeline.py`:
  - Полный сквозной цикл `prepared -> verified -> purged`.
  - Подтверждение очистки горячих семплов и сохранения `tb_media_session_summaries`.
  - Режим `dry_run` (без очистки hot-хранилища).
  - Блокировка при нарушении retention window и при активных потоках.
* `test_archive_restore.py`:
  - Инспекция пакета (`inspect_batch`).
  - Восстановление в свежий репозиторий (`restore_batch`).
  - Восстановление в JSONL с фильтром по типу записи и лимитом строк.
  - Аудит 3-летнего хранения (`check_retention_and_backups`) в режиме read-only.
* `test_archive_cli.py`:
  - CLI команды `status`, `archive`, `verify`, `check-retention`.

**Результат запуска:**
```text
============================= 21 passed in 1.26s ==============================
```

### 5.2. Проверки линтеров и типизации
```bash
uv run ruff check archive tests   # All checks passed!
uv run ruff format archive tests  # 13 files left unchanged
uv run pyright archive tests      # 0 errors, 0 warnings, 0 informations
```

---

## 6. Деплой и smoke-тестирование на хосте `87.242.100.34`

1. **Доставка артефактов:**
   Кодовая база `l4media` (включая `archive/`, `tests/`, `pyproject.toml`, обновлённый `compose.yaml`, `.env.example`, `deploy.sh`, `check.sh`) синхронизирована на сервер `87.242.100.34` в `/home/user1/l4media/`.
2. **Конфигурация:**
   Файл `.env` дополнен переменными:
   ```env
   L4MEDIA_ARCHIVE_WORKER_ENABLED=false
   L4MEDIA_ARCHIVE_DRY_RUN=false
   L4MEDIA_ARCHIVE_VOLUME_ROOT=/mnt/l4desk-archive
   L4MEDIA_HOT_TELEMETRY_DIR=/var/lib/l4media/telemetry
   ```
3. **Безопасность сервисов (Isolation & Disabled Worker):**
   - Контейнеры `l4media-ingress` и `l4media-nginx` пересобраны и запущены в штатном режиме.
   - Сервис `archive-worker` выделен в профиль `archive` и по умолчанию выключен флагом `L4MEDIA_ARCHIVE_WORKER_ENABLED=false`.
   - Запуск `python3 -m archive.worker` на хосте подтверждает:
     `[INFO] [l4media-archive] Worker is DISABLED (L4MEDIA_ARCHIVE_WORKER_ENABLED=false). Exiting cleanly.` (exit code 0).
4. **Сквозная диагностика `check.sh`:**
   Все 8 проверок пройдены успешно (exit code 0), включая порты 8443, логи nginx/ingress/janus, Control API routes & stats, Janus API, mTLS enforcement, lifecycle & regression suite и проверку статуса архиватора `[CHECK 8/8]`.
5. **Smoke-тест на сервере:**
   На сервере проверен полный сценарий генерации пакета, детерминированного сжатия, SHA-256 верификации, атомного перемещения и инспекции:
   ```text
   Smoke batch created: arch-media-2026-04-smoke01 state: purged
   Inspection verified: True
   Retention status: passed
   SMOKE TEST PASSED!
   ```

---

## 7. Готовность к откату (Rollback Readiness)

* При необходимости отката достаточно переключить ветку `l4media` на предыдущий коммит `37adfd01e5492e6b61e8ecb243579389ae2d858e` и выполнить `deploy.sh`.
* Поскольку воркер по умолчанию отключён (`L4MEDIA_ARCHIVE_WORKER_ENABLED=false`), а изменения схемы существующей базы данных отсутствуют, откат не несёт рисков прерывания текущих стримов или потери данных.

---

## 8. Потребители и следующий шаг

* **Выходной handoff:** `H-L4D-15C-MEDIA-v1`.
* **Потребитель:** `L4D-16-MB` (подключение архивных манифестов в MenuBuilder и контроль retention в Хабе).
* **Следующий промпт:** `L4D-16-MB`.
