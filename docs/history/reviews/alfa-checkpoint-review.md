
# Архитектурный анализ: перемешивание ProcessingBackend и MenuBuilder

## 1. Предназначение по изначальной задумке

| Аспект | ProcessingBackend | MenuBuilder |
|--------|------------------|-------------|
| **Аудитория** | Терминалы (устройства) | Пользователи (фронтенд) |
| **Аутентификация** | mTLS (клиентские сертификаты) | JWT (Bearer токены) |
| **Протокол** | XML + legacy форматы | JSON REST API |
| **Nginx** | nginx-mutual (порт 4443, `ssl_verify_client`) | JWT nginx (порт 443, `auth_jwt_module`) |
| **Домен** | `dev.leo4.ru:4443` / `iot-processing.ru:443` | `dev.leo4.ru:443` |

---

## 2. Текущая топология nginx

На хосте `176.108.247.249` работают **два** production nginx:

```
Порт 443 (JWT nginx — menubuilder-frontend)
├── /api/auth/          → menubuilder-backend:8000   [JWT OFF]
├── /api/ListMenuFile   → menubuilder-backend:8000   [JWT OFF]
├── /api/mcp/           → menubuilder-backend:8000   [JWT → jti]
├── /api/billing/       → processing-backend:8000    [JWT → sub, org]  ← ПЕРЕМЕШИВАНИЕ
├── /api/               → menubuilder-backend:8000   [JWT → sub, org, role]
└── /                   → static files (SPA)

Порт 4443 (mTLS nginx — nginx-mutual, конфиг в отдельном репо iot-rpc-rest-app)
├── /api/payment/*      → processing-backend:8000    [client cert headers]
├── /api/techgate/*     → processing-backend:8000    [client cert headers]
├── /api/licensebilling/* → processing-backend:8000  [client cert headers]
├── /api/gategauge/*    → processing-backend:8000    [client cert headers]
├── /api/ListMenuFile   → menubuilder-backend:8000   [client cert headers]  ← ПЕРЕМЕШИВАНИЕ
└── /api/debug/*        → processing-backend:8000    [client cert headers]
```

---

## 3. Выявленные точки архитектурного перемешивания

### 3.1. `/api/billing/` — JWT-эндпоинты в ProcessingBackend

**Файл:** `ProcessingBackend/backend/app/routers/billing.py` (1194 строк)
**Файл:** `ProcessingBackend/backend/app/dependencies.py` (строки 420–452, функция `get_current_user_jwt`)

**Суть нарушения:** В ProcessingBackend, который по задумке обслуживает только терминалы с сертификатами, живёт полноценный JWT-based API — 10 эндпоинтов для работы с биллингом:

```
GET  /api/billing/summary
GET  /api/billing/terminals
POST /api/billing/terminals/{id}/deactivate
POST /api/billing/terminals/{id}/cancel-deactivation
POST /api/billing/checkout
POST /api/billing/terminals/{id}/reactivation-checkout
POST /api/billing/orders/{id}/confirm
GET  /api/billing/orders/{id}
POST /api/billing/terminals/{id}/certificate-pin
```

Все используют `get_current_user_jwt()` — зависимость, которая доверяет заголовкам `jwt-sub` и `jwt-org`, выставленным **nginx MenuBuilder** после валидации JWT. ProcessingBackend **не валидирует JWT самостоятельно** — он полностью зависит от чужого nginx.

**Почему это проблема:**
- ProcessingBackend содержит `jwt_secret_hex` в своём `config.py`, хотя не использует его для валидации
- Бизнес-логика биллинга (billing.py, billing service, cert_billing service, payment_provider, billing schemas) — ~2000 строк кода, живущих в "не своём" бекенде
- Нарушение single responsibility: ProcessingBackend одновременно отвечает за терминальный протокол (XML) и пользовательский API (JSON)

### 3.2. `/api/ListMenuFile` — mTLS-эндпоинт в MenuBuilder

**Файл:** `MenuBuilder/backend/app/main.py` (эндпоинт `GET /api/ListMenuFile`)

**Суть нарушения:** mTLS nginx (порт 4443) проксирует `/api/ListMenuFile` на `menubuilder-backend:8000`. Это единственный эндпоинт MenuBuilder, доступный терминалам через сертификат. Он отдаёт JSON-дерево меню без какой-либо аутентификации (`auth_jwt_enabled off` в JWT nginx).

**Почему это проблема:**
- MenuBuilder по задумке работает только с JWT/фронтендом, но этот эндпоинт обслуживает терминалы
- Терминалы должны ходить на порт 4443 (mTLS nginx), а этот эндпоинт живёт в MenuBuilder
- Нет единого места, где видно все "терминальные" эндпоинты

### 3.3. Кросс-сервисный роутинг в nginx MenuBuilder

**Файл:** `MenuBuilder/nginx.conf` (строки 59–71)

```nginx
location /api/billing/ {
    auth_jwt_extract_var_claims sub org;
    proxy_pass http://processing-backend:8000/api/billing/;
    ...
    proxy_set_header jwt-sub $jwt_claim_sub;
    proxy_set_header jwt-org $jwt_claim_org;
}
```

**Суть нарушения:** Nginx MenuBuilder (JWT) проксирует запросы к ProcessingBackend. Это единственное место, где JWT nginx обращается к "чужому" бекенду. Архитектурно это означает:
- MenuBuilder nginx знает о существовании ProcessingBackend (связь через `pg_network`)
- ProcessingBackend не может быть развёрнут/заменён независимо от MenuBuilder nginx
- Логика маршрутизации "зашита" в конфиг nginx MenuBuilder, а не в отдельном API gateway

### 3.4. Дублирование моделей БД

Оба бекенда содержат **независимые копии** ORM-моделей для одних и тех же таблиц:

| Таблица | ProcessingBackend модель | MenuBuilder модель |
|---------|------------------------|-------------------|
| `orgs` | `Org` | `Org` |
| `terminals` | `Terminal` | `Terminal` |
| `licenses` | `License` | `License` |
| `org_statuses` | `OrgStatus` | `OrgStatus` |
| `org_billing_settings` | `OrgBillingSettings` | `OrgBillingSettings` |
| `certificate_pins` | `CertificatePin` | `CertificatePin` |
| `terminal_types` | `TerminalType` | `TerminalType` |
| `services` | `ServiceMenu` | `Service` |
| `api_tokens` | `ApiToken` | (raw SQL) |

**Почему это проблема:**
- Миграции только в ProcessingBackend (Alembic, 11 файлов), MenuBuilder использует `create_all`
- Изменение схемы требует синхронизации двух независимых кодовых баз
- Риск расхождения: `ServiceMenu` vs `Service` — уже разные имена для одной таблицы

### 3.5. Общий JWT secret без единой точки контроля

**Файлы:**
- `ProcessingBackend/backend/app/config.py` — `jwt_secret_hex`
- `MenuBuilder/backend/app/config.py` — `jwt_secret_hex`
- `MenuBuilder/.env` — `JWT_SECRET_HEX`
- `MenuBuilder/nginx.conf` — `auth_jwt_key "${JWT_SECRET_HEX}"`

Один и тот же секрет используется в 4 местах. ProcessingBackend хранит его, но **не использует** для валидации (только MenuBuilder backend и MenuBuilder nginx валидируют JWT). Это создаёт ложное впечатление, что ProcessingBackend как-то связан с JWT.

### 3.6. Общая сеть `pg_network`

Оба docker-compose используют внешнюю сеть `iot-rpc-rest-app_pg_network`. Это необходимо для доступа к PostgreSQL, но также означает, что:
- MenuBuilder nginx может обращаться к `processing-backend:8000` через эту сеть (и делает это для `/api/billing/`)
- Нет сетевой изоляции между бекендами

---

## 4. Сводная карта зависимостей

```
┌─────────────────────────────────────────────────────────┐
│                    MenuBuilder nginx (JWT, :443)         │
│                    dev.leo4.ru                           │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐              │
│  │ /api/auth │  │ /api/mcp │  │ /api/    │              │
│  └─────┬────┘  └─────┬────┘  └─────┬────┘              │
│        │             │             │                     │
│        ▼             ▼             ▼                     │
│  ┌─────────────────────────────────────┐                │
│  │       menubuilder-backend:8000      │                │
│  │  (JWT login, menus, admin, reports) │                │
│  └─────────────────────────────────────┘                │
│                                                         │
│  ┌──────────┐                                           │
│  │/api/billing│ ← JWT nginx проксирует сюда             │
│  └─────┬────┘                                           │
└────────┼────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────┐
│  ┌─────────────────────────────────────┐                │
│  │     processing-backend:8000         │ ← JWT-эндпоинты│
│  │  (billing.py — 10 эндпоинтов)      │   в "чужом"     │
│  │  get_current_user_jwt() ← nginx     │   бекенде       │
│  └─────────────────────────────────────┘                │
│                                                         │
│              mTLS nginx (nginx-mutual, :4443)            │
│              dev.leo4.ru                                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐              │
│  │/payment/  │  │/techgate/│  │/license- │              │
│  │/gategauge │  │/certs/   │  │billing/  │              │
│  └─────┬────┘  └─────┬────┘  └─────┬────┘              │
│        │             │             │                     │
│        ▼             ▼             ▼                     │
│  ┌─────────────────────────────────────┐                │
│  │     processing-backend:8000         │ ← mTLS-эндпоин│
│  │  get_current_terminal() ← nginx     │   ты (по       │
│  └─────────────────────────────────────┘   задумке)     │
│                                                         │
│  ┌──────────┐                                           │
│  │/ListMenu  │ ← mTLS nginx проксирует сюда             │
│  │  File     │                                           │
│  └─────┬────┘                                           │
└────────┼────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────┐
│  menubuilder-backend:8000           │ ← терминальный    │
│  (ListMenuFile — без аутентификации)│   эндпоинт в      │
│                                     │   "чужом" бекенде │
└─────────────────────────────────────┘
```

---

## 5. Риски текущей архитектуры

| Риск | Серьёзность | Описание |
|------|-------------|----------|
| **Атака через заголовки** | Средняя | Если nginx JWT будет скомпрометирован или неправильно настроен, ProcessingBackend доверяет `jwt-sub`/`jwt-org` без проверки подписи |
| **Сбой каскадом** | Средняя | Падение MenuBuilder nginx обрывает доступ к billing API, хотя ProcessingBackend работает нормально |
| **Рассогласование схемы** | Низкая | Дублирование моделей БД в двух независимых проектах ведёт к расхождению при изменениях |
| **Сложность деплоя** | Средняя | Изменение billing API требует деплоя ProcessingBackend + проверки маршрутизации в MenuBuilder nginx |
| **Нечёткая ownership** | Низкая | Billing код живёт в ProcessingBackend, но обслуживает пользователей MenuBuilder — неочевидно, кто отвечает за поддержку |

---

## 6. Что сделано правильно

- **mTLS nginx архитектурно отделён** от JWT nginx (разные порты, разные хосты, разные конфиги)
- **ProcessingBackend не валидирует JWT** — доверяет nginx, что является безопасным паттерном при правильной настройке proxy_set_header
- **JWT secret не используется в ProcessingBackend для подписи** — только для чтения из конфига (фактически мёртвый код)
- **Терминальные эндпоинты чисто mTLS** — payment, techgate, gategauge, licensebilling, certificates не смешаны с JWT
- **MenuBuilder не имеет доступа к терминальным данным напрямую** — только через billing API ProcessingBackend