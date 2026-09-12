# PROMPT AGENT — шаг 5 / задание 5: локальный инструмент публикации релиза `l4tools` в публичный Generic-реестр `l4tools-generic.ar.cloud.ru`

Ты DevOps/Release инженер: Python 3.14, `uv`, ruff/pyright. Разрешенные файлы: `deploy/publish_l4tools.py`, `deploy/tests/test_publish_l4tools.py`, новая директория `artifacts/l4tools/`, `releases.jsonl` (только добавление строк), `docs/ops_run-beta-ci-cd.md` (раздел про публикацию `l4tools`). **Не менять** Docker-компоненты, `deploy.py` для серверных образов, `nginx-configs/`, ничего на серверах `176.108.247.249` и `87.242.100.34`, терминальные утилиты.

Прочитай [общий overview](prompt_step5_stacks_overview.md): 2.2 (факты о реестре), 4.5 (контракт релиза), 6 (владелец: ключ реестра, имя версии). План: раздел 7.1 (API реестра), 7.4 (раскладка публикации), 7.5 (что не делается).

## 1. Факты

- **Отказ от промежуточного сервера:** сборка бинарников `l4tools` (`l4setup.exe`, `l4tools-release.json`, `SHA256SUMS`) выполняется локально на Windows-машине (задание 4). Промежуточный сервер `176.108.247.249` на этом этапе исключен: сборка на нем не происходит, отправлять файлы через `scp` в `inbox` и настраивать демоны polling не требуется.
- **Прямая публикация:** выкладка собранных артефактов осуществляется напрямую с машины релиз-инженера в Generic-реестр с помощью локального CLI-инструмента `deploy/publish_l4tools.py`.
- **Реестр:** `GET https://l4tools-generic.ar.cloud.ru/<path>` анонимно; `HEAD` → `digest: sha-256=<hex>` (RFC 3230) и `ETag`; `PUT https://<key_id>:<key_secret>@l4tools-generic.ar.cloud.ru/upload/<path>`; перезапись имени запрещена (ожидать 4xx на повтор — реестр иммутабелен); `%` в имени запрещен; Range не поддерживается; в корне только `ar-root.md` (не трогать).
- **Секреты:** ключ реестра выдает владелец и сохраняет локально в переменных окружения или `.env` (`AR_GENERIC_KEY_ID`, `AR_GENERIC_KEY_SECRET`); в Git, логах, чате, `artifacts/*.json` секретов быть не должно (маскируются как `***`). До получения ключа — только dry-run.

## 2. Задачи

1. **`deploy/publish_l4tools.py`** (Python 3.14, stdlib `urllib`/`hashlib`/`json`, без внешних зависимостей): подкоманды
   - `verify <dir>`: проверяет наличие трех файлов (`l4setup.exe`, `SHA256SUMS`, `l4tools-release.json`), сверяет хэши по `SHA256SUMS`, соответствие `files.l4setup.exe.sha256` в манифесте, `version` в манифесте, `dirty == false` (иначе отказ; флаг `--allow-dirty` только для beta).
   - `check <version>`: `HEAD /l4tools/<version>/l4setup.exe` анонимно → если 200 — «already published», выход `2` **до** любых `PUT`; если 404 — «not published», выход `0`.
   - `publish <dir> [--dry-run] [--allow-dirty]`: выполняет сквозной процесс:
     1. Локальная верификация файлов (`verify`).
     2. Проверка реестра на отсутствие версии (`check`), при наличии — немедленный выход `2`.
     3. Загрузка через HTTP `PUT /upload/l4tools/<version>/<file>`: порядок — `l4setup.exe`, затем `SHA256SUMS`, и последним `l4tools-release.json` как признак завершенности релиза.
     4. После каждого `PUT` — анонимный `HEAD` и сверка RFC 3230 `digest: sha-256` с локальным SHA-256; несовпадение → аварийный выход `3` с указанием файла.
     5. Фиксация релиза (`record`).
   - `record <dir>`: сохранение метаданных в `artifacts/l4tools/<version>.json` (`version`, `git_sha`, `published_at`, `urls{}`, `sha256{}`, `size{}`, `registry_digest_verified: true`, `signed: false`, `publisher: "publish_l4tools-cli"`) и дозапись строки в `releases.jsonl` (если файл существует или указан).
   - Секреты — только из env/`.env`; в консоли и логах URL печатаются без credentials, секреты маскируются как `***`.
2. **Retry и таймауты:** `PUT` 30 МБ — таймаут 300 с, 3 попытки только на сетевых ошибках и 5xx; на клиентских ошибках 4xx не повторять.
3. **Документация:** раздел в документации с описанием использования утилиты `deploy/publish_l4tools.py`, предварительных условий (ключи в окружении/`.env`), кодов выхода (0 — успех, 1 — ошибка валидации/сети, 2 — версия уже существует, 3 — расхождение дайджеста).

## 3. Проверки

- Локально: `uv run ruff check deploy`, `uv run ruff format deploy` — без ошибок.
- Unit-тесты (`deploy/tests/test_publish_l4tools.py`, `python -m unittest`):
  - `verify` на фикстурном каталоге (валидный / битый sha / dirty / несовпадающий хэш манифеста / отсутствие файлов).
  - `check` с замоканным `HEAD` 200/404/ошибка сети.
  - `publish --dry-run` моделирует план без сетевых вызовов.
  - Сверка RFC 3230 `digest` (совпал/не совпал, обработка выхода `3`).
  - Маскировка секрета в логах и URL.
  - Запись `artifacts/l4tools/<version>.json` и строки `releases.jsonl`.
- Интеграция с реальным реестром **без ключа**:
  - `HEAD https://l4tools-generic.ar.cloud.ru/ar-root.md` → есть `digest: sha-256=`.
  - `check 1.6.0` → 404 → «not published».
- Реальная публикация — **после** того, как владелец предоставит ключ `AR_GENERIC_KEY_ID`, `AR_GENERIC_KEY_SECRET` и подтвердит версию `1.6.0`:
  - Выполнить `publish tools/dist/` один раз.
  - Анонимный `HEAD`/`curl -I` каждого из трех файлов → `digest` совпадает с `SHA256SUMS`.
  - Повторный запуск `publish` → выход `2` без `PUT`.

## 4. Результат

Верни: `publish_l4tools.py`, тесты и их вывод, результат линтеров, транскрипт dry-run публикации (URL без credentials, коды, `digest`), сгенерированный `artifacts/l4tools/<version>.json`, запись в `releases.jsonl`, список долгов (Ed25519 `.sig` и серверные манифесты — Этап 2; Windows-раннер — Этап 4).
