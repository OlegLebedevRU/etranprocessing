FROM ghcr.io/astral-sh/uv:0.12.5@sha256:db2d5999728c5837e1bf9ba278ee6b05cef1e95e82a20e27b0c915cb4478b9d7 AS uv
FROM python:3.14-slim@sha256:810da6270e43d30a1f3e0e1eabbeb6fbd9d78ad9dd2e754d5297a3d6cb42df46

COPY --from=uv /uv /usr/local/bin/uv
WORKDIR /app
ENV PATH="/app/.venv/bin:$PATH" UV_PYTHON_DOWNLOADS=never UV_LINK_MODE=copy

COPY pyproject.toml uv.lock ./
COPY src/ src/
RUN uv sync --locked --no-dev --no-editable --no-cache

EXPOSE 8001

CMD ["python", "-m", "l4mcp.server"]
