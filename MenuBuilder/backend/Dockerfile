FROM ghcr.io/astral-sh/uv:0.12.5@sha256:db2d5999728c5837e1bf9ba278ee6b05cef1e95e82a20e27b0c915cb4478b9d7 AS uv
FROM python:3.14-slim@sha256:810da6270e43d30a1f3e0e1eabbeb6fbd9d78ad9dd2e754d5297a3d6cb42df46

COPY --from=uv /uv /usr/local/bin/uv
WORKDIR /workspace/MenuBuilder/backend

ENV PATH="/workspace/MenuBuilder/backend/.venv/bin:$PATH" \
    UV_PYTHON_DOWNLOADS=never UV_LINK_MODE=copy

COPY shared/ /workspace/shared/
COPY MenuBuilder/backend/pyproject.toml MenuBuilder/backend/uv.lock ./
COPY MenuBuilder/backend/app/ ./app/
RUN uv sync --locked --no-dev --no-editable --no-cache

EXPOSE 8000

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
