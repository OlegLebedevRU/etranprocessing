# AGENTS.md — Project Rules

## Secrets and credentials

**NEVER** hardcode secrets, API keys, passwords, tokens, database URLs, or internal URLs in source code. All sensitive values must come from environment variables or `.env` files.

- Use `os.environ.get("KEY")`, `pydantic-settings`, or similar mechanisms
- `.env` files must be in `.gitignore` and never committed
- Config classes must use `extra = "ignore"` to tolerate shared `.env` files
- Default values in config should be non-sensitive placeholders (e.g. `localhost`, empty string)
- If you discover a secret in code, remove it immediately and rotate the credential

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
