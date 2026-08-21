---
name: server-ops-diagnostics
description: Inspects server health, analyzes memory and disk usage, searches application and system logs, and troubleshoots Nginx/service issues using the connected MCP server-ops tools. Use when the user asks to inspect server status, debug errors/exceptions, check RAM/disk, or diagnose Nginx issues.
---

# Server Ops & Diagnostics Skill

Operational workflows for monitoring, diagnosing, and troubleshooting the production server at `176.108.247.249` via the connected `server-ops` MCP server.

## Mandatory Task Readiness Protocol (Step 0)

Before executing any diagnostic or operational workflow, verify and declare task readiness:

1. **Probe MCP Server Availability & Host Load:**
   - Call `mcp_server-ops_system_info(type="load")`
2. **Check Host Safety Thresholds:**
   - **RAM**: Available memory must be > 300 MiB (`mcp_server-ops_system_info(type="memory")` or `mcp_server-ops_memory_analysis`). *Critical: Server has 0B Swap.*
   - **Disk**: Root `/` usage must be < 90% (`mcp_server-ops_system_info(type="disk")` or `mcp_server-ops_disk_analysis`).
   - **Load Average**: Should be < 2.0 under normal operation.
3. **Set Readiness Mark:**
   - `[MCP Ops Readiness: READY]` — proceed with MCP workflows below.
   - `[MCP Ops Readiness: DEGRADED / UNAVAILABLE]` — fall back to direct SSH commands (`user1@176.108.247.249`, key `d:\.ssh\free-tier-cloud_ru`).

---

## Diagnostic Workflows

### 1. Error & Exception Investigation
- **Find recent application errors:**
  - Call `mcp_server-ops_log_search(level="error", lines=50)`
- **Search specific error keyword or traceback:**
  - Call `mcp_server-ops_log_search(keyword="Traceback", lines=50)`
- **Read log tail:**
  - List logs: `mcp_server-ops_log_list()`
  - Read specific log: `mcp_server-ops_log_read(path="<log_path>", limit=200)`
- **Check system journals (`journalctl`):**
  - Call `mcp_server-ops_log_search_system(service="docker", keyword="error")`

### 2. Resource & OOM Risk Analysis
- **Analyze memory consumption:**
  - Call `mcp_server-ops_memory_analysis()` to detect high-RAM processes and OOM risks.
- **Analyze disk space consumption:**
  - Call `mcp_server-ops_disk_analysis(path="/", depth=2)` to find large log files or unused Docker layers.

### 3. Nginx & Reverse Proxy Diagnostics
- **View Nginx configuration:**
  - Call `mcp_server-ops_nginx_config_read(path="/etc/nginx/nginx.conf")`
- **Validate Nginx syntax:**
  - Call `mcp_server-ops_nginx_config_test()`
- **Reload Nginx (zero downtime):**
  - Call `mcp_server-ops_nginx_reload()`, obtain `confirmationId`, and confirm with `mcp_server-ops_confirm_execute(confirmationId="...")`.

### 4. Configuration & Security Audit
- **Inspect environment variables safely:**
  - Call `mcp_server-ops_config_audit(file="server/.env")` — credentials and secret tokens are automatically masked.
- **Check server Git repository status:**
  - Call `mcp_server-ops_project_overview()` to verify deployed branches and latest commit hashes.

---

## Safety Rules
- **NEVER** expose unmasked passwords or API keys.
- **ALWAYS** confirm mutating actions (`nginx_reload`, `file_write`, `file_delete`) via `mcp_server-ops_confirm_execute`.
- If memory is low (< 300 MiB), avoid triggering heavy concurrent operations.
