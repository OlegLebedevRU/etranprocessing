FROM python:3.14-slim

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc libpq-dev && \
    rm -rf /var/lib/apt/lists/*

COPY shared/ /shared/
RUN pip install --no-cache-dir /shared

COPY ProcessingBackend/backend/pyproject.toml .
RUN pip install --no-cache-dir .

COPY ProcessingBackend/backend/app/ ./app/
COPY ProcessingBackend/backend/alembic/ ./alembic/
COPY ProcessingBackend/backend/alembic.ini .

EXPOSE 8000

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
