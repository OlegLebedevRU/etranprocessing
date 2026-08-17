# JWT Flow — сборка и конфигурирование

## Архитектура

```
Браузер → MenuBuilder nginx (JWT валидация) → MenuBuilder Backend → PostgreSQL
                                              ↓
MCP-сервер (HTTP, Docker network) ← proxy ← /api/mcp/
```

## Nginx JWT модуль

### Сборка образа

Используется **готовый модуль v2.5.0** из GitHub releases (не сборка из исходников).

**Базовый образ:** `debian:bookworm-slim` (НЕ `nginx:alpine` — у него другой layout)

**Ключевые компоненты:**
- nginx 1.31.1 из `nginx.org/packages/mainline/debian`
- `ngx-http-auth-jwt-module` v2.5.0 (pre-built `.so`)
- `libjwt0` + `libjansson4` из Debian repos
- Симлинк `libjwt.so.0 → libjwt.so.2` (модуль ожидает версию 2)

**Dockerfile** (`nginx/Dockerfile`):
```dockerfile
FROM debian:bookworm-slim

# Базовые пакеты + libjwt + libjansson
RUN apt-get update && apt-get install -y \
    libjansson4 libjwt0 curl gnupg2 ca-certificates \
    lsb-release debian-archive-keyring openssl gettext-base \
    && rm -rf /var/lib/apt/lists/*

# Репозиторий nginx.org
RUN curl https://nginx.org/keys/nginx_signing.key | gpg --dearmor > /usr/share/keyrings/nginx-archive-keyring.gpg \
    && printf "deb [signed-by=...] http://nginx.org/packages/mainline/debian $(lsb_release -cs) nginx\n" > /etc/apt/sources.list.d/nginx.list

# nginx 1.31.1
RUN apt-get update && apt-get install -y nginx=1.31.1*

# JWT модуль v2.5.0 (pre-built)
WORKDIR /usr/lib/nginx/modules
RUN curl -Ls https://github.com/TeslaGov/ngx-http-auth-jwt-module/releases/download/2.5.0/ngx-http-auth-jwt-module-2.5.0_libjwt-1.18.4_nginx-1.31.1.tgz \
    | tar -zx > ngx_http_auth_jwt_module_2.5.0.so

# Симлинк для libjwt
RUN ln -sf /usr/lib/x86_64-linux-gnu/libjwt.so.0 /usr/lib/x86_64-linux-gnu/libjwt.so.2

# Inline nginx.conf с load_module
COPY <<` /etc/nginx/nginx.conf
daemon off;
user  nginx;
worker_processes  auto;
error_log  /dev/stderr notice;
pid        /var/run/nginx.pid;
load_module /usr/lib/nginx/modules/ngx_http_auth_jwt_module_2.5.0.so;
events { worker_connections 1024; }
http {
    include /etc/nginx/mime.types;
    default_type application/octet-stream;
    log_format custom '...';
    include /etc/nginx/conf.d/*.conf;
}
`

RUN nginx -t
```

**Важно:**
- `daemon off;` в inline nginx.conf + `CMD ["nginx"]` (НЕ `CMD ["nginx", "-g", "daemon off;"]` — будет дублирование)
- `nginx -t` после копирования конфига — проверка синтаксиса при сборке
- `include /etc/nginx/conf.d/*.conf;` — сюда монтируется server block

### Entrypoint

```sh
#!/bin/sh
# Подстановка JWT_SECRET_HEX в шаблон конфига
envsubst '${JWT_SECRET_HEX}' < /etc/nginx/conf.d/default.conf.template > /etc/nginx/conf.d/default.conf

exec nginx
```

**Критично:** `exec nginx` без `-g 'daemon off;'` — иначе дублирование с inline конфигом.

---

## JWT конфигурация (server block)

### Директивы

| Директив | Уровень | Описание |
|----------|---------|----------|
| `auth_jwt_enabled on/off` | server, location | Включить/выключить JWT валидацию |
| `auth_jwt_key "hex..."` | server, http | Ключ для HS256 (64 hex символа = 32 байта) |
| `auth_jwt_algorithm HS256` | server, http | Алгоритм (HS256 — симметричный) |
| `auth_jwt_redirect off` | server, http | Не редиректить, а отдавать 401 |
| `auth_jwt_location HEADER=Authorization` | server, http | Где искать токен (по умолчанию Authorization header) |
| `auth_jwt_extract_var_claims jti` | location | Извлечь claim в переменную `$jwt_claim_jti` |
| `auth_jwt_extract_request_claims sub` | location | Извлечь claim в заголовок запроса |

### Структура конфига

```nginx
server {
    listen 443 ssl;
    server_name dev.leo4.ru;

    ssl_certificate /etc/nginx/ssl/fullchain.pem;
    ssl_certificate_key /etc/nginx/ssl/privkey.pem;
    ssl_protocols TLSv1.2 TLSv1.3;

    # JWT — глобально на server
    auth_jwt_enabled on;
    auth_jwt_key "${JWT_SECRET_HEX}";       # подставляется через envsubst
    auth_jwt_algorithm HS256;
    auth_jwt_redirect off;
    auth_jwt_location HEADER=Authorization;

    # Публичные endpoints — JWT выключен
    location /api/auth/ {
        auth_jwt_enabled off;
        proxy_pass http://menubuilder-backend:8000;
        ...
    }

    location = /api/ListMenuFile {
        auth_jwt_enabled off;
        proxy_pass http://menubuilder-backend:8000;
        ...
    }

    # MCP — JWT терминируется на nginx, backend получает только jti
    location /api/mcp/ {
        auth_jwt_extract_var_claims jti;
        proxy_set_header X-Auth-Jti $jwt_claim_jti;
        proxy_set_header Authorization "";  # Убираем JWT — терминация на nginx!
        proxy_pass http://menubuilder-backend:8000;
        proxy_read_timeout 300s;
        ...
    }

    # API — JWT включён (наследуется от server), извлекаем sub
    location /api/ {
        auth_jwt_extract_request_claims sub;
        proxy_pass http://menubuilder-backend:8000;
        ...
    }

    # Статика — JWT выключен
    location / {
        auth_jwt_enabled off;
        try_files $uri $uri/ /index.html;
    }
}
```

### Переменные

- `${JWT_SECRET_HEX}` — подставляется через `envsubst` из ENV переменной
- `$jwt_claim_jti` — nginx переменная, заполняется директивой `auth_jwt_extract_var_claims jti`
- `$jwt_claim_sub` — аналогично для `sub` claim
- `$host`, `$remote_addr`, `$scheme` — стандартные nginx переменные (НЕ заменяются envsubst)

**envsubst** заменяет только `${VAR_NAME}` (с фигурными скобками). `$host` и подобные не затрагиваются.

---

## JWT токен

### Формат

```
Header:  {"alg":"HS256","typ":"JWT"}
Payload: {"sub":"o.lebedev","org_id":1,"jti":"uuid-v4","exp":1787012718,"iat":1786983918}
Signature: HMAC-SHA256(base64(header).base64(payload), secret)
```

### Claims

| Claim | Источник | Описание |
|-------|----------|----------|
| `sub` | username из AUTH_USERS | Имя пользователя |
| `org_id` | org_id из AUTH_USERS | ID организации (tenant isolation) |
| `jti` | UUID v4 | Уникальный ID токена (для API tokens) |
| `exp` | now + 480 min (или 30 дней для API tokens) | Время истечения |
| `iat` | now | Время создания |

### Ключ

- **Формат:** 64 hex символа = 32 байта = 256 бит
- **Генерация:** `python -c "import secrets; print(secrets.token_hex(32))"`
- **Хранение:** ENV переменная `JWT_SECRET_HEX` в `.env` файле
- **Использование:** nginx (валидация) + backend (создание/декодирование)

### Проверка токена

```python
import hmac, hashlib, base64, json

secret = bytes.fromhex("176b79312cfae3cf5e6c0200548f32c7c47e7b06b933eb11c9b2936bfe2644bc")
token = "eyJhbGci..."
header, payload, signature = token.split(".")

# Проверка подписи
msg = f"{header}.{payload}"
expected = base64.urlsafe_b64encode(
    hmac.new(secret, msg.encode(), hashlib.sha256).digest()
).rstrip(b"=").decode()
assert signature == expected

# Декодирование payload
data = json.loads(base64.urlsafe_b64decode(payload + "=="))
print(data)  # {"sub": "o.lebedev", "org_id": 1, ...}
```

---

## Docker Compose

### MenuBuilder

```yaml
services:
  menubuilder-backend:
    build: ./backend
    environment:
      - DATABASE_URL=postgresql+asyncpg://etran:etran@pg:5432/etranprocessing
      - JWT_SECRET_HEX=${JWT_SECRET_HEX}    # из .env файла
      - AUTH_USERS=${AUTH_USERS}              # из .env файла
    networks:
      - pg_network
      - menubuilder_net

  menubuilder-frontend:
    build:
      context: .
      dockerfile: nginx/Dockerfile
    environment:
      - JWT_SECRET_HEX=${JWT_SECRET_HEX}    # для envsubst в entrypoint
    volumes:
      - ./frontend/dist:/usr/share/nginx/html:ro
      - ./nginx.conf:/etc/nginx/conf.d/default.conf.template:ro  # шаблон!
      - /path/to/certs/fullchain.pem:/etc/nginx/ssl/fullchain.pem:ro
      - /path/to/certs/privkey.pem:/etc/nginx/ssl/privkey.pem:ro
    networks:
      - menubuilder_net
```

**Важно:** nginx.conf монтируется как `.template` — entrypoint выполняет `envsubst` и создаёт `.conf`.

### ProcessingBackend + MCP

```yaml
services:
  processing-backend:
    build: ./backend
    env_file: ./backend/.env
    networks:
      - pg_network

  mcp-pin-server:
    build: ./mcp-pin-server
    environment:
      - DATABASE_URL=postgresql://etran:etran@pg:5432/etranprocessing
      - MCP_PORT=8001
    networks:
      - pg_network      # доступ к PostgreSQL
```

---

## API Tokens (long-lived)

### Принцип работы

1. Пользователь создаёт токен через `/api/profile/tokens`
2. Backend генерирует JWT с `jti` (UUID v4), `exp` = now + 30 дней
3. `jti` сохраняется в таблицу `api_tokens`
4. JWT отображается ОДИН РАЗ — пользователь копирует
5. При запросе: nginx валидирует JWT → backend проверяет `jti` по БД (не отозван)

### Таблица api_tokens

```sql
CREATE TABLE api_tokens (
    id SERIAL PRIMARY KEY,
    jti VARCHAR(36) UNIQUE NOT NULL,
    user_id VARCHAR(100) NOT NULL,
    name VARCHAR(200),
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ DEFAULT now(),
    last_used_at TIMESTAMPTZ,
    revoked_at TIMESTAMPTZ   -- NULL = активен
);
```

### Flow

```
MiMoCode → Authorization: Bearer <JWT with jti>
         → nginx: JWT валидация (signature + expiry)
         → nginx: X-Auth-Jti: $jwt_claim_jti
         → backend: проверка jti в api_tokens (не отозван)
         → proxy к MCP-серверу
```

---

## Типичные проблемы

### "unknown directive auth_jwt_key"
- Модуль не загружен. Проверить `load_module` в `/etc/nginx/nginx.conf`

### "invalid algorithm specified"
- Несовпадение алгоритма. HS256 требует hex ключ, RS256 — PEM public key

### 401 на всех запросах после логина
- Axios client не отправляет Authorization header. Нужен request interceptor:
```typescript
client.interceptors.request.use((config) => {
  const token = localStorage.getItem("mb_token");
  if (token) config.headers.Authorization = `Bearer ${token}`;
  return config;
});
```

### "daemon directive is duplicate"
- `daemon off;` в inline nginx.conf + `nginx -g 'daemon off;'` в CMD
- Решение: `CMD ["nginx"]` (без флагов)

### envsubst заменяет nginx переменные
- `${JWT_SECRET_HEX}` заменяется, `$host` — нет (без фигурных скобок)
- `envsubst '${JWT_SECRET_HEX}'` — явный список заменяемых переменных
