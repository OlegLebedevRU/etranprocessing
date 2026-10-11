FROM python:3.14-alpine
WORKDIR /app
COPY server.py /app/server.py
COPY native_captcha.py /app/native_captcha.py
USER 65534:65534
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
CMD ["python", "server.py"]
