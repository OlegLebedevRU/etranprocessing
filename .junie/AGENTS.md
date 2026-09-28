# Project Guidelines & Overview

## 1. Project Overview

**etranprocessing** is a payment processing platform and terminal management ecosystem. It handles payment requests from self-service payment kiosks/terminals, verifies client certificates and authentication, executes routing and balance accounting, and provides terminal menu configuration and administrative tools.

The platform is transitioning from a legacy ASP.NET / Microsoft SQL Server architecture to a modern asynchronous Python (FastAPI) / PostgreSQL stack with mutual TLS termination at Nginx.

---

## 2. Architecture & Request Flow

```
Payment Terminal (Mutual TLS HTTPS :4443 / legacy :443)
        │
        ▼
Nginx Reverse Proxy (SSL / Client Cert validation)
  - Validates client certificate
  - Forwards identity headers (X-Client-Cert-DN, X-Client-Cert-Serial, etc.)
        │
        ▼
ProcessingBackend (FastAPI + SQLAlchemy + asyncpg)
  - Authenticates terminal from cert headers
  - Handles payment / tech requests, terminal license checks & menus (ListMenuFile)
  - Persists payments and updates balances
        │
        ▼
PostgreSQL Database
        ▲
        │
MenuBuilder (FastAPI + React frontend + JWT auth)
  - User and Tenant Admin Web UI (:443 -> menubuilder-backend:8000)
  - Terminal menu builder, categories, items
  - User-facing billing API (/api/billing, /api/certificate-pin)
```

---

## 3. Repository Structure & Subprojects

| Directory / Subproject | Technology / Framework | Description | Entry Point / Key Files |
|---|---|---|---|
| **`shared/` (`etranprocessing_db`)** | Python 3.14, SQLAlchemy 2.0, asyncpg | Shared thin declarative ORM model layer for both backends (23 unified models, constraints, indexes). | `import etranprocessing_db` |
| **`ProcessingBackend/backend`** | Python 3.14, FastAPI, SQLAlchemy (asyncpg), Alembic | Core mTLS payment processing REST/XML API (payments, tech gate, license billing check, balance tracking, terminal menus `GET /api/ListMenuFile`, sole authority for Alembic migrations) | `uvicorn app.main:app` |
| **`ProcessingBackend/mcp-pin-server`** | Python 3.14, FastMCP / MCP SDK | Model Context Protocol server for PIN operations & certificate tools | `python -m pin_server.server` |
| **`MenuBuilder/backend`** | Python 3.14, FastAPI, SQLAlchemy | Tenant/admin portal, terminal menu management, and **user billing API** (`/api/billing`, `/api/certificate-pin`, `/api/admin/organizations`) | `uvicorn app.main:app` |
| **`MenuBuilder/frontend`** | React 19, TypeScript, Vite, Ant Design v6 | Web UI for configuring terminal payment menus, license cart, organizations, and billing (Code Splitting, Design Tokens, multi-tenant) | `npm run build` / `npm run dev` |
| **`BACK/`** | Legacy C#, ASP.NET (.NET Framework) | Legacy processing core (`ProcessingCore/EtranDispatcher`, SOAP processors) | `Global.asax`, `EtranDispatcher.asmx` |
| **`FRONT/`** | Legacy ASP.NET | Legacy front-facing web apps (`TechGate`, `GateGauge`, `licensebilling`, `Certificates`) | Web endpoints |
| **`CommonLibs/`** | Legacy .NET Framework | Shared legacy C# utility libraries and DLLs | Visual Studio Solution |
| **`GateYandexMoney/`** | Legacy C# | External payment gateway integration for Yandex.Money | Web services |
| **`migrate/`** | Python / SQL scripts | Database migration utilities for transitioning MSSQL data to PostgreSQL | Migration scripts |
| **`stored-procedures/`** | T-SQL | Legacy MSSQL stored procedures for business logic, routing, and ledger tracking | `.sql` scripts |
| **`docs/`** | Markdown | Comprehensive architecture guides, UX requirements (`docs/billing-cart-ux-requirements.md`), DevOps runbooks | `docs/` |

---

## 4. Development & Code Quality Guidelines

### Python Standards & Backend Guidelines
- **Python Version**: Target **Python 3.14** (`requires-python = "==3.14.*"`).
- **Backend Guidelines**: Comprehensive standards are defined in **`ProcessingBackend/GUIDELINES.md`**.
- **Shared DB Models (`shared/etranprocessing_db`)**: Single source of truth for all SQLAlchemy 2.0 ORM models for both `ProcessingBackend` and `MenuBuilder`.
- **Thin DB Layer Principle**: `etranprocessing_db` contains *strictly* declarative models, constraints, and relationships. No business logic, auth/crypto utilities, or framework dependencies.
- **Package Manager**: Use `uv` for dependency management and running tools.
- **Linters & Formatters**: Before committing or deploying, ensure all three checks pass in Python subprojects that have code changes (see Section 9 for conditional testing rules):
  ```bash
  uv run ruff check --fix <src_dir>
  uv run ruff format <src_dir>
  uv run pyright <src_dir>
  ```
- **Async Best Practices**: Use asynchronous SQLAlchemy 2.0 sessions (`AsyncSession`), `select()` syntax with eager loading (`selectinload`), and avoid blocking synchronous I/O. Models use `Mapped[T] = mapped_column(...)`.
- **FastAPI Dependency Injection**: `B008` is ignored; use `Depends()` in default argument positions.
- **Python 3.14 Exception Handling**: Prefer `with contextlib.suppress(SpecificException):` over `try...except...pass` (enforces Ruff rule `SIM105`). Never use silent broad `except Exception: pass`. For cleanup/rollback paths, catch `Exception` explicitly and log or document intent (`with contextlib.suppress(Exception):`).

### Frontend Standards (MenuBuilder/frontend)
- Use TypeScript with strict typing.
- Run `npm run build` to verify production bundle creation.

---

## 5. Security & Secrets Policy

**NEVER** commit or hardcode secrets, passwords, database credentials, internal tokens, or production API keys.

1. **Environment Variables**:
   - Store secrets only in `.env` files (never committed to git) or pass them via environment variables.
   - Pydantic Settings classes must use `extra = "ignore"` to tolerate shared `.env` files.
   - Default values for configuration keys must remain empty strings or non-sensitive local defaults (`localhost`, `[]`).
2. **Database Credentials**:
   - Database connection URLs in code must never contain hardcoded username/password credentials.
3. **JWT Claims & Auth**:
   - `org_id` claims inside JWT tokens may arrive as strings — always cast to `int` at the authentication boundary (`get_current_user`).

---

## 6. Testing & Verification

- Run test suites using `uv run pytest` in the respective subproject directories (`ProcessingBackend/backend`, `MenuBuilder/backend`) when those subprojects (or `shared/`) are modified (skipped for doc-only, tools-only, or legacy-only changes per Section 9).
- When writing tests for authenticated routes:
  - Generate short-lived mock JWT tokens using the application's internal token generator (`create_access_token`).
  - Use simulated certificate headers (`X-Client-Cert-DN`, `X-Client-Cert-Serial`) when testing terminal-authenticated endpoints.
- Clean up any temporary test scripts, scratch SQL files, or test tokens after verification.

---

## 7. Deployment & Operations

- **Infrastructure & Hosts**:
  - **Production Deploy Server**: `87.242.100.34` (user `user1`, SSH key `d:\.ssh\id_ed25519`).
    - **ОБЯЗАТЕЛЬНОЕ ПРАВИЛО ДЕПЛОЯ**: Деплой выполняется **ВСЕГДА** на хост `ssh user1@87.242.100.34 -i d:\.ssh\id_ed25519`.
    - **СЕРВЕР `176.108.247.249`**: Сервер `176.108.247.249` **удалён из документации деплоя**. Использование сервера `176.108.247.249` допускается **ТОЛЬКО по прямому указанию в промпте**.
    - Хост `87.242.100.34` содержит все рабочие сервисы: `processing-backend`, `menubuilder-backend`, `nginx-default` (порт 3000, раздача SPA `MenuBuilder/frontend/dist`), `mcp-pin-server`, `l4media-ingress`, `l4media-nginx`, `l4media-janus`, `nginx-mutual-legacy` (порт 443), `rabbitmq`, `app1`.
- **Container Orchestration**: Docker Compose используется для оркестрации сервисов (`/home/user1/compose.yaml`). Команды `docker` и `docker compose` выполняются через `sudo`.
- **Database Migrations (Alembic)**:
  - Миграции расположены в `ProcessingBackend/backend/alembic/versions/`.
  - В продакшене применяются командой: `ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker exec processing-backend alembic upgrade head"`.
  - Если изменения схемы затрагивают общие модели, перезапустить `menubuilder-backend`: `ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker restart menubuilder-backend"`.
- **Frontend Live Mounts & Delivery**: `nginx-default` монтирует локальный каталог `/home/user1/MenuBuilder/frontend/dist/`. Сборка выполняется локально (`npm --prefix MenuBuilder/frontend run build`), доставка артефактов на хост:
  ```bash
  scp -i d:\.ssh\id_ed25519 -r MenuBuilder/frontend/dist/* user1@87.242.100.34:/home/user1/MenuBuilder/frontend/dist/
  ```
  Файлы обновляются на лету без необходимости перезапуска контейнера (перезапуск `nginx-default` требуется только при изменениях в `nginx-configs/`).
- **SSH execution from Windows PowerShell:** При выполнении удалённых команд через `ssh` из PowerShell обязательно указывать флаг `-n` (`ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 ...`) для исключения зависаний дескриптора ввода (stdin handle blocking).
- **Legacy IIS Deployments**: Эндпоинты легаси ASP.NET используют модель динамической компиляции App_Code (no-compile deployment) согласно `docs/ops_run-devops-runbook.md`.

---

## 8. MCP Operations Server (`server-ops`) & Task Readiness Protocol

Операционный MCP-сервер (`server-ops`) обеспечивает мониторинг состояния системы, анализ логов, операции с Nginx/SSL и безопасную диагностику сервисов на хосте развертывания (`87.242.100.34`).

### Available MCP Ops Capabilities
- **System monitoring**: `mcp_server-ops_system_info`, `mcp_server-ops_memory_analysis`, `mcp_server-ops_disk_analysis`, `mcp_server-ops_service_status`
- **Log search & diagnostics**: `mcp_server-ops_log_search` (filtering by `keyword`, `level`: `error`/`warning`/`info`), `mcp_server-ops_log_read`, `mcp_server-ops_log_search_system` (`journalctl`), `mcp_server-ops_log_list`
- **Nginx & Certbot**: `mcp_server-ops_nginx_config_read`, `mcp_server-ops_nginx_config_test`, `mcp_server-ops_nginx_reload`, `mcp_server-ops_certbot_install`, `mcp_server-ops_certbot_renew`
- **Files & Safe execution**: `mcp_server-ops_file_list`, `mcp_server-ops_file_read`, `mcp_server-ops_file_search`, `mcp_server-ops_command_exec`
- **Project audit & confirmation**: `mcp_server-ops_project_overview`, `mcp_server-ops_config_audit` (auto-masks secrets), `mcp_server-ops_confirm_execute`

### Mandatory Task Readiness Verification Protocol (Признак готовности к задаче)
Before using MCP Ops capabilities in any task (debugging, deployment verification, diagnostics, or configuration change), the agent **MUST** verify readiness and establish an explicit task readiness status:

1. **Connectivity & Resource Pre-flight Check**:
   - Probe server connectivity and load: execute `mcp_server-ops_system_info(type="load")` or `mcp_server-ops_service_status(service="nginx")`.
   - Check host resource safety thresholds:
     - Available RAM > 300 MiB (`system_info` / `memory_analysis` — critical since server has 0B Swap).
     - Root disk usage < 90% (`system_info` / `disk_analysis`).
     - Load average within healthy limits (< 2.0).
2. **Readiness Status Declaration & Non-Blocking Policy**:
   - Explicitly record the status in the task plan / context:
     - `[MCP Ops Readiness: READY]` — all checks passed; MCP tools may be used for diagnostics, log analysis, and verification.
     - `[MCP Ops Readiness: DEGRADED / UNAVAILABLE]` — MCP unreachable or resource limits exceeded; fall back to standard SSH runbook commands (`user1@87.242.100.34`, key `d:\.ssh\id_ed25519`).
   - **CRITICAL: `[MCP Ops Readiness: UNAVAILABLE]` (или `DEGRADED`) НЕ блокирует деплой (Non-Blocking)**. Отсутствие подключения или недоступность MCP Ops сервера не является причиной для отмены или задержки деплоя. Сборка, деплой, применение миграций и верификация в этом случае выполняются в штатном режиме через прямой SSH-транспорт (`scp -i d:\.ssh\id_ed25519 ...`, `ssh -n -i d:\.ssh\id_ed25519 sudo docker ...`) без блокировок.
3. **Safety Guardrails**:
   - State-changing operations (`nginx_reload`, `file_write`, `file_delete`) return a `confirmationId` and MUST be explicitly confirmed via `mcp_server-ops_confirm_execute`.
   - Never inspect raw secret files directly: use `mcp_server-ops_config_audit` for inspecting `.env` configurations.

---

## 9. AI Agent Scope Restrictions & Conditional Verification

### Directory Access & Inspection Constraints (Ограничения видимости и изменений)
1. **`/FRONT/` and `/BACK/` (Legacy ASP.NET / C#)**:
   - **STRICTLY DO NOT** inspect, search, analyze, or modify files in `/FRONT` and `/BACK` unless the prompt explicitly contains the phrase `"доработка легаси"` or explicitly names these folders (`"FRONT"`, `"BACK"`).
   - *Запрещено просматривать, анализировать и изменять каталоги `/FRONT` и `/BACK`, если в промпте прямо не указано «доработка легаси» или явно не написаны названия этих папок.*
2. **`/tools/` (Auxiliary CLI utilities)**:
   - **STRICTLY DO NOT** inspect, search, analyze, or modify files in `/tools` unless the prompt explicitly mentions `"tools"`.
   - *Запрещено просматривать, анализировать и изменять каталог `/tools`, если в промпте прямо не указано слово «tools».*
3. **`sqlFileExample/` and `stored-procedures/`**:
   - **NEVER** inspect, analyze, consider, or touch files in `sqlFileExample` or `stored-procedures` (or `stored-procedure`) unless these folder names are explicitly written in the prompt.
   - *Каталоги `sqlFileExample` и `stored-procedures` вообще не рассматривать и не трогать, только если напрямую не написаны названия этих папок в промпте.*

### Conditional Verification & Testing (Условный пропуск тестов и проверок)
4. **Skipping Tests/Linters for Doc, Tools, or Legacy Changes**:
   - If changes are confined **ONLY** to documentation (`docs/`, `*.md`), `tools/`, or legacy code (`FRONT/`, `BACK/`) and do **NOT** modify `ProcessingBackend` and/or `MenuBuilder` (or `shared/etranprocessing_db`), **DO NOT** run tests (`pytest`), linters/formatters (`ruff`, `pyright`), or frontend checks (`npm run build`) in `ProcessingBackend` or `MenuBuilder`.
   - Test execution and code quality checks in `ProcessingBackend` / `MenuBuilder` are mandatory **ONLY** when code in those subprojects (or `shared/`) is actually modified.
   - *Если изменяются только документы, `tools` или легаси и это проходит без изменения `ProcessingBackend` и/или `MenuBuilder` (а также `shared`), то НЕ нужно тестировать и проверять код в соответствующих папках/репозиториях.*

---

## 10. MQTT Client Development Rules (Правила разработки MQTT-клиентов)

### Mandatory Initial Clarification / Обязательный опрос типа клиента
Перед началом разработки или изменения любого MQTT-клиента агент **обязан спросить в чате**:

> **"Какой тип MQTT-клиента создаётся: main_app или extra_service?"**

Дальнейшая реализация должна строго зависеть от ответа пользователя.

### 1. Сценарий для `main_app` (Основное приложение)
Если пользователь отвечает `main_app`, агент обязан применить дополнительный сценарий MQTT presence/status для основного приложения:

```
Client CONNECT:
  will_topic = dev/{SN}/app
  will_payload = app_offline
  will_retain = true

After CONNACK:
  PUBLISH dev/{SN}/app = app_online, retain = true

Normal shutdown:
  PUBLISH dev/{SN}/app = app_offline, retain = true
  DISCONNECT
```

**Требования для `main_app`**:
- `{SN}` должен быть заменён на серийный номер устройства/терминала.
- Will Message должен задаваться до подключения клиента к брокеру.
- Публикация `app_online` должна выполняться только после успешного `CONNACK`.
- При штатном завершении клиент должен сначала опубликовать `app_offline` с `retain=true`, затем выполнить корректный `DISCONNECT`.
- При аварийном отключении брокер должен сам опубликовать `app_offline` через Will Message.
- Retain должен быть включён для online/offline статуса.

### 2. Сценарий для `extra_service` (Вспомогательный сервис)
Если пользователь отвечает `extra_service`, агент обязан применить дополнительный сценарий MQTT presence/status для вспомогательного сервиса:

```
Client CONNECT:
  will_topic = dev/{SN}/svc
  will_payload = svc_offline
  will_retain = true

After CONNACK:
  PUBLISH dev/{SN}/svc = svc_online, retain = true

Normal shutdown:
  PUBLISH dev/{SN}/svc = svc_offline, retain = true
  DISCONNECT
```

**Требования для `extra_service`**:
- `{SN}` должен быть заменён на серийный номер устройства/терминала.
- Will Message должен задаваться до подключения клиента к брокеру.
- Публикация `svc_online` должна выполняться только после успешного `CONNACK`.
- При штатном завершении клиент должен сначала опубликовать `svc_offline` с `retain=true`, затем выполнить корректный `DISCONNECT`.
- При аварийном отключении брокер должен сам опубликовать `svc_offline` через Will Message.
- Retain должен быть включён для online/offline статуса.

### 3. Общие требования к реализации MQTT-клиентов
- **Запрет самостоятельного выбора**: Если тип клиента не указан или ответ отличается от `main_app`/`extra_service`, агент не должен самостоятельно выбирать тип. Нужно уточнить тип клиента в чате.
- **Блокировка реализации**: Нельзя реализовывать MQTT-клиент без выбранного типа клиента.
- **Обязательность presence-сценария**: Presence/status-сценарий должен быть частью стандартной реализации MQTT-клиента.
- **Строгое соответствие топиков и payload**: Топики и payload должны использоваться строго как указано выше.
- **Запрет произвольных изменений**: Не заменять `app`/`svc` и `app_online`/`app_offline`/`svc_online`/`svc_offline` на другие значения без отдельного согласования.
- **Проверка существующего кода**: Если в проекте уже есть MQTT-клиент, при его изменении агент должен проверить наличие этого сценария и добавить его при отсутствии.

---

## IDE Integration

Always use the `jetbrains-index` MCP server when applicable for:
- **Finding references** — Use `ide_find_references` instead of grep/search
- **Go to definition** — Use `ide_find_definition` for accurate navigation
- **Renaming symbols** — Use `ide_refactor_rename` for safe, project-wide renames
- **Type hierarchy** — Use `ide_type_hierarchy` to understand class relationships
- **Finding implementations** — Use `ide_find_implementations` for interfaces/abstract classes
- **Diagnostics** — Use `ide_diagnostics` to check for code problems

The IDE's semantic understanding is far more accurate than text-based search. Prefer IDE tools over grep, ripgrep, or manual file searching when working with code symbols.
