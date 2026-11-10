FROM ghcr.io/astral-sh/uv:0.13.7@sha256:96f40d3904aad8d0bd010539001d36c8e2188b981e1334471a90c4a1d93bb490 AS uv
FROM python:3.14.10-slim@sha256:9aaa474097d094040649f2162a627c0049b408f52138d980916788d80f04802f

COPY --from=uv /uv /usr/local/bin/uv
WORKDIR /workspace/ProcessingBackend/backend

ENV PATH="/workspace/ProcessingBackend/backend/.venv/bin:$PATH" \
    UV_PYTHON_DOWNLOADS=never UV_LINK_MODE=copy

COPY shared/ /workspace/shared/
COPY ProcessingBackend/backend/pyproject.toml ProcessingBackend/backend/uv.lock ./
COPY ProcessingBackend/backend/app/ ./app/
COPY ProcessingBackend/backend/alembic/ ./alembic/
COPY ProcessingBackend/backend/alembic.ini .
RUN uv sync --locked --no-dev --no-editable --no-cache

EXPOSE 8000

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
