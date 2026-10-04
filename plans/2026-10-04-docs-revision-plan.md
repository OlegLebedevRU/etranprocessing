# Ревизия документации и план актуализации (срез 2026-10-04, обновлён после Волн 0–1)

**База сверки:** кодовая база ветки `origin/main` (= состояние production, HEAD `9e529c5`).
**Ограничения:** актуальность документации по `tools/` не проверялась (только референс); детальная сверка `l4media/*` и `iot-rpc-rest-app` (app1, вне репозитория) не проводилась.
**Статус:** Волна 0 (синхронизация) и Волна 1 (P0: секреты, quickstart, единый порядок деплоя) — **выполнены**, изменения в рабочей копии, не закоммичены. Волны 2–4 — ниже.

---

## Журнал выполнения

### Волна 0 — Синхронизация (выполнена 2026-10-04)
- Локальная копия синхронизирована с `origin/main` через `git stash push -u` + `git pull --ff-only`.
- Уникальный контент стэша извлечён в рабочую копию: `docs/menu_arch-l4mcp-tenant-skill-library.md`, `docs/term_net-leo4proxy-dns-srv-endpoints.md` (прод-дубли остальных untracked подтверждены и отброшены).
- Стэш `stash@{0}: docs-revision-backup-2026-10-04` сохраняет tools WIP (`tools/leo4proxy/src/policy*`, `tools/l4con/src/event_ipc*`, релизные скрипты, бинарники) — **не дропать** до извлечения tools WIP в отдельную ветку.
- `l4desk-landing/` остался в рабочем дереве (не в стэше) — не тронут.

### Волна 1 — P0 (выполнена 2026-10-04, подтверждения владельца получены)
Подтверждённый флоу деплоя: builder **176.108.247.249 — штатный путь**, триггер ручной адресный; GitHub Actions не существует; миграции — `alembic upgrade head` из нового PB-образа при релизе; digest-фиксация в `compose.yaml` — скриптом релиза.

Выполнено:
1. **Секреты**: [`etran_cert-infrastructure-architecture.md`](../docs/etran_cert-infrastructure-architecture.md) §6.4 (креды MS SQL `ai-agent`) и [`proc_cert-issuance-flow.md`](../docs/proc_cert-issuance-flow.md) (`SIGN_KEY`, CA-URL) заменены плейсхолдерами.
2. **Quickstart**: [`etran_dev-quickstart.md`](../docs/etran_dev-quickstart.md) — DevOps-раздел переписан на release-контракт; «Run migration on server» заменён.
3. **GitHub Actions выпилен**: удалены `.github/workflows/build-image.yml`, `docs/ops_run-github-actions-ci-cd.md`, строка реестра README; [`test_ci.py`](../.github/ci/test_ci.py) — путь заменён на `.github/ci/beta.py`. `.github/ci/*` сохранены (живой код launcher). 32 CI-теста — OK.
4. **Timer выпилен**: удалены `deploy/beta/etran-beta.{service,timer}`; [`install.py`](../deploy/beta/install.py) не устанавливает юниты; [`ops_run-beta-ci-cd.md`](../docs/ops_run-beta-ci-cd.md) актуализирован (ручной триггер, эксплуатация без journalctl/timer, inline-описание отката); карточка [`.agent-context/operations/deployment-invariants.md`](../.agent-context/operations/deployment-invariants.md) обновлена.

**Остатки Волны 1 (за владельцем):**
- ⚠️ **Ротация секретов**: `ai-agent` (MS SQL 172.17.100.1) и `SIGN_KEY` — значения остались в Git-истории.
- ⚠️ **AGENTS.md**: строка «176.108.247.249 удалён из документации деплоя, только по прямому указанию» противоречит подтверждённому штатному пути — требуется правка владельцем (файл правил агентов, вне doc-scope).
- ⚠️ **Сервер builder 176**: установленные `/etc/systemd/system/etran-beta.{service,timer}` (disabled/inactive) — удаление на сервере по протоколу серверных правок с подтверждением.

---

## Волна 2 — P1: фактические расхождения «доки ↔ код» (не начата)

### 2.1. `docs/proc_pay-backend-architecture.md`
- Структура роутеров неполна: нет `devices_legacy.py`, `gate_gauge.py`, `leo4proxy.py`, `list_menu.py`; сервисы: `ca`, `cert_billing`, `cert_discovery`, `companion_cert`, `email_service`, `gauge_engine`, `leo4proxy_policy`, `sn`.
- DevOps-секция: `cd /home/user1/ProcessingBackend && docker compose up -d --build` → единый `/home/user1/compose.yaml`, digest-образы, без build.
- «docker cp migration.py» → Alembic 001–029 (владелец PB; применяется из нового образа при релизе).
- Compose-фрагмент (build, external `iot-rpc-rest-app_pg_network`) не соответствует `compose.yaml`; PostgreSQL 15 → 18; имя контейнера nginx → `nginx-mutual-legacy-nginx-mutual-1`.
- Добавить инвариант auto-bind `cert_serial` (legacy ≤ 20 hex, никогда не перезаписывать 40-символьный new CA serial).

### 2.2. `docs/proc_cert-issuance-flow.md`
- File Structure: `billing.py` и PIN-эндпоинт — в **MenuBuilder**, не в ProcessingBackend; фактический путь `POST /api/billing/terminals/{terminal_id}/certificate-pin`.
- `nginx-mutual-ssl.conf` → `ProcessingBackend/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf`; модели — `shared/etranprocessing_db`.
- Nginx-секция: привести полную матрицу маршрутов (`/certificates/`, `/certificates`, `/certificates/Dispatcher.ashx`, `/api/certificates/pins`, `/api/leo4proxy/policy`).

### 2.3. `docs/ops_net-infrastructure-connections.md` §2.3
Колонки легаси-схемы заменить на актуальные (сверено с `shared/etranprocessing_db/models/`): `org_statuses.status` (varchar active/blocked); `terminals.sn/is_active` (не `device_sn/status_id`); `licenses.expires_at/balance/license_type/billing_period_months` (не `license_until`).

### 2.4. `docs/etran_data-database-ownership.md`
Матрица не включает ~23 таблицы L4Desk/fin ledger (миграция 027) и `l4desk_terminals.paid_until` (029). Владельцы по коду: подписки — MB (`subscriptions.py`, `subscription_payments.py`, `subscription_worker.py`); transport policy — PB (`leo4proxy_policy.py`). Обновить «Проверено».

### 2.5. `docs/ops_run-beta-ci-cd.md`
Таблица production digest'ов (срез 2026-09-29) не совпадает с актуальным `compose.yaml` (264cc058/0a67f0ff/ab09311d): пометить «срез на дату» + ссылка на compose как источник истины. Указать, что l4media-сервисы — отдельный `l4media/compose.yaml`.

### 2.6. `docs/menu_ui-frontend-architecture.md`
Дополнить новыми областями: `routes/licenses|l4desk|console|mcp|settings`, `video-surveillance.tsx`, `api/subscriptions|janusClient|certificate-pin`, `AdminSubscriptionsPanel`, Playwright e2e.

### 2.7. `docs/ops_net-nginx-config-guide.md`
Порты 4443/443 не соответствуют прод-раскладке (mTLS=443 nginx-mutual-legacy; JWT=1443/1444 nginx-default). Перепроверить по `nginx-configs/*` и `legacy_ssl.conf`.

### 2.8. `docs/etran_arch-architecture-analysis.md`
Срез 2026-08-31: нет retired finance API, подписок, YooKassa, SmartCaptcha, событий 900–999, L4Desk/video. Актуализировать или пометить «исторический срез».

### 2.9. Реестр `docs/README.md`
Не зарегистрированы: `etran_arch-remote-input-control.md`, `menu_auth-smartcaptcha.md`, `term_tool-event75-inventory.md`, `legacy-terminal-integration-guide.md`, `terminal-tools-user-guide.md`, подкаталоги `ingress_iot/`, `l4capture/`. Для `ingress_iot/remote-input-protocol.md` определить статус относительно `etran_arch-remote-input-control.md`.

---

## Волна 3 — P2: именование и гигиена (не начата)

1. `legacy-terminal-integration-guide.md` и `terminal-tools-user-guide.md` — нарушают naming convention: переименовать с префиксом или перенести в `docs/history/`.
2. `terminal-tools-user-guide.md` ↔ `term_tool-user-guide.md` — проверить дублирование, слить/архивировать.
3. AGENTS.md ↔ [`ops_run-remote-console-diagnostics.md`](../docs/ops_run-remote-console-diagnostics.md): перечень RPC-методов (AGENTS.md не знает 7004 Keepalive Lease) — синхронизировать (правка AGENTS.md — за владельцем).
4. Судьба извлечённых уникальных доков: `term_net-leo4proxy-dns-srv-endpoints.md` слить с продовым `term_net-leo4proxy-dns-srv-implementation-context.md`; `menu_arch-l4mcp-tenant-skill-library.md` — зарегистрировать или перенести в history (не реализовано в проде).
5. `ops_run-remote-console-diagnostics.md`: убрать ProcessingBackend из перечня бэкендов консоли (консоль = MenuBuilder + app1 + l4con).

---

## Волна 4 — кодовые правки (отдельные задачи, вне doc-scope)

1. `ProcessingBackend/backend/app/dependencies.py` — dead code после `raise HTTPException` в legacy-ветке (недостижимая OU-mismatch-проверка).
2. `compose.yaml` — `RABBITMQ_DEFAULT_USER/PASS: guest/guest` → env-переменные.
3. `legacy_ssl.conf` — `/GateGauge/UpdateScript.ashx` → легаси `46.38.51.114`: задокументировать как временное исключение + план миграции (дока — Волна 2, решение — здесь).
4. Двойная публикация `subscriptions`/`remote_sessions` (`/api/v1` + `/api`) — зафиксировать как официальный compat-контракт в API-доке (дока — Волна 2).

---

## Порядок исполнения и приёмка

| Волна | Состав | Статус | Критерий готовности |
|---|---|---|---|
| 0. Синхронизация | Локальная копия = прод; извлечение уникального контента | ✅ Выполнена | `git status` ревью; стэш сохранён |
| 1. P0 | Секреты, quickstart, единый порядок деплоя, выпиливание GH Actions/timer | ✅ Выполнена | Нет кредов в доках; один путь деплоя; 32 CI-теста OK |
| 1R. Остатки P0 | Ротация секретов; AGENTS.md; юниты на builder | ⏳ За владельцем | Секреты ротированы; противоречий AGENTS.md нет |
| 2. P1-факты | §2.1–2.9 пофайлово | ⬜ Не начата | Каждый факт сверен с HEAD; таблицы/колонки/порты/digest совпадают |
| 3. P2-гигиена | Реестр, именование, дубли, AGENTS.md-синхронизация, судьба уникальных доков | ⬜ Не начата | Все .md зарегистрированы и соответствуют naming convention |
| 4. Кодовые правки | Dead code, rabbitmq-креды, UpdateScript, compat-контракт | ⬜ Отдельные задачи | С тестами (ruff/pyright/pytest по правилам AGENTS.md) |

**Верификация каждой волны:** `git grep` по убранным секретам; сверка таблиц/колонок с `shared/etranprocessing_db/models/`; digest — с `compose.yaml`; для доков «единого источника правды» (remote-console, cert-infrastructure, database-ownership) — перекрёстная проверка ссылок между собой и с AGENTS.md.
