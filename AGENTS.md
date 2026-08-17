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
