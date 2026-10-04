# Ревизия документации и план актуализации (срез 2026-10-04, обновлён после Волн 0–1)

**База сверки:** кодовая база ветки `origin/main` (= состояние production, HEAD `9e529c5`).
**Ограничения:** актуальность документации по `tools/` не проверялась (только референс); детальная сверка `l4media/*` и `iot-rpc-rest-app` (app1, вне репозитория) не проводилась.
**Статус:** Волны 0–3 выполнены. Волна 1R (остатки P0) — за владельцем. Волна 4 — отдельные кодовые задачи.

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
- ⚠️ **AGENTS.md**: перечень RPC-методов консоли (7001–7003) не упоминает 7004 Keepalive Lease — правка владельцем.
- ⚠️ **Сервер builder 176**: установленные `/etc/systemd/system/etran-beta.{service,timer}` (disabled/inactive) — удаление на сервере по протоколу серверных правок с подтверждением.

---

## Волна 2 — P1: фактические расхождения «доки ↔ код» (выполнена 2026-10-04)

Сверка с HEAD выполнена explore-ревью routers/services/models/compose/frontend. Основная масса правок уже была в Волнах 0–1; в этой волне закрыты остатки:

1. **`proc_pay-backend-architecture.md`** — роутеры (payment, tech_gate, gate_gauge, licensebilling, certificates, leo4proxy, list_menu, devices_legacy, health) и services (payment_service, ca, cert_billing, cert_discovery, companion_cert, email_service, gauge_engine, leo4proxy_policy, sn) совпадают с кодом; Alembic 001–029; PostgreSQL 18; cert_serial auto-bind; production compose digest без `build`.
2. **`proc_cert-issuance-flow.md`** — billing.py/PIN — MenuBuilder (`POST /api/billing/terminals/{id}/certificate-pin`); `legacy_ssl.conf`; pins на public gateway → `404` (internal `require_service_auth`); `shared/etranprocessing_db`.
3. **`ops_net-infrastructure-connections.md`** — `org_statuses.status`, `terminals.sn/is_active`, `licenses.expires_at/balance/license_type/billing_period_months`.
4. **`etran_data-database-ownership.md`** — L4Desk/fin ledger (027/029); явно: подписки — логика MB без отдельной таблицы (`l4desk_terminals.paid_until`); transport policy — PB `leo4proxy_policy.py` без таблицы.
5. **`ops_run-beta-ci-cd.md`** — digest-таблица = «срез на 2026-09-29», истина — `compose.yaml`; l4media — отдельный `l4media/compose.yaml`.
6. **`menu_ui-frontend-architecture.md`** — routes licenses/l4desk/console/mcp/settings, video-surveillance, janusClient/certificate-pin/subscriptions, AdminSubscriptionsPanel; **исправлено**: vitest + Playwright e2e (`e2e/*.spec.ts`) существуют (ранее ошибочно «нет scripts»).
7. **`ops_net-nginx-config-guide.md`** — mTLS :443 nginx-mutual-legacy; JWT :3000/:1443/:1444 nginx-default.
8. **`etran_arch-architecture-analysis.md`** — помечен как срез 2026-08-31 с list of post-snapshot changes.
9. **`docs/README.md`** — реестр дополнен; статус `ingress_iot/remote-input-protocol.md` определён относительно `etran_arch-remote-input-control.md` (эталон — arch-спецификация).

---

## Волна 3 — P2: именование и гигиена (выполнена 2026-10-04)

1. `legacy-terminal-integration-guide.md`, `terminal-tools-user-guide.md` — **перенесены** в `docs/history/planning-and-research/` (naming convention).
2. `terminal-tools-user-guide.md` ↔ `term_tool-user-guide.md` — дублей не осталось: актуальный `term_tool-user-guide.md` (1.10.2); legacy ZIP-маршрут в history.
3. AGENTS.md ↔ 7004 — **за владельцем** (см. остатки Волны 1).
4. `term_net-leo4proxy-dns-srv-endpoints.md` — помечен как исторический план с ссылкой на `term_net-leo4proxy-dns-srv-implementation-context.md` + reliability matrix. `menu_arch-l4mcp-tenant-skill-library.md` — **перенесён** в history (design, не реализован).
5. `ops_run-remote-console-diagnostics.md` — убран ProcessingBackend из перечня бэкендов консоли (консоль = MenuBuilder + app1 + l4con).

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
| 1R. Остатки P0 | Ротация секретов; AGENTS.md (176/7004); юниты на builder | ⏳ За владельцем | Секреты ротированы; противоречий AGENTS.md нет |
| 2. P1-факты | §2.1–2.9 пофайлово | ✅ Выполнена | Каждый факт сверен с HEAD; таблицы/колонки/порты/digest совпадают |
| 3. P2-гигиена | Реестр, именование, дубли, судьба уникальных доков | ✅ Выполнена | Все .md в реестре; naming convention (без правок AGENTS.md) |
| 4. Кодовые правки | Dead code, rabbitmq-креды, UpdateScript, compat-контракт | ⬜ Отдельные задачи | С тестами (ruff/pyright/pytest по правилам AGENTS.md) |

**Верификация каждой волны:** `git grep` по убранным секретам; сверка таблиц/колонок с `shared/etranprocessing_db/models/`; digest — с `compose.yaml`; для доков «единого источника правды» (remote-console, cert-infrastructure, database-ownership) — перекрёстная проверка ссылок между собой и с AGENTS.md.
