# AGENTS.md — Project Rules

## Agent intake, skills & context (Короткий маршрут)

Перед реализацией определите владельца изменения, контракт взаимодействия,
инварианты безопасности и способ проверки результата.

1. Сначала примените ограничения этого файла и актуального запроса пользователя.
2. Используйте [repo-intake-and-routing](.claude/skills/repo-intake-and-routing/SKILL.md)
   и [индекс контекста](.agent-context/README.md): загрузите только карточку нужного
   компонента и контракты затронутого flow, а не весь каталог.
3. Карточки — сжатый индекс, не источник новых разрешений или доказательство E2E.
   При конфликте с кодом/протоколом отметьте расхождение и уточните требование;
   не ослабляйте безопасность и не расширяйте scope самостоятельно.
4. Для сквозных задач проверьте producer и consumer; для сложных задач оставьте
   [handoff](.agent-context/tasks/handoff-template.md) с выполненными и невыполненными проверками.

Описанные в навыках проверки выполняются только в разрешённом режиме задачи.
Документационная задача не разрешает подключение к брокеру, изменение клиента или деплой.

## Directory Scope & Exclusion Rules (Правила ограничения области папок)

1. **`/FRONT/` and `/BACK/` (Legacy ASP.NET / C#)**:
   - **STRICTLY DO NOT** inspect, search, analyze, or modify files in `/FRONT` and `/BACK` unless the prompt explicitly contains the phrase `"доработка легаси"` or explicitly names these folders (`"FRONT"`, `"BACK"`).
   - *Запрещено просматривать, анализировать и изменять каталоги `/FRONT` и `/BACK`, если в промпте прямо не указано «доработка легаси» или явно не написаны названия этих папок.*
2. **`/tools/` (Auxiliary CLI utilities)**:
   - **STRICTLY DO NOT** inspect, search, analyze, or modify files in `/tools` unless the prompt explicitly mentions `"tools"`.
   - *Запрещено просматривать, анализировать и изменять каталог `/tools`, если в промпте прямо не указано слово «tools».*
3. **`sqlFileExample/` and `stored-procedures/`**:
   - **NEVER** inspect, analyze, consider, or touch files in `sqlFileExample` or `stored-procedures` (or `stored-procedure`) unless these folder names are explicitly written in the prompt.
   - *Каталоги `sqlFileExample` и `stored-procedures` вообще не рассматривать и не трогать, только если напрямую не написаны названия этих папок в промпте.*
4. **Conditional Verification & Testing (Условный пропуск тестов и проверок)**:
   - If changes are confined **ONLY** to documentation (`docs/`, `*.md`), `tools/`, or legacy code (`FRONT/`, `BACK/`) and do **NOT** modify `ProcessingBackend` and/or `MenuBuilder` (or `shared/etranprocessing_db`), **DO NOT** run tests (`pytest`), linters/formatters (`ruff`, `pyright`), or frontend checks (`npm run build`) in `ProcessingBackend` or `MenuBuilder`.
   - Test execution and code quality checks in `ProcessingBackend` / `MenuBuilder` are mandatory **ONLY** when code in those subprojects (or `shared/`) is actually modified.
   - *Если изменяются только документы, `tools` или легаси и это проходит без изменения `ProcessingBackend` и/или `MenuBuilder` (а также `shared`), то НЕ нужно тестировать и проверять код в соответствующих папках/репозиториях.*

## Secrets and credentials

**NEVER** hardcode secrets, API keys, passwords, tokens, database URLs, or internal URLs in any tracked file. This includes:

- **Python source** (`.py`) — no credentials in `os.getenv()` defaults, no hardcoded DB URLs
- **Config files** (`config.py`, `settings.py`) — defaults must be empty or localhost-only
- **Docker / YAML** (`docker-compose.yaml`, `*.yml`) — use `env_file:` or `${VAR}` references, never inline credentials
- **Documentation / skills** (`.md`, `SKILL.md`) — no scripts containing real credentials, passwords, or DB URLs with auth
- **Shell scripts** (`.sh`) — no hardcoded secrets; read from env

All sensitive values must come from environment variables or `.env` files.

### Rules

- Use `os.environ.get("KEY")`, `os.environ["KEY"]`, `pydantic-settings`, or similar mechanisms
- `.env` files must be in `.gitignore` and never committed
- Config classes must use `extra = "ignore"` to tolerate shared `.env` files
- Default values in config should be empty strings or non-sensitive placeholders (e.g. `localhost`, `[]`)
- Database URLs must never contain credentials in code — require the env var
- If you discover a secret in code, remove it immediately and rotate the credential

### Pre-commit / pre-deploy scan

Use the repository's current secret scanner or an equivalent read-only scan for tracked files. Do not encode shell-specific one-off commands or production host patterns in this project-wide file. Review every match; remove real credentials from Git history and rotate them.

## Code quality

Before any commit or deployment, all three checks must pass in every Python subproject that has code changes (not required if changes affect only docs, tools, or legacy per scope rules below):

```bash
uv run ruff check --fix <src_dir>
uv run ruff format <src_dir>
uv run pyright <src_dir>
```

## Python version & Backend Guidelines

All projects target **Python 3.14** (`requires-python = "==3.14.*"`).

Detailed backend code standards, architecture rules, and patterns are documented in **`ProcessingBackend/GUIDELINES.md`**. Key rules include:
- **Shared DB Models (`shared/etranprocessing_db`)**: Single source of truth for all SQLAlchemy 2.0 ORM models for both `ProcessingBackend` and `MenuBuilder`.
- **Thin DB Layer Principle**: `etranprocessing_db` contains *strictly* declarative models, constraints, and relationships. No business logic, auth/crypto utilities, or framework dependencies.
- **FastAPI dependency injection**: `B008` is ignored; use `Depends()` in default argument positions.
- **SQLAlchemy 2.0 Declarative**: Use `Mapped[T] = mapped_column(...)` for models.
- **Python 3.14 Exception Handling**: Prefer `with contextlib.suppress(SpecificException):` over `try...except...pass` (`SIM105`). Never use silent broad `except Exception: pass`. For cleanup/rollback paths, explicitly catch `Exception` and log or document intent (`with contextlib.suppress(Exception):`).

## Project structure & Service Boundaries

| Subproject | Technology / Framework | Role & Service Boundary | Entry point |
|---|---|---|---|
| **`shared/` (`etranprocessing_db`)** | Python 3.14, SQLAlchemy 2.0, asyncpg | Shared thin declarative ORM model layer for both backends (models, constraints, indexes). | `import etranprocessing_db` |
| **`ProcessingBackend/backend`** | Python 3.14, FastAPI, SQLAlchemy (asyncpg), Alembic | Core mTLS payment processing gateway, terminal XML/SOAP handlers (`/api/payment`, `/api/techgate`, `/api/gategauge`, `/api/licensebilling`, `/api/certificates`, `GET /api/ListMenuFile`). Sole authority for Alembic migrations. **No user-facing JWT routes.** | `uvicorn app.main:app` |
| **`MenuBuilder/backend`** | Python 3.14, FastAPI, SQLAlchemy | Tenant & admin web portal, terminal menu management, and **user-facing billing API** (`/api/billing`, `/api/certificate-pin`, `/api/admin/organizations`, JWT authentication). | `uvicorn app.main:app` |
| **`MenuBuilder/frontend`** | React 19, TypeScript, Vite, Ant Design v6 | Web UI for tenant administrators, terminal menu builder, license cart, and admin panels (Code Splitting, Design Tokens, multi-tenant). | `npm run build` / `npm run dev` |
| **`ProcessingBackend/mcp-pin-server`** | Python 3.14, FastMCP / MCP SDK | Model Context Protocol server for PIN operations & certificate tools. | `python -m pin_server.server` |
| **`tools/`** | C (Win32/CNG/CryptoAPI) / Python | Auxiliary CLI utilities for terminals and server management (`leo4proxy`, `mosquitto`, `l4con`, `l4sql`, `l4pin`, `l4superv`, `l4install`). | `tools/` |

## L4 Tools Suite & Terminal Architecture Rules

Comprehensive architectural specifications, orchestration principles, REST-RPC integration protocols, and development guidelines for creating and modifying tools in `tools/` are documented in:
- **[`docs/term_tool-architecture-guide.md`](docs/term_tool-architecture-guide.md)** — Architectural blueprint, orchestration principles, REST-RPC integration flow, and step-by-step guideline for adding new native tools to L4 Suite.
- **[`docs/ops_run-remote-console-diagnostics.md`](docs/ops_run-remote-console-diagnostics.md)** — Remote web console and diagnostic agent protocol, MQTT topic matrix, RPC methods (`7001` Exec, `7002` Cancel, `7003` Ping), and E2E test cases.
- **[`docs/term_tool-user-guide.md`](docs/term_tool-user-guide.md)** — User guide and operational manual for engineers.

### Key Rules for Tools Development (`tools/`):
1. **Isolated Subdirectory Model**: Every tool in `C:\l4tools` resides in its own isolated subfolder (e.g. `C:\l4tools\l4sql\l4sql.exe`, `C:\l4tools\l4con\l4con.exe`).
2. **Zero-Touch PATH & Working Directory**:
   - `l4con` sets default spawned process working directory to `C:\l4tools` (preventing `C:\Windows\system32` leakage) and streams active prompt `C:\l4tools> <command>` to the console.
   - `l4con` and `l4install` enrich the process and system `PATH` with all suite subdirectories, allowing seamless execution without full paths.
3. **Mandatory 32/64-bit Architecture Build**: Always support both x86 (universal for POSReady 7/Windows 7–11) and x64 builds via `build.cmd`.
4. **Zero-Dependency Win32 / C**: Native static `/MT` builds using only standard Windows SDK libraries (`odbc32.lib`, `advapi32.lib`, `user32.lib`, `winhttp.lib`, `ws2_32.lib`, `shlwapi.lib`).
5. **Distribution Packaging**: Any new or updated tool must be staged in `tools/l4superv/pack_zip.cmd` and handled in `tools/l4superv/src/installer_main.c`.

## C / C++ Toolchains & Build Tools (Local Development Machine)

For building native Windows utilities in `tools/` (or examples like `D:\work\iot.leo4.ru\iot-rpc-rest-app\examples\c-win-clion-rpc-client`), two fully functional C/C++ toolchains are available on this machine:

### 1. Mandatory Unified 32/64-bit Architecture Build Policy for `tools/`
- **Mandatory Policy for AI Agents**: Always build native Windows CLI utilities and background services in `tools/` (`tools/leo4proxy`, `tools/terminal-cert-installer`) for **unified 32-bit (x86) and 64-bit (x64) architectures**.
- **Target Compatibility**: 32-bit (x86) builds are critical for payment kiosks/terminals running 32-bit Windows 7 Embedded / POSReady 7, and run seamlessly on 64-bit Windows via WOW64.
- **Output Artifacts**:
  - `bin\x86\<tool>.exe`: 32-bit static binary (`/MT`, pure x86 PE).
  - `bin\x64\<tool>.exe`: 64-bit static binary (`/MT`, x64 PE).
  - `bin\<tool>.exe`: Default universal binary (x86 for maximum compatibility across all terminal machines).
- **Automated Unified Build**:
  Running `build.cmd` (or `build.cmd all`) in any `tools/<subproject>` automatically builds both x86 and x64 targets.

### 2. JetBrains CLion Bundled Toolchain (MinGW-w64 + CMake + Ninja)
- **CMake**: `C:\Program Files\JetBrains\CLion 2025.2.4\bin\cmake\win\x64\bin\cmake.exe` (v4.2.2)
- **GCC / MinGW**: `C:\Program Files\JetBrains\CLion 2025.2.4\bin\mingw\bin\gcc.exe` (GCC 13.1.0 x64)
- **Ninja**: `C:\Program Files\JetBrains\CLion 2025.2.4\bin\ninja\win\x64\ninja.exe` (v1.13.2)
- **Security & Network Libs**: `-lncrypt`, `-lcrypt32`, `-lwinhttp`, `-lsecur32`, `-lws2_32` available out of the box.
- **Example CMake invocation**:
  ```powershell
  & "C:\Program Files\JetBrains\CLion 2025.2.4\bin\cmake\win\x64\bin\cmake.exe" -B build -G Ninja -DCMAKE_C_COMPILER="C:/Program Files/JetBrains/CLion 2025.2.4/bin/mingw/bin/gcc.exe" -DCMAKE_MAKE_PROGRAM="C:/Program Files/JetBrains/CLion 2025.2.4/bin/ninja/win/x64/ninja.exe"
  & "C:\Program Files\JetBrains\CLion 2025.2.4\bin\cmake\win\x64\bin\cmake.exe" --build build --config Release
  ```

### 3. Microsoft Visual C++ Build Tools 2022 (MSVC) + Windows SDK 10
- **Install path**: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`
- **Compiler**: `cl.exe` (v19.44 for x86 & x64)
- **Windows SDK**: 10.0.22621.0 (`ncrypt.lib`, `crypt32.lib`, `secur32.lib`, `ws2_32.lib`, `winhttp.lib`)
- **Environment activation**:
  - x86 (32-bit): `call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"`
  - x64 (64-bit): `call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"`
- **Standalone Static Binary Compilation (`/MT` — zero runtime dependencies)**:
  - Unified Build: `cd tools\<tool_dir> && build.cmd`
  - x86 Build only: `cd tools\<tool_dir> && build.cmd x86`
  - x64 Build only: `cd tools\<tool_dir> && build.cmd x64`

## Infrastructure & Servers

- **Primary Production Server**: `87.242.100.34` (internal IP `10.0.0.5`, user: `user1`, SSH key: `d:\.ssh\id_ed25519`)
  - **ОБЯЗАТЕЛЬНОЕ ПРАВИЛО ДЕПЛОЯ**: Деплой выполняется **ВСЕГДА** на хост `ssh user1@87.242.100.34 -i d:\.ssh\id_ed25519`.
  - **СЕРВЕР `176.108.247.249`**: Сервер `176.108.247.249` **удалён из документации деплоя**. Использование сервера `176.108.247.249` допускается **ТОЛЬКО по прямому указанию в промпте**.
  - Orchestration: `/home/user1/compose.yaml` in Docker network `user1_default`.
  - Runs Docker containers: `processing-backend` (:8000), `menubuilder-backend` (:8000), `nginx-default` (:80, :3000, :1443, :1444), `app1` (:8000), `mcp-pin-server` (:8001), `rabbitmq` (:5672, :8883).
  - External mTLS Proxy: `nginx-mutual-legacy` (:443) managed via `/home/user1/nginx-mutual-legacy/docker-compose.yml`, connected to network `user1_default`. Terminates client mTLS for terminals (`iot-processing.ru`) and locally forwards requests to `http://processing-backend:8000`.
- **Managed Database Server**: `10.0.0.7:5432` (Managed PostgreSQL 18)
  - Databases: `etran` (processing and menubuilder models), `iot_rpc` (IoT platform models).

## Terminal Routing & Proxying (nginx-mutual-legacy)

In `ProcessingBackend/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf`:
- `upstream new_processing_backend` routes to local container `server processing-backend:8000;`.
- Terminal endpoints (`/payment/`, `/payment/etran.ashx`, `/techgate/etran.ashx`, `/api/ListMenuFile`, `/licensebilling/`) proxy directly to `http://new_processing_backend`.
- Legacy fallback for unprocessed routes: `http://46.38.51.114`.
- **Deploying & Reloading Nginx**:
  ```bash
  scp -i d:\.ssh\id_ed25519 ProcessingBackend/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf user1@87.242.100.34:/home/user1/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf
  ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker exec nginx-mutual-legacy-nginx-mutual-1 nginx -t && sudo docker exec nginx-mutual-legacy-nginx-mutual-1 nginx -s reload"
  ```

## Certificate Architecture & Terminal mTLS Rules

Comprehensive documentation for the certificate subsystem, mTLS proxying, native C tools, and verification scripts is in **[`docs/etran_cert-infrastructure-architecture.md`](docs/etran_cert-infrastructure-architecture.md)**.

### Key Rules for Certificate Management:
1. **Serial Number Update Invariant**:
   - `terminals.cert_serial` for new CA certificates (`iot.leo4.ru`, 40 hex chars) is updated **EXCLUSIVELY** in `POST /api/certificates/?function=setup`. No other flow can overwrite it.
   - For legacy certificates (`SubCA`, $\le 20$ chars), `dependencies.py` auto-binds serials if unset or legacy, but will **never** overwrite a 40-character new CA serial.
2. **Dual-Issuer Authentication (`get_current_terminal`)**:
   - **New CA (`iot.leo4.ru`)**: Strict check `Terminal.sn == CN AND Terminal.cert_serial == Serial`. Rejects on mismatch with `401 Unauthorized` (`serial_mismatch`).
   - **Legacy CA**: Lookup by `OU` (`device_id`) and `O` (`org_id`).
3. **Native C Tooling & Windows Schannel**:
   - When generating keys in CNG KSP (`ncrypt.dll`), private keys are non-exportable (`NCRYPT_EXPORT_POLICY_PROPERTY = 0`).
   - In `CRYPT_KEY_PROV_INFO`, `dwKeySpec` **must be `0`** (not `0xFFFFFFFF`) for Windows Schannel / SSPI compatibility (`AcquireCredentialsHandleW`).
4. **PowerShell mTLS Verification**:
   - Use `[System.Net.HttpWebRequest]` with `$req.ClientCertificates.Add($cert)` to test terminal mTLS endpoints directly from Windows (see full scripts in `docs/etran_cert-infrastructure-architecture.md`).
5. **Terminal Verification Registry & Discovery Audit**:
   - The state of all terminals is tracked in real-time by joining `terminal_cert_discovery` + `terminals` + `org_statuses` + `licenses`.
   - When migrating endpoints to the new backend, execute the verification audit script from `docs/etran_cert-infrastructure-architecture.md §6.3` to inspect all connected terminals.
   - Do not turn an audit finding into an automatic data import. Reconciliation requires an explicit migration task, reviewed source data, an idempotent script, and post-migration verification.

## Deployment invariants

Keep only durable invariants here. Use the current runbook under `docs/` for environment-specific hosts, paths and step-by-step commands.

- **`docker` on the server requires `sudo`.** Plain `docker ...` / `docker
  compose ...` fails with "permission denied while trying to connect to the
  Docker daemon socket". Always prefix with `sudo docker ...` /
  `sudo docker compose ...` over SSH.
- **Database migrations (Alembic):** Alembic migrations reside in
  `ProcessingBackend/backend/alembic/`; `ProcessingBackend` is the sole migration owner. Deploy migrations through the standard image/release flow, run `alembic upgrade head`, and roll all consumers of changed shared models to compatible versions.
- **JWT `org_id` claims travel as strings — cast to `int` at the auth
  boundary, once.** Any endpoint that binds an org id into a raw SQL query
  against an integer column will raise `asyncpg.exceptions.DataError` if it
  receives the JWT claim unconverted. Fix it in the shared
  `get_current_user()`/auth dependency, not per-endpoint.
- **Bind-mounted frontend containers don't need a restart after `npm run
  build`.** If `docker-compose.yaml` mounts `dist/` as a live volume (see
  MenuBuilder's `menubuilder-frontend`), rebuilding on the host is sufficient
  — nginx picks up the new files immediately. Only restart the container for
  `nginx.conf`/cert/env changes. Verify by comparing file mtimes inside vs.
  outside the container (watch for timezone offsets between host and
  container when eyeballing timestamps).
- **Authenticated deployment checks:** use dedicated short-lived test credentials issued through an approved test path. Do not generate ad hoc tokens from production secrets or copy executable scratch scripts into running containers.
- **SSH from non-interactive clients:** disable stdin forwarding and keep environment-specific identities and hostnames in the runbook, not in this file.
- **Cleanup:** remove temporary artifacts and test credentials created by the current task from local and remote environments.

## MCP Operations Server (`server-ops`) & DevOps Protocol

An operations MCP server (`server-ops`) is configured for the production server (`87.242.100.34`). It provides tools for system health inspection, log investigation, Nginx and SSL management, and safe command execution.

### Available MCP Ops Capabilities
- **System monitoring**: `mcp_server-ops_system_info`, `mcp_server-ops_memory_analysis`, `mcp_server-ops_disk_analysis`, `mcp_server-ops_service_status`
- **Log search & diagnostics**: `mcp_server-ops_log_search` (filtering by `keyword`, `level`: `error`/`warning`/`info`), `mcp_server-ops_log_read`, `mcp_server-ops_log_search_system` (`journalctl`), `mcp_server-ops_log_list`
- **Nginx & Certbot**: `mcp_server-ops_nginx_config_read`, `mcp_server-ops_nginx_config_test`, `mcp_server-ops_nginx_reload`, `mcp_server-ops_certbot_install`, `mcp_server-ops_certbot_renew`
- **Files & Command execution**: `mcp_server-ops_file_list`, `mcp_server-ops_file_read`, `mcp_server-ops_file_search`, `mcp_server-ops_command_exec` (whitelisted safe commands)
- **Project audit & confirmation**: `mcp_server-ops_project_overview`, `mcp_server-ops_config_audit` (auto-masks secrets), `mcp_server-ops_confirm_execute` (2FA confirmation for mutating actions)

### Mandatory Task Readiness Verification Protocol (Признак готовности к задаче)
Before utilizing MCP Ops capabilities in any debugging, deployment, diagnostics, or configuration task, the agent **MUST** verify readiness and establish an explicit task readiness status:

1. **Connectivity & Resource Pre-flight Check**:
   - Probe server connectivity and load: call `mcp_server-ops_system_info(type="load")` or `mcp_server-ops_service_status(service="nginx")`.
   - Check host resource safety thresholds:
     - Available RAM > 300 MiB (`system_info` / `memory_analysis` — critical since server has 0B Swap).
     - Root disk usage < 90% (`system_info` / `disk_analysis`).
     - Load average within healthy limits (< 2.0).
2. **Readiness Status Declaration & Non-Blocking Policy**:
   - Explicitly record the status in task context / plan:
     - `[MCP Ops Readiness: READY]` — all checks passed; MCP tools may be used for diagnostics, log analysis, and verification.
     - `[MCP Ops Readiness: DEGRADED / UNAVAILABLE]` — MCP unreachable or resource thresholds exceeded; fall back to standard SSH runbook commands (`user1@87.242.100.34`, key `d:\.ssh\id_ed25519`).
   - **CRITICAL: `[MCP Ops Readiness: UNAVAILABLE]` (или `DEGRADED`) НЕ блокирует деплой (Non-Blocking)**. Отсутствие подключения или недоступность MCP Ops сервера не является причиной для отмены или задержки деплоя. Сборка, деплой, применение миграций и верификация в этом случае выполняются в штатном режиме через прямой SSH-транспорт (`scp`, `ssh sudo docker ...`) без блокировок.
3. **Safety Guardrails**:
   - State-changing operations (`nginx_reload`, `file_write`, `file_delete`) return a `confirmationId` and MUST be verified before confirmation via `mcp_server-ops_confirm_execute`.
   - Never inspect raw secret files directly: use `mcp_server-ops_config_audit` for inspecting `.env` configurations.

## MQTT Client Development Rules for AI Agents (Правила разработки MQTT-клиентов)

### Обязательный предварительный опрос типа клиента
Перед началом разработки или изменения любого MQTT-клиента агент **обязан спросить в чате**:

> **"Какой тип MQTT-клиента создаётся: main_app, extra_service или svc_desk?"**

Дальнейшая реализация должна строго зависеть от ответа пользователя.

---

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

---

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

---

### 3. Сценарий для `svc_desk` (Агент удалённого ввода l4desk)

Client CONNECT (только localhost Mosquitto, client_id = svc_desk):
  will_topic   = dev/{SN}/ctl
  will_payload = {"v":1,"type":"presence","agent":"l4desk","status":"offline","desktop_available":false,"timestamp":"<UTC>"}
  will_retain  = true, will_qos = 1

After CONNACK:
  SUBSCRIBE srv/{SN}/ctl (qos 1)
  PUBLISH dev/{SN}/ctl = presence status=online (retain=true, qos=1), затем каждые 30 с

Normal shutdown:
  PUBLISH dev/{SN}/ctl = presence status=offline (retain=true) → DISCONNECT

Запреты: не публиковать в dev/{SN}/svc|app|evt|out|res; команды/ACK/NACK — без retain; только pointer_move/mouse_click(left).
Спецификация протокола: см. [`docs/etran_arch-remote-input-control.md`](docs/etran_arch-remote-input-control.md),
[`docs/etran_arch-video-remote-desktop-e2e.md`](docs/etran_arch-video-remote-desktop-e2e.md)
и [карточку MQTT с известными расхождениями](.agent-context/contracts/mqtt-topic-matrix.md).

---

### 4. Общие требования к реализации MQTT-клиентов

- **Запрет самостоятельного выбора**: Если тип клиента не указан или ответ отличается от `main_app`/`extra_service`/`svc_desk`, агент не должен самостоятельно выбирать тип. Нужно уточнить тип клиента в чате.
- **Блокировка реализации**: Нельзя реализовывать MQTT-клиент без выбранного типа клиента.
- **Обязательность presence-сценария**: Presence/status-сценарий должен быть частью стандартной реализации MQTT-клиента.
- **Строгое соответствие топиков и payload**: Топики и payload должны использоваться строго как указано выше.
- **Запрет произвольных изменений**: Не заменять `app`/`svc`/`ctl` и `app_online`/`app_offline`/`svc_online`/`svc_offline` на другие значения без отдельного согласования.
- **Изоляция топиков**: `svc_desk` нельзя заменять на `extra_service`, так как `dev/{SN}/svc` занят `l4con` (retained last-wins).
- **Проверка существующего кода**: Если в проекте уже есть MQTT-клиент, при его изменении агент должен проверить наличие этого сценария и добавить его при отсутствии.

---

## Remote Diagnostics & Web Console Flow Rules (Правила разработки удалённой консоли и диагностики)

Полная сквозная спецификация архитектуры, взаимодействия компонентов и матрицы топиков зафиксирована в **[`docs/ops_run-remote-console-diagnostics.md`](docs/ops_run-remote-console-diagnostics.md)**.

### 1. Обязательное следование спецификации
Все AI-агенты при любой разработке, рефакторинге, добавлении функционала или отладке компонентов подсистемы удалённой диагностики и веб-консоли:
- Frontend: `MenuBuilder/frontend/src/routes/devices/DeviceConsoleTab.tsx`
- Backend: `MenuBuilder/backend` / `ProcessingBackend`
- Client / Agent: `tools/l4con`
- MQTT Broker / Routing

**ОБЯЗАНЫ** строго следовать правилам протокола `iot-rpc-rest-app` и документу `docs/ops_run-remote-console-diagnostics.md`:
- Строго соблюдать матрицу топиков: Server ➔ Device (`srv/<SN>/tsk`, `srv/<SN>/rsp`), Device ➔ Server (`dev/<SN>/req`, `dev/<SN>/res`, `dev/<SN>/out`, `dev/<SN>/svc`).
- Категорически запрещено создавать произвольные топики (`srv/<SN>/cmd`, `dev/<SN>/ctrl` и т.д.).
- Использовать регламентированные методы `7001` (`CMD_DIAG_EXEC`) и `7002` (`CMD_DIAG_CANCEL`).

### 2. Актуализация спецификации
Обновляйте `docs/ops_run-remote-console-diagnostics.md` в той же задаче, которая намеренно меняет утверждённый протокол, форматы сообщений или ответственность компонентов. Обычная благодарность или подтверждение пользователя не является отдельным запросом на изменение файлов.

---

## Server File Modification & Repository Consistency Rules (Запрет прямого изменения файлов на сервере)

### 1. Категорический запрет прямых изменений на сервере без подтверждения
AI-агентам **СТРОГО ЗАПРЕЩЕНО** напрямую изменять, создавать, удалять или перезаписывать файлы на сервере (через SSH, nano/vim/sed, перенаправления bash/PowerShell, MCP `file_write`/`file_patch`/`file_delete`, `docker cp`, `docker exec` и т.д.) **без предварительного запроса и получения явного подтверждения от пользователя в чате**.

### 2. Принцип единого источника правды (Single Source of Truth)
- **Основной путь внесения изменений**: Все правки исходного кода, конфигурационных файлов, скриптов и документации должны выполняться **исключительно в локальном репозитории**, проходить локальную валидацию/сборку и доставляться на сервер через стандартный регламент деплоя (CI/CD, git push/pull, пересборка контейнеров, регламентированный rsync/scp дистрибутивов).
- Прямое редактирование файлов на сервере приводит к неконсистентности (расхождению) между удаленным (remote) репозиторием, локальной рабочей копией и рантайм-файлами на сервере.

### 3. Обязательный протокол согласования при необходимости серверных правок
Если в исключительных случаях (хотфикс аварии, отладка на живом стенде, специфичные серверные runtime-конфиги) требуется непосредственное изменение файлов на сервере, агент **ОБЯЗАН** перед выполнением действия запросить подтверждение в чате и предоставить:
1. **Причину и обоснование**: Почему изменение необходимо сделать непосредственно на сервере в рантайме, а не штатным деплоем.
2. **Точный diff / состав изменений**: Список затрагиваемых серверных путей и точный текст добавляемых/изменяемых строк.
3. **План обеспечения консистентности (Reconciliation Plan)**:
   - Как именно данное изменение будет немедленно перенесено (backported) в локальный репозиторий.
   - Как локальное изменение будет закоммичено и запушено во внешний репозиторий (Git remote).
   - Как будет гарантировано, что при следующем стандартном деплое серверная runtime-копия и образ/контейнер не перетрут эти изменения и не вернутся к устаревшему состоянию.
4. **Пошаговые команды синхронизации**: Конкретные шаги по синхронизации трёх сущностей: **Внешний репозиторий (remote)** ⟷ **Локальный репозиторий (local)** ⟷ **Рантайм-копия на сервере (runtime)**.

---

## Changelog Maintenance Rules (Ведение CHANGELOG.md)

Поддерживайте `CHANGELOG.md` соответствующего подпроекта для пользовательских, протокольных, архитектурных и других release-significant изменений. Внутренние правки без наблюдаемого эффекта не требуют записи.

Фиксируйте итоговое состояние по разделам `Added`, `Changed`, `Fixed`, `Deprecated`, `Removed`; не превращайте changelog в перечень промежуточных действий или коммитов. Дату и версию финализируйте только в рамках явной release/versioning задачи либо принятого проектом процесса, а не по ключевым словам в сообщениях пользователя.

---

## Documentation Naming Convention & Structure Rules (Правила именования и структуры документации)

Все активные документы в каталоге `docs/` обязаны строго именоваться по стандарту двух мнемокодов с разделителем:
👉 **[`docs/etran_dev-documentation-naming-convention.md`](docs/etran_dev-documentation-naming-convention.md)**

### 1. Формула префикса имени файла
```text
{СФЕРА}_{ФЛОУ}-{дескриптивное-имя}.md
```
- **Мнемокод 1 (`СФЕРА`)**:
  - `etran` — общесистемный контур (сквозная архитектура, общая БД, стандарты платформы)
  - `proc` — подпроект `ProcessingBackend`
  - `menu` — подпроект `MenuBuilder`
  - `term` — терминалы, киоски, утилиты L4 Suite
  - `ops` — DevOps, Nginx, серверная инфраструктура, развертывание
- **Мнемокод 2 (`ФЛОУ`)**:
  - `arch` (архитектура), `data` (БД и модели), `auth` (аутентификация/JWT), `bill` (биллинг), `cert` (сертификаты/mTLS), `pay` (платежный процессинг), `ui` (фронтенд), `tool` (утилиты киосков), `conn` (связь и аудит устройств), `net` (сеть/прокси), `run` (эксплуатация/ранбуки), `dev` (руководства разработчика).

### 2. Обязанности AI-агентов:
1. **Строгое соответствие префиксов**: Любой новый документ технической документации в `docs/` обязан создаваться с префиксом `{СФЕРА}_{ФЛОУ}-`. Создание файлов без префикса запрещено.
2. **Синхронизация с реестром**: Каждый новый или переименованный документ должен быть немедленно зарегистрирован в **[`docs/README.md`](docs/README.md)**.
3. **Изоляция истории активной разработки**: Архивные материалы, промежуточные аудиты и черновики хранятся строго в **`docs/history/`** и не переименовываются по этому стандарту для сохранения исторического контекста.

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
