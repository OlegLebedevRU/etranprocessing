# Целевая архитектура многотенантной аутентификации и авторизации (MenuBuilder)

## 1. Обзор и принципы

В платформе **etranprocessing / MenuBuilder** пользователи взаимодействуют с тенантными сущностями (терминалы, варианты меню, группы услуг, биллинг, отчёты).
Платформа разграничивает доступ между обычными пользователями организаций и суперпользователями (администраторами платформы), которым требуется переключаться между организациями (тенантный контекст) и работать на платформенном уровне.

### Ключевые архитектурные принципы:
1. **Tenant-in-token (Вариант A)**: `orgId` является неотъемлемым claim'ом JWT. Nginx (`auth_jwt`), backend MenuBuilder, ProcessingBackend и любые downstream-сервисы считывают тенант напрямую из проверенного токена без дополнительных запросов в БД.
2. **Единый контур доверия (RS256 External Issuer)**: Все токены выпускаются внешним JWT issuer'ом (Yandex Cloud Function). Локальная подпись токенов в production исключена.
3. **Один активный токен у клиента**: У клиента всегда ровно один рабочий токен. Понятие «локального master-токена» устранено: платформенный контекст суперпользователя — это токен с claim `orgId=0, is_superuser=true`.
4. **Правило авторизации №1 (Права по claim)**: Доступ к суперпользовательским ручкам (`/api/admin/*`, `/api/devices/*`) проверяется исключительно по флагу `is_superuser` (через `require_superuser`), а не по `token_type`. Суперпользователь внутри тенанта сохраняет доступ к административным сервисам без необходимости переключения токенов.
5. **Правило авторизации №2 (Изоляция тенанта)**: Тенант для обычного пользователя определяется строго из токена (`resolve_org_id`). Любой `org_id` из query/body/headers валидируется: при несовпадении с активным токеном возвращается 403 Forbidden.
6. **Сессия — источник истины про активный тенант**: Активный тенант сохраняется в серверной сессии (`user_sessions.active_org_id`). При `POST /auth/refresh` тенант берётся из `session.active_org_id`, что исключает сброс тенанта у суперпользователя при истечении access-токена.

---

## 2. Модель токенов и таблица claims

Все токены подписываются алгоритмом **RS256** внешним issuer'ом с публичным ключом, проверяемым в Nginx и backend.

| Токен | Кому выдаётся | Ключевые claims | Назначение и доступ |
|---|---|---|---|
| **Tenant token (Обычный)** | Обычный пользователь | `sub`, `userId`, `orgId=<fixed>`, `role="user"`, `is_superuser=false` | Доступ исключительно к тенантным API (`/api/menu/*`, `/api/reports/*`, `/api/billing/*`, `/api/monitoring`) своей организации. |
| **Platform token (Суперюзер)** | Суперпользователь вне тенанта | `sub`, `userId`, `orgId=0`, `role="superuser"`, `is_superuser=true` | Платформенные API (`/api/admin/*`, `/api/devices/*`, `/api/auth/*`). Тенантные ручки возвращают 403 «Выберите организацию». |
| **Tenant token (Impersonated)** | Суперпользователь в тенанте | `sub`, `userId`, `orgId=X`, `role="superuser"`, `is_superuser=true`, `is_imp=true`, `orig_sub` | Полный доступ к тенантным ручкам организации `X` **и** одновременный доступ к платформенным `/api/admin/*`. Мутации логируются для аудита. |

---

## 3. Транспорт токенов и безопасность (HttpOnly Cookie + CSRF)

### 3.1. HttpOnly Cookie как единственный транспорт браузера
Браузерный клиент (`MenuBuilder/frontend`) не хранит токены в `localStorage` (защита от XSS-атак).
- При `login`, `refresh`, `switch`: сервер устанавливает HttpOnly cookie `accessToken` (`path=/`, `SameSite=Lax`, `max_age=expires_in`, `Secure` при HTTPS / `X-Forwarded-Proto: https`).
- `refreshToken` передается в HttpOnly cookie с ограничением пути `path=/api/auth`.
- Заголовок `Authorization: Bearer <token>` поддерживается на бэкенде для API-клиентов, внешних интеграций и тестов.

### 3.2. CSRF-защита
Для защиты от межсайтовой подделки запросов (CSRF) в `app/main.py` действует middleware:
- Для всех мутирующих запросов (`POST`, `PUT`, `PATCH`, `DELETE`), аутентифицированных через cookie (при отсутствии заголовка `Authorization: Bearer`), обязательно требуется наличие заголовка:
  `X-Requested-With: XMLHttpRequest`
- Запросы без этого заголовка отклоняются с `403 Forbidden` (`detail: CSRF check failed`).
- Эндпоинты `/api/auth/login` и `/api/auth/refresh` исключены из проверки.

---

## 4. Диаграммы последовательности ключевых потоков

### 4.1. Вход в систему (Login Flow)
```
Пользователь                 Frontend                  Backend             JWT Issuer           PostgreSQL
    │                            │                        │                    │                     │
    │── Ввод логина/пароля ─────>│                        │                    │                     │
    │                            │── POST /auth/login ───>│                    │                     │
    │                            │                        │── Аутентификация ───────────────────────>│
    │                            │                        │<─ UserRecord ────────────────────────────│
    │                            │                        │   (last_org_id)                          │
    │                            │                        │                                          │
    │                            │                        │── Single-flight issue_tokens(orgId) ────>│
    │                            │                        │<─ {accessToken, refreshToken} ───────────│
    │                            │                        │                                          │
    │                            │                        │── create_session(active_org_id) ────────>│
    │                            │<── 200 OK + Set-Cookie │                                          │
    │                            │    (accessToken,       │                                          │
    │                            │     refreshToken)      │                                          │
    │                            │── GET /auth/me ───────>│                                          │
    │                            │<── 200 UserInfo ───────│                                          │
    │<── Переход в систему ──────│   (SessionContext)     │                                          │
```

### 4.2. Переключение тенанта суперпользователем (Tenant Switch Flow)
```
Суперюзер                   Frontend                  Backend             JWT Issuer           PostgreSQL
    │                            │                        │                    │                     │
    │── Выбор Org X в Switcher ─>│                        │                    │                     │
    │                            │── POST /admin/switch ─>│                    │                     │
    │                            │   {org_id: X}          │── Проверка session по refreshToken ─────>│
    │                            │                        │── Проверка Org X (если X > 0) ──────────>│
    │                            │                        │                                          │
    │                            │                        │── issue_tokens(orgId=X, is_imp=true) ───>│
    │                            │                        │<─ {accessToken} ─────────────────────────│
    │                            │                        │                                          │
    │                            │                        │── set_session_active_org(session, X) ───>│
    │                            │                        │── set_user_last_org(user, X) ───────────>│
    │                            │                        │   (БЕЗ создания новой сессии!)           │
    │                            │<── 200 OK + Set-Cookie │                                          │
    │                            │    (обновлен           │                                          │
    │                            │     accessToken)       │                                          │
    │                            │── BroadcastChannel ───>│ (Остальные вкладки перезагружаются)
    │                            │   "tenant-switched"    │                                          │
    │<── Обновление контекста UI─│                        │                                          │
```

### 4.3. Фоновое и реактивное обновление (Silent & Reactive Refresh)
```
Frontend Timer               Frontend                  Backend             JWT Issuer           PostgreSQL
    │                            │                        │                    │                     │
    │── Таймер: exp - 300с ─────>│                        │                    │                     │
    │   (или visibilitychange)   │── POST /auth/refresh ─>│                                          │
    │                            │   (с cookie)           │── Чтение session.active_org_id ─────────>│
    │                            │                        │<─ active_org_id = X ─────────────────────│
    │                            │                        │                                          │
    │                            │                        │── issue_tokens(orgId=X) ────────────────>│
    │                            │                        │<─ {new accessToken, refreshToken} ───────│
    │                            │                        │                                          │
    │                            │                        │── touch_session / rotate ───────────────>│
    │                            │<── 200 OK + Set-Cookie │                                          │
    │                            │    (accessToken)       │                                          │
    │── Перезапуск таймера ──────│                        │                                          │
```

---

## 5. Иерархия зависимостей FastAPI

```
                        ┌──────────────────────────────┐
                        │      get_current_user        │
                        │   (Декодирует RS256 JWT,     │
                        │    проверяет cookie/Bearer,  │
                        │    логирует аудит имперсонац)│
                        └──────────────┬───────────────┘
                                       │
                       ┌───────────────┴───────────────┐
                       ▼                               ▼
        ┌──────────────────────────────┐ ┌──────────────────────────────┐
        │      require_superuser       │ │    require_tenant_context    │
        │ (Проверяет claim is_superuser│ │  (Требует org_id > 0,        │
        │  или роль superuser/admin)   │ │   иначе 403 "Выберите орг")  │
        └──────────────┬───────────────┘ └─────────────┬────────────────┘
                       │                               │
                       ▼                               ▼
        - GET  /api/admin/tenants/*      - /api/menu-variants/*
        - GET  /api/admin/organizations  - /api/groups/*, /api/services/*
        - GET  /api/admin/terminals/*    - /api/reports/* (resolve_org_id)
        - CRUD /api/devices/*            - /api/monitoring, /api/catalog/*
```

---

## 6. Отвергнутые альтернативы

### Альтернатива B: Server-Side Tenant Context (Контекст на стороне сервера)
- **Суть предложения:** Access JWT выпускается только с идентификатором пользователя (`userId`), а активная организация (`orgId`) хранится исключительно в сессии сервера или передается пользовательским заголовком `X-Org-Id`.
- **Причины отказа:**
  1. **Нарушение самодостаточности токена:** Downstream-сервисы (ProcessingBackend, сервисы отчетов, Nginx mTLS gate) не могут доверять заголовку клиента без запроса в базу данных или обращения к сессионному хранилищу, что создает узкое горлышко и ломает stateless-архитектуру микросервисов.
  2. **Риск подмены тенанта (Tenant Spoofing):** При доверии заголовкам клиента на периметре Nginx требуется сложная логика валидации прав пользователя на каждой точке входа.
  3. **Проблемы распределенного аудита:** В логах Nginx и микросервисов claims токена однозначно фиксируют тенант и факт имперсонации (`is_imp`, `orig_sub`), что критично для финтех-процессинга и требований безопасности.
- **Итог:** Выбран вариант **A (Tenant-in-token)**, а задержка внешнего issuer'а (2–3 сек) нейтрализована увеличением TTL до 60 мин, упреждающим фоновым обновлением (silent refresh) и single-flight дедупликацией запросов.
