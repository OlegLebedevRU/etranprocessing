# AGENTS.md — Project Rules

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

```bash
# DB URLs with credentials in any tracked file
grep -rn "postgresql://.*:.*@" --include="*.py" --include="*.yaml" --include="*.yml" --include="*.toml" --include="*.md"

# Hardcoded non-empty secret defaults
grep -rn 'os\.getenv(.*,\s*"[^"]\{8,\}")' --include="*.py"

# Production URLs in code (not in .env)
grep -rn "https://dev\.\|https://api\.\|https://prod\." --include="*.py"
```

Zero matches expected. If found, move to `.env`.

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
| **`shared/` (`etranprocessing_db`)** | Python 3.14, SQLAlchemy 2.0, asyncpg | Shared thin declarative ORM model layer for both backends (23 unified models, constraints, indexes). | `import etranprocessing_db` |
| **`ProcessingBackend/backend`** | Python 3.14, FastAPI, SQLAlchemy (asyncpg), Alembic | Core mTLS payment processing gateway, terminal XML/SOAP handlers (`/api/payment`, `/api/techgate`, `/api/gategauge`, `/api/licensebilling`, `/api/certificates`, `GET /api/ListMenuFile`). Sole authority for Alembic migrations. **No user-facing JWT routes.** | `uvicorn app.main:app` |
| **`MenuBuilder/backend`** | Python 3.14, FastAPI, SQLAlchemy | Tenant & admin web portal, terminal menu management, and **user-facing billing API** (`/api/billing`, `/api/certificate-pin`, `/api/admin/organizations`, JWT authentication). | `uvicorn app.main:app` |
| **`MenuBuilder/frontend`** | React 19, TypeScript, Vite, Ant Design v6 | Web UI for tenant administrators, terminal menu builder, license cart, and admin panels (Code Splitting, Design Tokens, multi-tenant). | `npm run build` / `npm run dev` |
| **`ProcessingBackend/mcp-pin-server`** | Python 3.14, FastMCP / MCP SDK | Model Context Protocol server for PIN operations & certificate tools. | `python -m pin_server.server` |
| **`tools/`** | C (Win32/CNG/CryptoAPI) / Python | Auxiliary CLI utilities for terminals and server management. | `tools/` |

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

- **Primary Application Server**: `176.108.247.249` (user: `user1`, SSH key: `d:\.ssh\free-tier-cloud_ru`)
  - Runs Docker containers: `processing-backend`, `menubuilder-backend`, `menubuilder-frontend`, `mcp-pin-server`, `postgres`, `rabbitmq`, etc.
  - Reverse proxy Nginx on port 443 routes `/api/billing/` and admin/portal routes to `menubuilder-backend:8000`, and terminal endpoints to `processing-backend:8000`.
- **Legacy mTLS Reverse Proxy Server**: `87.242.100.34` (user: `user1`, SSH key: `d:\.ssh\id_ed25519`)
  - Runs `nginx-mutual-legacy` (terminates client mTLS and forwards requests to `176.108.247.249`).

## Selective Endpoint Switching (Legacy vs New Backend)

In `ProcessingBackend/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf`, individual terminal endpoints can be switched between the legacy backend (`http://46.38.51.114`) and the new backend (`https://new_processing_backend/api/...` -> `176.108.247.249:4443`):

1. **Active switched endpoints**:
   - `/certificates/` -> `https://new_processing_backend/api/certificates/`
   - `/licensebilling/` -> `https://new_processing_backend/api/licensebilling/`
2. **Switching an endpoint to New Backend**:
   - Change `proxy_pass http://46.38.51.114/<endpoint>` to `proxy_pass https://new_processing_backend/api/<endpoint>`.
   - Add `proxy_ssl_verify off;`.
   - If the endpoint had mirror directives (`mirror /_mirror_...`), disable or comment them out.
3. **Rollback to Legacy Backend**:
   - Change `proxy_pass https://new_processing_backend/api/<endpoint>` back to `proxy_pass http://46.38.51.114/<endpoint>`.
   - Remove `proxy_ssl_verify off;`.
   - Re-enable mirror directives if needed.
4. **Deploying & Reloading Nginx**:
   ```bash
   scp -i d:\.ssh\id_ed25519 ProcessingBackend/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf user1@87.242.100.34:/home/user1/nginx-mutual-legacy/nginx-configs/legacy_ssl.conf
   ssh -n -i d:\.ssh\id_ed25519 user1@87.242.100.34 "sudo docker exec nginx-mutual-legacy-nginx-mutual-1 nginx -t && sudo docker exec nginx-mutual-legacy-nginx-mutual-1 nginx -s reload"
   ```

## Certificate Architecture & Terminal mTLS Rules

Comprehensive documentation for the certificate subsystem, mTLS proxying, native C tools, and verification scripts is in **[`docs/certificate-architecture.md`](docs/certificate-architecture.md)**.

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
   - Use `[System.Net.HttpWebRequest]` with `$req.ClientCertificates.Add($cert)` to test terminal mTLS endpoints directly from Windows (see full scripts in `docs/certificate-architecture.md`).
5. **Terminal Verification Registry & Discovery Audit**:
   - The state of all terminals is tracked in real-time by joining `terminal_cert_discovery` + `terminals` + `org_statuses` + `licenses`.
   - When migrating endpoints to the new backend, execute the verification audit script from `docs/certificate-architecture.md §6.3` to inspect all connected terminals.
   - Any unauthenticated terminals (`validation_status = 'terminal_not_found'`, e.g., `OU=346, O=516`) must be imported from legacy MS SQL (`172.17.100.1`) with a generated platform SN (`a4b<7-digit dev_id>c<5-digit rand>d<DDMMYY>`), `OrgStatus(org_id, 'active')`, `License`, and `CertificatePin` (`creation_source='system'`) as described in `docs/certificate-architecture.md §6.4`.

## CI/CD — lessons learned (verified in production sessions)

These are working, verified practices for deploying and verifying changes on
the production host (`176.108.247.249`). See also each subproject's
`.claude/skills/deploy-*/SKILL.md` for the full step-by-step deploy flow.

- **`docker` on the server requires `sudo`.** Plain `docker ...` / `docker
  compose ...` fails with "permission denied while trying to connect to the
  Docker daemon socket". Always prefix with `sudo docker ...` /
  `sudo docker compose ...` over SSH.
- **Database migrations (Alembic):** Alembic migrations reside in
  `ProcessingBackend/backend/alembic/`. To apply migrations in production:
  1. Upload migration files (e.g. `012_add_org_contacts_and_billing_modes.py`) to the server.
  2. Copy into the container or rebuild `processing-backend`:
     `sudo docker cp /home/user1/ProcessingBackend/backend/alembic/versions/. processing-backend:/app/alembic/versions/`
  3. Execute: `sudo docker exec processing-backend alembic upgrade head`
  4. If database schema was altered with new columns/tables, restart dependent containers (e.g. `sudo docker restart menubuilder-backend`).
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
- **Testing authenticated endpoints without real credentials:** generate a
  short-lived JWT directly inside the target container using the app's own
  `create_access_token()` (same secret, same claim names), then curl the
  endpoint with it. This verifies a fix end-to-end post-deploy without ever
  needing/typing a real user's password.
- **SSH execution from Windows PowerShell:** When invoking remote commands via `ssh` from PowerShell, use `ssh -n ...` (e.g. `ssh -n -i d:\.ssh\free-tier-cloud_ru ...`) to prevent stdin handle blocking.
- **PowerShell → `ssh` → remote shell quoting is fragile for inline Python.**
  Multi-line `python -c "..."` one-liners with parentheses/dict literals
  reliably get mangled through nested PowerShell/ssh/remote-shell quoting.
  Prefer: write the script to a local file, `scp` it to `/tmp` on the server,
  `docker cp` it into the container, then `docker exec <container> python
  /tmp/script.py`. Delete the temp script (locally and on the server/
  container) once done — never leave test scripts or generated tokens lying
  around.
- **After any lint/build/deploy/verification pass, clean up temp
  artifacts** (scp'd test scripts, generated tokens, scratch SQL files) from
  both the server and the local session workspace.

## MCP Operations Server (`server-ops`) & DevOps Protocol

An operations MCP server (`server-ops`) is connected to the primary application server (`176.108.247.249`). It provides tools for system health inspection, log investigation, Nginx and SSL management, and safe command execution.

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
     - `[MCP Ops Readiness: DEGRADED / UNAVAILABLE]` — MCP unreachable or resource thresholds exceeded; fall back to standard SSH runbook commands (`user1@176.108.247.249`, key `d:\.ssh\free-tier-cloud_ru`).
   - **CRITICAL: `[MCP Ops Readiness: UNAVAILABLE]` (или `DEGRADED`) НЕ блокирует деплой (Non-Blocking)**. Отсутствие подключения или недоступность MCP Ops сервера не является причиной для отмены или задержки деплоя. Сборка, деплой, применение миграций и верификация в этом случае выполняются в штатном режиме через прямой SSH-транспорт (`scp`, `ssh sudo docker ...`) без блокировок.
3. **Safety Guardrails**:
   - State-changing operations (`nginx_reload`, `file_write`, `file_delete`) return a `confirmationId` and MUST be verified before confirmation via `mcp_server-ops_confirm_execute`.
   - Never inspect raw secret files directly: use `mcp_server-ops_config_audit` for inspecting `.env` configurations.

## MQTT Client Development Rules for AI Agents (Правила разработки MQTT-клиентов)

### Обязательный предварительный опрос типа клиента
Перед началом разработки или изменения любого MQTT-клиента агент **обязан спросить в чате**:

> **"Какой тип MQTT-клиента создаётся: main_app или extra_service?"**

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

### 3. Общие требования к реализации MQTT-клиентов

- **Запрет самостоятельного выбора**: Если тип клиента не указан или ответ отличается от `main_app`/`extra_service`, агент не должен самостоятельно выбирать тип. Нужно уточнить тип клиента в чате.
- **Блокировка реализации**: Нельзя реализовывать MQTT-клиент без выбранного типа клиента.
- **Обязательность presence-сценария**: Presence/status-сценарий должен быть частью стандартной реализации MQTT-клиента.
- **Строгое соответствие топиков и payload**: Топики и payload должны использоваться строго как указано выше.
- **Запрет произвольных изменений**: Не заменять `app`/`svc` и `app_online`/`app_offline`/`svc_online`/`svc_offline` на другие значения без отдельного согласования.
- **Проверка существующего кода**: Если в проекте уже есть MQTT-клиент, при его изменении агент должен проверить наличие этого сценария и добавить его при отсутствии.
