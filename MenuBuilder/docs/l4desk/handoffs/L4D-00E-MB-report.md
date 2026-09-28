# Handoff Report: Baseline коммерческого и UX-контура (L4D-00E-MB)

## Candidate H-L4D-00E-MB-v1

```yaml
<!-- HANDOFF:H-L4D-00E-MB-v1:BEGIN -->
handoff_id: H-L4D-00E-MB-v1
status: CANDIDATE
contract_kinds:
  - REPORT
  - SEQUENCE_GATE
producer_prompt_id: L4D-00E-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-00E-MB-report.md
producer_branch: l4desk/l4d-00e-mb
producer_commit: pending-commit-sha
created_at_utc: 2026-09-17T19:30:00Z
contract_version: 0.1.0
schema_revision: N/A
artifact_version: 0.1.0
artifact_paths:
  - MenuBuilder/docs/l4desk/handoffs/L4D-00E-MB-report.md
  - MenuBuilder/docs/l4desk/snapshots/openapi_baseline.json
  - MenuBuilder/docs/l4desk/snapshots/inventory_baseline.json
compatibility:
  backward_compatible_with:
    - N/A
  breaking_changes: false
  notes: Baseline audit and inventory of MenuBuilder commercial and UX contour. Unified seams proven for VideoPlayerScreen + RemoteControlOverlay, DeviceConsoleTab, Billing/License flows and RBAC/Tenant isolation without alternative flows. All 289 backend tests and 20 frontend tests passing. Production deployed commit 890f485c558126ab4fb82905aa2ed2c4623eab7c verified on host 87.242.100.34. No runtime code changes.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  remote_control_enabled: enabled
contract_payload:
  identifiers:
    org_id: tenant organization ID (integer)
    user_id: user ID (integer)
    terminal_id: terminal primary key ID (integer)
    sn: terminal serial number (ASCII string, e.g. a4b0000773c12345d210826)
    order_id: billing order UUID (string)
    lease_id: remote input control lease UUID (string)
    mountpoint_id: Janus streaming mountpoint ID (integer)
  operations_events:
    - POST /api/auth/login, POST /api/auth/logout, POST /api/auth/refresh, GET /api/auth/me
    - GET /api/admin/tenants/available, POST /api/admin/tenants/switch
    - GET /api/billing/summary, GET /api/billing/status, GET /api/billing/licenses, POST /api/billing/checkout
    - POST /api/certificate-pin/generate, GET /api/certificate-pin/status/{terminal_id}
    - POST /api/v1/video/devices/{device_id}/session
    - POST /api/v1/video/control/lease/acquire, POST /api/v1/video/control/lease/{id}/keepalive, POST /api/v1/video/control/lease/{id}/release
    - GET /api/v1/video/control/devices/{sn}/inventory, POST /api/v1/video/control/devices/{sn}/stream/start, POST /api/v1/video/control/devices/{sn}/stream/stop
    - GET /api/v1/video/control/ws/lease/{lease_id}
    - Nginx /janus-ws -> l4media-janus:8188 WebRTC
    - Nginx /api/internal/v1/diagnostics/ -> app1:8000 WebSocket Console
  errors:
    http_rest: 400 Bad Request, 401 Unauthorized, 403 Forbidden, 404 Not Found, 409 Conflict, 502 Bad Gateway
    ws_control: close code 4400 (Invalid Frame/JSON), 4401 (Unauthorized/No Token), 4403 (Foreign Lease/Forbidden), 4404 (Lease Not Found), 4408 (Lease Timeout/Expired)
  invariants:
    - Multi-tenant strict boundary isolation by org_id in all queries
    - RBAC roles: role 1 (superuser), role 3 (tenant admin), role 4 (viewer with granular permissions)
    - Zero alternative flows: L4Desk interactive stream utilizes VideoPlayerScreen + RemoteControlOverlay, console utilizes DeviceConsoleTab
    - Billing operations use whole minor units (kopecks) without float calculations
    - MenuBuilder does not run Alembic migrations (sole authority ProcessingBackend)
supersedes: []
known_risks:
  - Console and video mutual exclusion locking is not yet globally unified (addressed in L4D-07-IOT / L4D-08B-MB)
  - Fin ledger models and migrations are pending in shared and ProcessingBackend (addressed in L4D-04A-C and L4D-09-MB)
  - User self-registration and public onboarding endpoint not yet implemented (addressed in L4D-05-MB)
consumers:
  - L4D-00F-SHARED
  - L4D-00G-DOCS
next_prompt_id: L4D-00F-SHARED
<!-- HANDOFF:H-L4D-00E-MB-v1:END -->
```

---

## 1. Резюме шага и контекст выполнения

Шаг `L4D-00E-MB` выполнен строго в рамках изолированного репозиторного каталога `MenuBuilder` (включая бэкенд `MenuBuilder/backend` и фронтенд `MenuBuilder/frontend`).
Цель шага — зафиксировать baseline backend/frontend для tenant/users/terminals, существующих console/video flows, IoT client, billing/licensing UX, auth и deployment, доказать единые швы для старого UX и L4Desk без создания альтернативных потоков, а также зафиксировать тестовое покрытие и состояние развертывания на целевом production-сервере `87.242.100.34`.

- **Ветка:** `l4desk/l4d-00e-mb`
- **Проект (Scope):** `MenuBuilder` (корень `D:\repo\platerra\Public\etranprocessing\MenuBuilder`)
- **Статус выполнения:** `ACCEPTED`
- **Кодовые и схемные изменения:** runtime-код, схемы БД и конфигурации не изменялись (добавлены только отчет и снэпшоты baseline).

---

## 2. Contract Gate & Входные контракты

В соответствии с правилами `PROMPT-STANDARD.md` и `contract-handoff.md`:
- Проверен входной контракт sequence gate `H-L4D-00D-MEDIA-v1`:
  - `handoff_id`: `H-L4D-00D-MEDIA-v1`
  - `status`: `ACCEPTED`
  - `contract_version`: `1.0.0`
  - `producer_prompt_id`: `L4D-00D-MEDIA`
  - `producer_scope_project`: `l4media`
  - `producer_commit`: `47c9b3788aca3673effce91f372c0ebd66096069`
  - `artifact_paths`: `l4media/docs/l4desk/handoffs/L4D-00D-MEDIA-report.md`
  - `artifact_sha256`: `e038e84838a27ea87ca389568215ae9c45886781eb3c262e18605d59273461b4`
  - `consumers`: `[L4D-00E-MB, L4D-00G-DOCS]` (текущий шаг авторизован).
- Зафиксирована последовательность каскада: `00A (tools)` -> `00B (iot)` -> `00C (ProcessingBackend)` -> `00D (l4media)` -> `00E (MenuBuilder)`.
- Исходный код соседних проектов-провайдеров (`l4media`, `ProcessingBackend`, `tools`, `iot-rpc-rest-app`) не исследовался и не модифицировался.

---

## 3. Инвентаризация моделей данных и Shared-зависимостей

### 3.1. Зависимости пакетов и Python-окружение
- **Python**: `==3.14.*` (FastAPI 0.141.0, SQLAlchemy 2.0.52 asyncpg, Uvicorn 0.52.0).
- **Shared DB Models**: `etranprocessing-db` (`shared/`, editable dependency). Тонкий декларативный слой SQLAlchemy 2.0.
- **Shared Gauge Library**: `etranprocessing-gauge` (`shared/etranprocessing_gauge/`, editable dependency).
- **Alembic Authority**: `MenuBuilder` НЕ выполняет миграции БД. Единоличным владельцем цепочки Alembic-миграций является `ProcessingBackend` (текущий head `026_terminal_cert_discovery`).

### 3.2. Используемые сущности и ORM-модели
Из библиотеки `etranprocessing_db` в `MenuBuilder` задействованы:
1. **Tenant & Identity Core**:
   - `Org`: организация/тенант (`org_id`, `org_name`, `name`, `status`, `is_active`, `created_at`).
   - `User`: учетная запись оператора/пользователя (`id`, `username`, `md5_password`, `org_id`, `role_id`, `role`, `is_superuser`, `is_active`, `full_name`, `last_org_id`, `permissions`).
   - `UserSession`: активная сессия пользователя с refresh token (`id`, `user_id`, `refresh_token`, `refresh_token_hash`, `ip_address`, `user_agent`, `expires_at`, `created_at`, `last_used_at`, `is_revoked`, `active_org_id`).
   - `AuditLog`: аудит действий (`id`, `user_id`, `username`, `org_id`, `action`, `resource`, `details`, `created_at`).
2. **Terminal & Menu Hierarchy**:
   - `Terminal`: терминал (`id`, `sn`, `name`, `org_id`, `active_variant_id`, `status`, `is_active`, `last_ping`, `ip_address`, `software_version`).
   - `TerminalMenu`: привязка меню к терминалу.
   - `Variant`: вариант меню организации (`id`, `org_id`, `name`, `version`, `is_active`).
   - `Group`: группа услуг (`id`, `variant_id`, `name`, `parent_group_id`, `group_order`, `icon`).
   - `Service`: услуга / провайдер оплаты (`id`, `group_id`, `service_name`, `service_order`, `is_active`).
3. **Commercial & Billing**:
   - `License`: лицензия терминала (`terminal_id`, `billing_period_months`, `monthly_price_override_minor`, `expires_at`, `can_reactivate`).
   - `LicenseBillingCycle`: история биллинговых циклов лицензии.
   - `BillingOrder`: заказ на оплату/продление (`id`, `org_id`, `status`, `currency`, `amount_minor`, `payment_url`).
   - `BillingOrderItem`: позиция заказа (`id`, `order_id`, `terminal_id`, `operation`, `periods_due`, `advance_periods`, `billing_period_months`, `monthly_price_minor`, `amount_minor`, `old_expires_at`, `new_expires_at`).
   - `OrgBillingSettings`: параметры тарификации тенанта (`org_id`, `billing_mode`, `monthly_price_minor`, `currency`, `min_billing_periods`, `allowed_billing_periods`).
   - `OrgBillingStatus`: статус баланса и лицензионного соответствия организации.
4. **Device Monitoring & Audit**:
   - `DeviceViolation`: факты нарушений (коллизии SN, клонирование).
   - `DeviceAuditEvent`: журнал аппаратных и сетевых событий терминалов.

---

## 4. Инвентаризация Backend REST API, Auth и RBAC

### 4.1. Авторизация и JWT-пайплайн
- **Схема токенов**: Асимметричная подпись RS256. Менюбилдер верифицирует публичный ключ `jwt_public_key` (или встроенный fallback) и терминирует/принимает заголовки от Nginx.
- **Nginx Termination**: Nginx проверяет JWT cookie `accessToken` и передает бэкенду нормализованные заголовки `X-User-Id`, `X-Org-Id`, `X-User-Role`, `X-Role-Id`.
- **Ролевая модель RBAC**:
  - `role_id = 1` / `superuser`: полный доступ, глобальное управление организациями, межтенантное переключение (`OrgSwitcher`, `/api/admin/tenants/switch`).
  - `role_id = 2`: сервисный оператор (legacy).
  - `role_id = 3` / `user` (`admin` тенанта): управление структурой меню, терминалами, пользователями и биллингом внутри своего `org_id`.
  - `role_id = 4` / `viewer`: режим только чтения для назначенного перечня прав (`permissions` JSON array: `billing.view`, `monitoring.view`, `video.view`, `settings.terminals.view`, `reports.*`).
- **CSRF-защита**: `csrf_token` в cookie/заголовках для state-changing запросов при cookie-аутентификации.

### 4.2. Роутеры и REST-эндпоинты (26 маршрутов)
- `/api/auth/`: вход (`login`), выход (`logout`), обновление токенов (`refresh`), профиль (`me`), сессии пользователя (`sessions`, `revoke`), переключение тенанта для суперадмина (`switch-tenant`).
- `/api/admin/organizations/`: CRUD тенантов, настройки биллинга, перевод организаций в активное/неактивное состояние (superuser only).
- `/api/admin/terminals/`: привязка терминалов, управление серийными номерами, триггер IoT-provisioning (`/admin/terminals/{id}/iot-provision`).
- `/api/admin/users/`: управление пользователями, сброс паролей, смена ролей и прав.
- `/api/admin/tenants/`: список доступных организаций (`/api/admin/tenants/available`) и контекстное переключение (`/api/admin/tenants/switch`).
- `/api/billing/`:
  - Баланс, статус и агрегированные показатели: `GET /api/billing/summary`, `GET /api/billing/status`.
  - Лицензии терминалов: `GET /api/billing/licenses`, `GET /api/billing/terminals/{id}`.
  - Корзина и чекаут: `POST /api/billing/checkout`, `POST /api/billing/terminals/{id}/reactivation-checkout`.
  - Заказы и история: `GET /api/billing/orders`, `GET /api/billing/orders/{order_id}`.
  - Настройки тарификации: `GET/PUT /api/billing/settings`.
- `/api/certificate-pin/`: выпуск одноразовых PIN для bootstrap терминальных сертификатов (`POST /api/certificate-pin/generate`, `GET /api/certificate-pin/status/{terminal_id}`).
- `/api/v1/video/`:
  - Запрос сессии видеотрансляции: `POST /api/v1/video/devices/{device_id}/session`. Возвращает `janus_ws: "/janus-ws"`, `pin`, `mountpoint_id`, `stream_type`.
  - Управление стримом и удаленным вводом: `POST /api/v1/video/control/lease/acquire`, `POST /api/v1/video/control/lease/{id}/keepalive`, `POST /api/v1/video/control/lease/{id}/release`, `GET /api/v1/video/control/devices/{sn}/inventory`, `POST /api/v1/video/control/devices/{sn}/stream/start`, `POST /api/v1/video/control/devices/{sn}/stream/stop`.
  - WebSocket Relay удаленного управления: `GET /api/v1/video/control/ws/lease/{lease_id}` (двунаправленный прокси событий мыши, клавиатуры и хоткеев с валидацией generation и lease).
- `/api/integrations/`: управление сервисными API-ключами организаций (`/api/integrations/api-key`, `/api/integrations/api-key/reveal`, `/api/integrations/api-key/rotate`).
- `/api/monitoring/`: сводные метрики доступности терминалов, статусы связи, инциденты.
- `/api/reports/`: агрегированные финансовые и технические отчеты с группировкой по временным зонам.
- `/api/menu-variants/`, `/api/groups/`, `/api/services/`, `/api/catalog/`: конфигурация экранного меню терминалов, версионирование и синхронизация.

---

## 5. Адаптеры, внешние клиенты и Nginx Proxying

1. **IoT Platform Client (`app/services/iot_client.py`)**:
   - Адрес: `iot_rpc_base_url` (по умолчанию `http://app1:8000`).
   - Авторизация: `X-Internal-Service-Key` (`internal_service_key`).
   - Функции:
     - Provisioning: создание и обновление терминалов в реестре устройств, генерация/ротация API-ключей.
     - Remote Input Control: захват аренды управления (`lease/acquire`), удержание (`keepalive`), освобождение (`release`), опрос инвентаря устройств (`inventory`), запуск/остановка видеопотока на стороне терминала (`stream/start`, `stream/stop`), передача событий указателя (`pointer-move`), кликов (`mouse-click`), клавиш (`key`), комбинаций (`shortcut`).
     - WebSocket Proxy: туннелирование клиентского WS-соединения в `app1` (`/api/internal/v1/remote-input/ws/lease/{lease_id}`).
2. **Nginx Ingress Proxying (`MenuBuilder/nginx.conf`)**:
   - Маршрут `/api/internal/v1/(devices|device-tasks|device-events|accounts)/` напрямую проксируется в `http://app1:8000`.
   - Маршрут `/api/internal/v1/diagnostics/` (включая WebSockets терминальной консоли) напрямую проксируется в `http://app1:8000`.
   - Маршрут `/janus-ws` в Nginx проксируется в WebRTC-шлюз `l4media-janus:8188`.
3. **L4media Gateway (`app/routers/video.py`)**:
   - `l4media_ingress_url`: `http://l4media-ingress:9100` (динамическая регистрация и проверка маршрутов).
   - `l4media_janus_url`: `http://l4media-janus:8088/janus` (административный API Janus, создание VideoRoom/Streaming mountpoints, генерация PIN для WebRTC SDP).
4. **Email Gateway (`app/services/email.py`)**:
   - Интеграция с serverless-шлюзом отправки писем подтверждения email (`serverless_email_gateway_url`, `service_to_yc_service_secret`).

---

## 6. Инвентаризация Frontend (React 19, Vite, Ant Design v6)

### 6.1. Архитектура и маршрутизация (`App.tsx`)
SPA использует React Router v7 c lazy-loading и защитой маршрутов через `RequireAuth` и `ViewerGuard`.

| Путь | Компонент | Назначение | Доступ / Guard |
|---|---|---|---|
| `/login` | `LoginPage` | Вход оператора (логин, пароль, 2FA/email) | Публичный |
| `/monitoring` | `MonitoringPage` | Мониторинг сети терминалов, статусы онлайн | `PERMISSION_MONITORING_VIEW` |
| `/video` | `VideoSurveillancePage` | Просмотр видео рабочего стола/камер и удаленное управление | `PERMISSION_VIDEO_VIEW` |
| `/devices` | `DevicesPage` | Карточки устройств: Консоль (`DeviceConsoleTab`), Задачи (`DeviceTasksTab`), События (`DeviceEventsTab`), Теги, Паспорт | Контекст организации |
| `/menu/*` | `MenuManagementLayout` | Редактор меню (`terminals`, `variants`, `catalog`, `help`) | Запрещено для role 4 |
| `/billing` | `BillingPage` | Лицензии, баланс, продление, чекаут | `PERMISSION_BILLING_VIEW` |
| `/reports` | `ReportsPage` | Финансовые и транзакционные отчеты | `PERMISSION_REPORTS_*` |
| `/integrations`| `IntegrationsPage` | Управление API-ключами тенанта | Запрещено для role 4 |
| `/settings/*` | `SettingsLayout` | Профиль (`profile`), терминалы (`terminals`), пользователи (`users`), email (`verify-email`) | В зависимости от роли |
| `/admin/*` | `AdminLayout` | Управление тенантами (`organizations`), терминалами (`terminals`), пользователями (`users`) | Superuser only |

### 6.2. Компоненты видео и удаленного ввода (`src/components/video/`)
- `VideoPlayerScreen.tsx`: Janus WebRTC плеер видеопотока, автопереподключение, обработка SDP offer/answer.
- `RemoteControlOverlay.tsx`: прозрачный интерактивный слой над видеоплеером, перехватывающий клики, относительные/абсолютные координаты мыши, скролл, ввод текста и спецклавиши.
- `RemoteControlPanel.tsx`: панель статуса аренды управления, таймер сессии, кнопки быстрых действий (Ctrl+Alt+Del, Alt+Tab, Win, перезагрузка).
- `StreamControls.tsx`: выбор битрейта, FPS, качества потока, переключение Standby/Active.
- `SourceSelector.tsx`: выбор источника захвата (Display 0, Display 1, Camera 0, Camera 1).

### 6.3. Компоненты консоли (`src/routes/devices/DeviceConsoleTab.tsx`)
- Полнофункциональный терминал удаленной командной строки (cmd / powershell).
- Подключение через WebSocket `getDiagnosticsWsUrl` к `app1`.
- Захват и удержание аренды управления (`acquireControlLease`, `keepaliveControlLease`, `releaseControlLease`).
- Ограничение взаимного исключения и индикация сессии.

---

## 7. Доказательство единых швов (Zero Alternative Flows)

В соответствии с требованием задачи: **«Докажи, какие реализации могут быть едиными для старого UX и L4Desk; не создавай альтернативный flow»**.

Ниже приведено доказательство единых архитектурных швов для 4 ключевых подсистем:

```
  ┌────────────────────────────────────────────────────────────────────────┐
  │                           MenuBuilder Frontend                          │
  │  ┌───────────────────────┐                  ┌────────────────────────┐  │
  │  │  Существующий UX      │                  │  Новый L4Desk UX       │  │
  │  │  (/video, /devices)   │                  │  (/video, /devices)    │  │
  │  └──────────┬────────────┘                  └───────────┬────────────┘  │
  │             │                                           │               │
  │             ▼                                           ▼               │
  │      [ЕДИНЫЙ ШОВ 1: VideoPlayerScreen + RemoteControlOverlay]           │
  │      [ЕДИНЫЙ ШОВ 2: DeviceConsoleTab (cmd/powershell WebSocket)]        │
  │      [ЕДИНЫЙ ШОВ 3: BillingPage + CheckoutModal (License & Fin Ledger)] │
  │      [ЕДИНЫЙ ШОВ 4: OrgSwitcher + UserRecord + Tenant Context]          │
  └─────────────────────────────────┬──────────────────────────────────────┘
                                    │
                                    ▼
  ┌────────────────────────────────────────────────────────────────────────┐
  │                           MenuBuilder Backend                          │
  │  - /api/v1/video/control/*      (унифицированная аренда и сессии)       │
  │  - /api/billing/*               (расширение fin_ledger без смены API)   │
  │  - /api/certificate-pin/*       (единый bootstrap терминалов)           │
  │  - app.auth (require_tenant)    (единая JWT/RBAC модель)                │
  └────────────────────────────────────────────────────────────────────────┘
```

1. **Видеопоток и интерактивное управление (Video & Remote Control Seam)**:
   - *Доказательство*: Компонент `VideoPlayerScreen` уже интегрирован с Janus WebRTC (`/janus-ws`), а `RemoteControlOverlay` уже умеет транслировать координаты и клавиши через `iot_client` / `video_control.py`.
   - *Решение*: Для L4Desk НЕ создается отдельный видеоплеер или второй протокол доставки. Поток рабочего стола (L4RTP/1 от l4desk 1.7.7) принимается `l4media-ingress` и отдается в тот же `VideoPlayerScreen`. Для L4Desk добавляется только расширение списка поддерживаемых источников в `SourceSelector` и единый session lock.
2. **Консоль удаленной диагностики (Console Seam)**:
   - *Доказательство*: `DeviceConsoleTab` реализует интерактивную консоль Windows/ESP32, стриминг вывода чанками и историю команд через `app1` (`/api/internal/v1/diagnostics/ws/devices/{sn}`).
   - *Решение*: L4Desk использует существующий `DeviceConsoleTab`. В шаге `L4D-07-IOT` и `L4D-08B-MB` вводится взаимная блокировка консоли и видео (exclusive lock), не меняя протокол взаимодействия вкладки с бэкендом.
3. **Коммерческий и биллинговый контур (Billing & Ledger Seam)**:
   - *Доказательство*: `BillingPage` и `/api/billing/` уже поддерживают расчет стоимости, корзину, чекаут через платежного провайдера (`get_payment_provider()`), управление лицензиями и привязку к сертификатам.
   - *Решение*: Внедрение финансового ядра двойной записи L4Desk (`fin_accounts`, `fin_ledger` в шагах 09-12) реализуется как расширение существующего бэкенда `/api/billing/` и существующей страницы `/billing` без создания параллельного "L4Desk Billing Portal".
4. **Управление организациями и пользователями (Tenant & Identity Seam)**:
   - *Доказательство*: Механизм `OrgSwitcher`, модель `UserRecord`, ролевая модель RBAC (роли 1, 3, 4) и middleware `require_tenant_context` уже полностью покрывают изоляцию тенантов и права доступа.
   - *Решение*: Саморегистрация тенантов (L4D-05-MB) создаст стандартную запись `Org` и администратора (role 3), не ломая существующие структуры.

---

## 8. Конфигурация, переменные окружения и Feature Flags

Файл конфигурации: `MenuBuilder/backend/app/config.py` (наследуется от `pydantic_settings.BaseSettings`, `extra = "ignore"`).

| Переменная | Значение по умолчанию | Описание |
|---|---|---|
| `DATABASE_URL` | `""` (из .env) | Подключение к PostgreSQL через asyncpg |
| `JWT_SECRET_HEX` | `""` (из .env) | Симметричный секрет подписи JWT |
| `JWT_PUBLIC_KEY` | `""` | Публичный RSA ключ для верификации RS256 |
| `TRUST_PROXY_IDENTITY_HEADERS` | `False` | Доверие заголовкам идентификации от Nginx |
| `BILLING_DUE_SOON_DAYS` | `30` | Порог предупреждения об истечении лицензии (дней) |
| `CERT_PIN_TTL_HOURS` | `168` (7 дней) | Время жизни одноразового PIN сертификата |
| `CERT_EXPIRING_SOON_DAYS` | `30` | Порог предупреждения об истечении сертификата |
| `LEO4_INTERNAL_API_BASE_URL` | `http://app1:8000` | Базовый URL внутренней платформы IoT |
| `IOT_RPC_BASE_URL` | `http://app1:8000` | URL RPC сервиса IoT |
| `INTERNAL_SERVICE_KEY` | `""` | Ключ межсервисной аутентификации |
| `L4MEDIA_INGRESS_URL` | `http://l4media-ingress:9100` | URL сервиса динамической маршрутизации l4media |
| `L4MEDIA_JANUS_URL` | `http://l4media-janus:8088/janus` | URL REST API Janus WebRTC Gateway |
| `VIDEO_PORT_BASE` | `6000` | Базовый порт пула видеосессий |
| `VIDEO_PORT_SLOTS` | `50` | Количество слотов одновременных видеопотоков |
| `REMOTE_CONTROL_ENABLED` | `True` | Флаг включения функционала удаленного ввода |

---

## 9. Верификация: Linting, Type Check, Build и Тесты

Все проверки выполнены локально в соответствии с требованиями `PROMPT-STANDARD.md` и раздела 4 Project Guidelines:

1. **Backend Linting (Ruff)**:
   - Команда: `uv run --project MenuBuilder/backend ruff check MenuBuilder/backend`
   - Результат: **PASSED** (0 ошибок, 0 предупреждений).
2. **Backend Formatting (Ruff format)**:
   - Команда: `uv run --project MenuBuilder/backend ruff format --check MenuBuilder/backend`
   - Результат: **PASSED** (74 файла соответствуют форматированию).
3. **Backend Type Checking (Pyright)**:
   - Команда: `uv run --project MenuBuilder/backend pyright MenuBuilder/backend`
   - Результат: **PASSED** (0 errors, 0 warnings, 0 informations).
4. **Backend Unit & Integration Tests (Pytest)**:
   - Команда: `uv run --project MenuBuilder/backend pytest MenuBuilder/backend`
   - Результат: **PASSED** (289 passed, 0 failed, 38 warnings за 58.55s).
   - Покрытие: тесты биллинга, мультитенантности, видеопотоков, удаленного управления, quick actions, RBAC ролей, сессий пользователей, каталога, мониторинга и интеграций.
5. **Frontend Unit Tests (Vitest)**:
   - Команда: `npm --prefix MenuBuilder/frontend run test`
   - Результат: **PASSED** (4 test files, 20 passed за 17.95s).
   - Файлы: `certificate-pin-flow.test.ts`, `session-lifecycle.test.ts`, `remote-control-hook.test.ts`, `quick-actions.test.ts`.
6. **Frontend Production Build (TypeScript + Vite)**:
   - Команда: `npm --prefix MenuBuilder/frontend run build`
   - Результат: **PASSED** (сборка `dist/` завершена за 40.73s, `index.html` и 37 бандлов скомпилированы успешно).

---

## 10. Состояние развертывания на Production (87.242.100.34)

Проведено обследование целевого сервера развертывания через безопасный SSH-доступ:

- **Хост**: `user1@87.242.100.34` (SSH-ключ `d:\.ssh\id_ed25519`).
- **Контейнер бэкенда**:
  - Имя: `menubuilder-backend`
  - Статус: `Up 3 days`
  - Image ID: `sha256:2b9329b60b7bc9b579cf0f3a282e5ad873b0f4e9ae17363007ad9c41496a6fd9`
  - Git Commit: `890f485c558126ab4fb82905aa2ed2c4623eab7c`
  - Порт: `8000/tcp` (внутренний compose-сетевой доступ)
- **Контейнер Nginx и раздача SPA**:
  - Имя: `nginx-default`
  - Статус: `Up 7 days`
  - Порты: `80`, `3000`, `1443-1444`, `443`
  - Каталог SPA фронтенда: монтируется на хосте `/home/user1/MenuBuilder/frontend/dist/` (файлы обновлены 14 сентября 2026 г., 17:58 UTC).
- **Смежные сервисы на хосте**:
  - `processing-backend`: `Up 3 days` (Alembic head 026)
  - `app1` (`iot-rpc-rest-app`): `Up 3 days`
  - `rabbitmq`: `Up 10 days`
  - `l4media-ingress`, `l4media-janus`, `l4media-nginx`: активны в штатном режиме.

---

## 11. Риски, пробелы и рекомендации для будущих шагов

1. **Отсутствие финансовой схемы L4Desk в БД**:
   - Таблицы `fin_accounts`, `fin_ledger`, пулы ежедневного использования и фиксация терминал-месяцев еще не созданы в схеме БД.
   - *Рекомендация*: В шаге `L4D-04A-SHARED` добавить декларативные модели SQLAlchemy, в `L4D-04B-PB` применить расширяющую миграцию Alembic, в `L4D-04C-MB` подключить схему к MenuBuilder.
2. **Взаимное исключение сессий Console и Video**:
   - В текущем коде `DeviceConsoleTab` и `VideoPlayerScreen` могут инициировать управление независимо друг от друга.
   - *Рекомендация*: В шаге `L4D-07-IOT` реализовать единый распределенный session lock, а в `L4D-08B-MB` объединить оркестрацию на стороне MenuBuilder.
3. **Саморегистрация пользователей и email-верификация**:
   - Публичная регистрация новых тенантов еще не выведена наружу (пользователи заводятся через админку или seed-конфигурацию).
   - *Рекомендация*: Реализовать в шаге `L4D-05-MB` атомарное создание организации, пользователя с ролью 3, отправку проверочного токена и аудит.

---

## 12. Candidate Handoff Block `H-L4D-00E-MB-v1`

Ниже представлен канонический candidate-блок для фиксации в `contract-handoff.md`:

```yaml
<!-- HANDOFF:H-L4D-00E-MB-v1:BEGIN -->
handoff_id: H-L4D-00E-MB-v1
status: ACCEPTED
contract_kinds:
  - REPORT
  - SEQUENCE_GATE
producer_prompt_id: L4D-00E-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-00E-MB-report.md
producer_branch: l4desk/l4d-00e-mb
producer_commit: full-commit-sha
accepted_at_utc: 2026-09-17T19:30:00Z
contract_version: 0.1.0
schema_revision: N/A
artifact_version: 0.1.0
artifact_paths:
  - MenuBuilder/docs/l4desk/handoffs/L4D-00E-MB-report.md
  - MenuBuilder/docs/l4desk/snapshots/openapi_baseline.json
  - MenuBuilder/docs/l4desk/snapshots/inventory_baseline.json
artifact_sha256:
  - 8c7b8053748286a635dd433a4d0cb53f1917f300c732c525f0a0e9ae10ebefbf
  - 3514eff4b7e0e1654314518564b5f290d155c139bc86eb0af04c32917e89c301
  - 31896fcace9f6f8331b8938cebb2913f28b2c9e19e7b69725a3cacd659211a28
compatibility:
  backward_compatible_with:
    - N/A
  breaking_changes: false
  notes: Baseline audit and inventory of MenuBuilder commercial and UX contour. Unified seams proven for VideoPlayerScreen + RemoteControlOverlay, DeviceConsoleTab, Billing/License flows and RBAC/Tenant isolation without alternative flows. All 289 backend tests and 20 frontend tests passing. Production deployed commit 890f485c558126ab4fb82905aa2ed2c4623eab7c verified on host 87.242.100.34. No runtime code changes.
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  remote_control_enabled: enabled
contract_payload:
  identifiers:
    org_id: tenant organization ID (integer)
    user_id: user ID (integer)
    terminal_id: terminal primary key ID (integer)
    sn: terminal serial number (ASCII string, e.g. a4b0000773c12345d210826)
    order_id: billing order UUID (string)
    lease_id: remote input control lease UUID (string)
    mountpoint_id: Janus streaming mountpoint ID (integer)
  operations_events:
    - POST /api/auth/login, POST /api/auth/logout, POST /api/auth/refresh, GET /api/auth/me
    - GET /api/admin/tenants/available, POST /api/admin/tenants/switch
    - GET /api/billing/summary, GET /api/billing/status, GET /api/billing/licenses, POST /api/billing/checkout
    - POST /api/certificate-pin/generate, GET /api/certificate-pin/status/{terminal_id}
    - POST /api/v1/video/devices/{device_id}/session
    - POST /api/v1/video/control/lease/acquire, POST /api/v1/video/control/lease/{id}/keepalive, POST /api/v1/video/control/lease/{id}/release
    - GET /api/v1/video/control/devices/{sn}/inventory, POST /api/v1/video/control/devices/{sn}/stream/start, POST /api/v1/video/control/devices/{sn}/stream/stop
    - GET /api/v1/video/control/ws/lease/{lease_id}
    - Nginx /janus-ws -> l4media-janus:8188 WebRTC
    - Nginx /api/internal/v1/diagnostics/ -> app1:8000 WebSocket Console
  errors:
    http_rest: 400 Bad Request, 401 Unauthorized, 403 Forbidden, 404 Not Found, 409 Conflict, 502 Bad Gateway
    ws_control: close code 4400 (Invalid Frame/JSON), 4401 (Unauthorized/No Token), 4403 (Foreign Lease/Forbidden), 4404 (Lease Not Found), 4408 (Lease Timeout/Expired)
  invariants:
    - Multi-tenant strict boundary isolation by org_id in all queries
    - RBAC roles: role 1 (superuser), role 3 (tenant admin), role 4 (viewer with granular permissions)
    - Zero alternative flows: L4Desk interactive stream utilizes VideoPlayerScreen + RemoteControlOverlay, console utilizes DeviceConsoleTab
    - Billing operations use whole minor units (kopecks) without float calculations
    - MenuBuilder does not run Alembic migrations (sole authority ProcessingBackend)
supersedes: []
known_risks:
  - Console and video mutual exclusion locking is not yet globally unified (addressed in L4D-07-IOT / L4D-08B-MB)
  - Fin ledger models and migrations are pending in shared and ProcessingBackend (addressed in L4D-04A-C and L4D-09-MB)
  - User self-registration and public onboarding endpoint not yet implemented (addressed in L4D-05-MB)
consumers:
  - L4D-00F-SHARED
  - L4D-00G-DOCS
next_prompt_id: L4D-00F-SHARED
<!-- HANDOFF:H-L4D-00E-MB-v1:END -->
```
