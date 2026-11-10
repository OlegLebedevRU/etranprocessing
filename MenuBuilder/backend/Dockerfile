FROM ghcr.io/astral-sh/uv:0.13.7@sha256:96f40d3904aad8d0bd010539001d36c8e2188b981e1334471a90c4a1d93bb490 AS uv
FROM python:3.14.10-slim@sha256:9aaa474097d094040649f2162a627c0049b408f52138d980916788d80f04802f AS runtime

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

# Verify the same application source in an isolated build container.
FROM runtime AS verification
COPY contracts/hevc-v2/ /workspace/contracts/hevc-v2/
COPY contracts/siplite-camera-v1/ /workspace/contracts/siplite-camera-v1/
COPY MenuBuilder/nginx.conf /workspace/MenuBuilder/nginx.conf
COPY MenuBuilder/backend/tests/ ./tests/
RUN uv sync --locked --no-editable --no-cache
RUN uv run ruff check app && uv run ruff format --check app && uv run pyright app
RUN uv run pytest -q

FROM runtime AS release
