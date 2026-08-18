# AGENTS.md — Project Rules

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

Before any commit or deployment, all three checks must pass in every Python subproject:

```bash
uv run ruff check --fix <src_dir>
uv run ruff format <src_dir>
uv run pyright <src_dir>
```

## Python version

All projects target **Python 3.14** (`requires-python = "==3.14.*"`).

## Project structure

| Subproject | Source dir | Entry point |
|---|---|---|
| ProcessingBackend/backend | `app/` | `uvicorn app.main:app` |
| MenuBuilder/backend | `app/` | `uvicorn app.main:app` |
| ProcessingBackend/mcp-pin-server | `src/` | `python -m pin_server.server` |

## CI/CD — lessons learned (verified in production sessions)

These are working, verified practices for deploying and verifying changes on
the production host (`176.108.247.249`). See also each subproject's
`.claude/skills/deploy-*/SKILL.md` for the full step-by-step deploy flow.

- **`docker` on the server requires `sudo`.** Plain `docker ...` / `docker
  compose ...` fails with "permission denied while trying to connect to the
  Docker daemon socket". Always prefix with `sudo docker ...` /
  `sudo docker compose ...` over SSH.
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
