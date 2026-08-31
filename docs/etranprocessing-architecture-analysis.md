# Архитектурный анализ etranprocessing

**Дата среза:** 2026-08-31, актуализировано после внедрения и production-проверки  
**Граница решения:** `ProcessingBackend` + `MenuBuilder` + общие пакеты `shared/etranprocessing_db` и `shared/etranprocessing_gauge`.  
**Источники:** актуальный код, контейнерная конфигурация и профильные документы из `docs/`. Исключённые пользователем каталоги в анализ не входили.

## 1. Резюме

`etranprocessing` представляет собой не один монолит, а платформу из двух прикладных контуров над общей PostgreSQL-схемой:

- `ProcessingBackend` обслуживает доверенный терминальный трафик: идентификацию по mTLS-атрибутам, payment/tech-запросы, проверку активности и лицензии, выдачу меню, телеметрию и gauge-события.
- `MenuBuilder` обслуживает людей и администраторов: JWT-аутентификацию, multi-tenant управление меню и каталогом, терминалами, организациями, биллингом, мониторингом, отчётами и IoT-интеграциями.
- `shared/etranprocessing_db` является общим декларативным словарём данных, `shared/etranprocessing_gauge` — единым framework-independent deterministic gauge core, а `ProcessingBackend/backend/alembic` — единственной цепочкой изменения схемы.
- PostgreSQL связывает оба контура транзакционными данными, RabbitMQ/MQTT — оперативными gauge-событиями, а внутренний Leo4 IoT API — состоянием устройств и диагностикой. Владение таблицами и допустимые cross-domain чтения зафиксированы в `docs/database-ownership.md`.

Архитектура рационально разделяет machine-to-machine и human-facing нагрузки. После внедрения устранена неоднозначность владельца `ListMenuFile`, формализованы DB ownership и shared-schema rollout, а дублировавшийся gauge engine заменён общим пакетом; основной оставшийся источник связанности — общая PostgreSQL-схема.

## 2. Контекст и границы

```text
Платёжный терминал
    │ HTTPS, опубликованный :443
    ▼
Legacy TLS proxy (87.242.100.34)
    │ очищенные X-Client-Cert-* из TLS session
    ▼
Primary terminal ingress :4443 (176.108.247.249)
    │ forwarded identity только от trusted proxy IP
    ▼
ProcessingBackend ───────────────┐
    │ payment/menu/telemetry      │
    │ gauge events                │
    ▼                             ▼
PostgreSQL ◄──────────────► RabbitMQ/MQTT
    ▲                             ▲
    │ catalog/menu/billing        │ monitoring events
    │                             │
MenuBuilder backend ──────────────┘
    ▲             │ internal API v1
    │ /api + JWT  ▼
MenuBuilder nginx/frontend ──► Leo4 IoT platform
```

В состав развертывания также входит `ProcessingBackend/mcp-pin-server`, который изолирует PIN/certificate-инструменты от основного HTTP API. Он зависит от processing backend и подключён к общей PostgreSQL-сети, но не является владельцем предметной схемы.

## 3. Компоненты и ответственность

### 3.1 ProcessingBackend

Точка композиции `ProcessingBackend/backend/app/main.py` подключает независимые роутеры:

- `payment` — приём и проведение платежных запросов;
- `tech` — технические запросы терминалов;
- `menu` — terminal-facing `ListMenuFile`;
- `gate_gauge` и `gate_gauge_soap` — телеметрия и совместимые интерфейсы gauge-контура;
- `certificate_pin` — PIN/certificate операции;
- `health` — readiness/liveness API.

Критическая граница находится в `app/dependencies.py`: terminal identity формируется из переданных reverse proxy атрибутов сертификата, после чего проверяются терминал, организация, `terminal.is_active` и срок `license.expires_at`. TLS termination и формирование identity вынесены на edge, а предметная авторизация выполняется в приложении; пока строгая CA validation временно отключена, подлинность identity дополнительно зависит от trusted-hop filtering, header sanitization и сетевой недоступности backend в обход proxy.

### 3.2 MenuBuilder backend

Backend агрегирует API для:

- сессий и JWT, tenant/superuser контекста;
- пользователей, организаций и терминалов;
- меню, snapshots, terminal bindings, каталога и сервисов;
- billing orders, certificate PIN и административных billing-настроек;
- мониторинга, отчётности, интеграций и IoT-операций;
- MCP HTTP endpoint.

Предметные области вынесены в роутеры и сервисы. После рефакторинга `app/main.py` является только composition root (lifecycle, middleware и router wiring). Dashboard statistics, monitoring и reports размещены в `app/routers/dashboard.py`, `monitoring.py` и `reports.py`; публичные URL и response contracts сохранены.

### 3.3 MenuBuilder frontend и nginx

React/Vite SPA построена вокруг route-level экранов и типизированных API-клиентов. Nginx выполняет сразу три роли:

1. отдаёт статические assets и SPA fallback;
2. проверяет JWT для защищённых `/api/` маршрутов и передаёт claims backend;
3. проксирует часть `/api/internal/v1/` непосредственно в IoT-платформу с внутренним service key.

Такой edge-BFF подход уменьшает число hop для frontend, но делает nginx частью прикладной модели авторизации. Контракт имён claims и заголовков должен тестироваться как публичный интерфейс.

Контракт формализован: защищённые маршруты передают только `X-User-Id`, `X-Org-Id` и `X-User-Role`, legacy `jwt-*` очищаются, а public auth routes очищают все forwarded identity headers. Terminal-facing `ListMenuFile` удалён из MenuBuilder backend и nginx.

### 3.4 Общая модель данных

`shared/etranprocessing_db` содержит только SQLAlchemy-модели и отношения по доменам:

- auth: пользователи, сессии, API tokens;
- org: организации, статусы и billing settings;
- terminal: терминалы, типы, лицензии, certificate discovery/history;
- menu/catalog: группы, сервисы, версии, snapshots, bindings, категории и элементы каталога;
- billing: заказы, позиции и certificate PIN;
- payment: платежи, параметры, TSP и балансы;
- telemetry: tech/gauge records.

Thin-model подход соблюдён: бизнес-расчёты находятся в backend-сервисах. Alias `ServiceMenu = Service` показывает незавершённый слой обратной совместимости и должен иметь критерий удаления.

Gauge algorithm и in-memory `GaugeStore` не входят в DB layer: они вынесены в отдельный `etranprocessing-gauge` package без framework/runtime dependencies. Локальные `gauge_engine.py` оставлены только как thin compatibility exports, MQTT clients и lifecycle остаются специфичными для приложений.

## 4. Основные потоки

### 4.1 Терминальная авторизация и обработка запроса

1. Edge принимает клиентский сертификат и формирует certificate identity headers из TLS session. На текущем переходном этапе строгая CA-валидация временно отключена (`optional_no_ca`), но отсутствие identity не даёт доступ к terminal endpoint.
2. Legacy proxy всегда перезаписывает входные `X-Client-Cert-*`; primary ingress принимает forwarded identity только от доверенного IP legacy proxy, а для прямого `:4443` использует собственную TLS session.
3. `get_current_terminal` связывает сертификат с терминалом и фиксирует discovery/audit состояние.
4. Предметный доступ определяется активностью терминала, статусом организации и сроком лицензии.
5. Роутер выполняет payment, tech или menu use case в асинхронной транзакции SQLAlchemy.

Главный инвариант: только доверенный proxy может формировать `X-Client-Cert-*`; прямой сетевой доступ к backend закрыт container network. Прямой опубликованный `:4443` не доверяет пользовательским certificate headers. Application boundary требует одновременно DN и serial и затем проверяет terminal/org/license state.

### 4.2 Управление меню

1. Пользователь входит через MenuBuilder и получает JWT с user/organization/role claims.
2. Backend применяет tenant scope к каталогу, сервисам, версиям меню и terminal bindings.
3. Публикация формирует версионированное состояние и snapshot.
4. Терминал получает предназначенное ему меню через terminal-facing API.

Snapshots и распространение версии до binding дают основу для аудита и воспроизводимости. Владелец `GET /api/ListMenuFile` — только `ProcessingBackend`: внешний `:443` exact location направляет запрос на primary `:4443`, а тот — в `processing-backend:8000`. MenuBuilder больше не содержит ни route, ни nginx location этого endpoint.

### 4.3 Биллинг и активность

Текущая модель разделяет два факта:

- `terminal.is_active` — административная доступность терминала;
- `license.expires_at` — оплаченный срок обслуживания.

`MenuBuilder` рассчитывает задолженность, режим renewal/restoration, прогноз и итог заказа. `ProcessingBackend` применяет состояние при terminal access. После Alembic migration `019` единственный контракт состояния — `terminal.is_active + license.expires_at`; `docs/billing-architecture.md` синхронизирован, а исторические упоминания `renewal_enabled` явно выведены из активного контракта.

### 4.4 Gauge и мониторинг

Оба backend используют `shared/etranprocessing_gauge` как единый deterministic engine и `GaugeStore`. Processing-контур принимает terminal telemetry, а MenuBuilder показывает агрегированное состояние. MQTT bus остаётся app-specific, поскольку зависит от settings/lifecycle; его клиенты в рамках изменения не модифицировались. Поведение core закреплено общими contract vectors и consumer regression tests обоих backend.

### 4.5 IoT и удалённая диагностика

MenuBuilder обращается к Leo4 через internal API v1. Для части browser-запросов nginx завершает пользовательский JWT, очищает `Authorization` и добавляет service key плюс tenant/user headers. Удалённая консоль и диагностика используют отдельный документированный MQTT/REST-RPC контракт.

## 5. Данные, транзакции и владение

### Сильные стороны

- Одна ORM-модель исключает расхождение типов и отношений между backend.
- Один Alembic owner предотвращает конкурирующие migration heads.
- Асинхронные сессии и явные роутеры поддерживают управляемую транзакционность.
- Tenant scope присутствует на auth boundary и в прикладных фильтрах.

### Ограничения

- Общая БД технически позволяет обращаться к таблицам другого домена; допустимые writers/readers и column-scoped co-writer исключения теперь зафиксированы в `docs/database-ownership.md`, но пока не обеспечиваются отдельными DB roles.
- Изменение shared models требует согласованного развертывания обоих backend.
- MenuBuilder reporting всё ещё содержит временные прямые read-only запросы к payment/telemetry доменам; теперь они локализованы в профильных routers и явно перечислены в ownership matrix.
- Column/use-case ownership документирован, но следующий уровень enforcement — отдельные DB grants/owner APIs.

## 6. Развёртывание и эксплуатационная модель

- `ProcessingBackend/docker-compose.yaml` запускает `processing-backend` и `mcp-pin-server` в общей внешней PostgreSQL-сети; processing backend также подключён к RabbitMQ-сети.
- `MenuBuilder/docker-compose.yaml` запускает backend и frontend/nginx; backend подключён к PostgreSQL и RabbitMQ, frontend — к backend и IoT-сети.
- Frontend `dist` подключён read-only volume, поэтому новая сборка assets становится доступна без пересборки nginx-контейнера.
- Миграции применяются только из processing-контейнера; при изменении shared schema оба backend должны работать с совместимыми версиями модели.
- `ListMenuFile` проходит через две опубликованные proxy-ступени: `87.242.100.34:443` → `176.108.247.249:4443` → `processing-backend:8000`. Exact locations и sanitization contract покрыты smoke/contract tests.
- Общий gauge package устанавливается в images обоих backend отдельной local package dependency; изменение не потребовало schema migration.

Обязательная последовательность schema deployment зафиксирована как expand → deploy compatible readers/writers → migrate/backfill → switch → contract. Историческая migration `019` потребовала атомарной координации; новые destructive changes до подтверждения совместимости обоих backend запрещены.

## 7. Архитектурные риски

| Приоритет | Риск | Последствие | Рекомендация |
|---|---|---|---|
| Контролируется | Owner `/api/ListMenuFile` | Regression routing может вернуть endpoint в MenuBuilder | Owner закреплён за ProcessingBackend, exact locations и route ownership tests обязательны |
| Высокий | Переходный `optional_no_ca` для terminal TLS | Криптографическая подлинность сертификата пока не гарантируется edge-слоем | Сохранить header sanitization/network isolation; отдельно вернуть strict CA validation после готовности цепочки сертификатов |
| Высокий | Shared database enforcement только соглашениями | Ошибочная cross-domain запись остаётся технически возможной | Ввести отдельные DB roles/grants и owner APIs, следовать `docs/database-ownership.md` |
| Контролируется | Gauge core | Consumer может обойти общий package | Общие vectors, direct package imports и compatibility exports проверяются в CI |
| Контролируется | MenuBuilder composition root | Новая прикладная логика может вернуться в `main.py` | `main.py` оставлять wiring-only; use cases размещать в routers/services/repositories |
| Контролируется | JWT/certificate header contracts | Изменение nginx claims может разойтись с FastAPI | Канонические headers и spoofing/tenant tests обязательны |
| Средний | Historical billing/routing документы | Старый план может быть принят за активный контракт | Authoritative документы содержат owner/date, исторические поля помечены как архивные |
| Низкий | Compatibility alias `ServiceMenu` без срока удаления | Миграционный API становится постоянным | Найти потребителей и завести критерий удаления |

## 8. Целевая эволюция

### Реализовано 2026-08-31

1. `ListMenuFile` закреплён за ProcessingBackend и удалён из MenuBuilder.
2. Добавлены contract/smoke tests для nginx routing/header sanitization, tenant switching и terminal certificate forwarding; production probes без certificate data и со spoofed headers возвращают `401` на `:443` и `:4443`.
3. Опубликованы ownership matrix для 28 таблиц и expand → migrate → contract правило shared schema.
4. `MenuBuilder/app/main.py` сокращён до composition root; dashboard, monitoring и reporting вынесены в профильные routers.
5. Billing-документация синхронизирована с `terminal.is_active + license.expires_at`.
6. Gauge engine и store вынесены в отдельный `etranprocessing-gauge`; тесты: gauge core `5`, ProcessingBackend `74`, MenuBuilder `170` passed, Ruff/Pyright без ошибок.

### Среднесрочно

1. Снизить связанность через БД: междоменные записи выполнять через API/use-case owner, сохраняя shared package декларативным.
2. Ввести техническое enforcement ownership через DB roles/grants и owner API для междоменных записей.
3. Ввести versioned internal contracts для IoT API и событий RabbitMQ/MQTT.
4. Добавить наблюдаемость сквозных потоков: correlation id от proxy до SQL/event logs и метрики отказов auth/license/routing.
5. После подготовки доверенной CA chain вернуть строгую client certificate validation на обоих terminal ingress.

## 9. Оценка текущего состояния

- **Разделение пользовательского и терминального контуров:** хорошее.
- **Единообразие модели данных:** хорошее, с повышенной deployment-связанностью.
- **Tenant isolation:** реализовано в ключевых границах, требует систематических negative tests.
- **Edge security:** header sanitization, trusted-hop forwarding и negative probes реализованы; CA validation остаётся осознанно переходной (`optional_no_ca`).
- **Модульность ProcessingBackend:** хорошая.
- **Модульность MenuBuilder backend:** хорошая; `main.py` является wiring-only composition root.
- **Документальная согласованность:** хорошая для routing, DB ownership, shared schema и billing terminology; historical plans не являются активным контрактом.

Итог: bounded responsibilities формализованы и подтверждены production-деплоем. `ListMenuFile` имеет одного владельца, header contracts проверяются отрицательными сценариями, DB ownership документирован, composition root разгружен, gauge core унифицирован. Следующий архитектурный выигрыш дадут техническое enforcement DB ownership, versioned internal contracts, end-to-end observability и возврат строгой CA validation.
