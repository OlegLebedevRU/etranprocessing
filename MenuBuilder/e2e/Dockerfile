FROM python:3.14-slim@sha256:810da6270e43d30a1f3e0e1eabbeb6fbd9d78ad9dd2e754d5297a3d6cb42df46

WORKDIR /app
COPY mock_gateway.py /app/mock_gateway.py
COPY run_payment_smoke.py /app/run_payment_smoke.py
RUN mkdir /data && chown 65532:65532 /data
USER 65532:65532

ENV E2E_MOCK_BIND=0.0.0.0 E2E_MOCK_PORT=8080 E2E_MOCK_DB_PATH=/data/mock.sqlite3
EXPOSE 8080
CMD ["python", "/app/mock_gateway.py"]
