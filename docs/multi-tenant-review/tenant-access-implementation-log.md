# Журнал реализации: Целевая архитектура доступа фронтенда к API в разрезе тенанта (MenuBuilder)

## Шаг 0. Разведка и baseline

**Дата:** 2026-09-03  
**Статус:** Выполнен  

### 1. Nginx конфигурация (п. 2)
- Файл: `MenuBuilder/nginx.conf`
- Настройки JWT:
  - `auth_jwt_enabled on;`
  - `auth_jwt_algorithm RS256;`
  - `auth_jwt_location COOKIE=accessToken;`
  - `auth_jwt_key` содержит открытый ключ RS256 внешнего JWT issuer'а.
- Маршруты:
  - `/api/auth/`: `auth_jwt_enabled off;`, зачищаются заголовки `X-User-Id`, `X-Org-Id`, `X-User-Role`, `jwt-*`.
  - `/api/billing/` и `/api/`: токен извлекается из `COOKIE=accessToken`, директива `auth_jwt_extract_var_claims sub org role userId orgId roleId;` форвардит в backend `X-User-Id $jwt_claim_userId;`, `X-Org-Id $jwt_claim_orgId;`, `X-User-Role $jwt_claim_role;`, `X-Role-Id $jwt_claim_roleId;`.
  - Защита антиспуфинга: Nginx зачищает клиентские заголовки перед передачей, однако backend (`app/auth.py`) при прямом доступе (в обход Nginx) доверяет заголовкам `X-User-Id` без проверки токена (дефект D1).
  - `/api/mcp/`: токен извлекается из `HEADER=Authorization`, проверяется Nginx, форвардится только `X-Auth-Jti`.

### 2. Конфигурация пользователей и o.lebedev (п. 3)
- В `MenuBuilder/.env` и `MenuBuilder/backend/tests/conftest.py`:
  - `AUTH_USERS=[{"username":"o.lebedev","md5_password":"...","org_id":1,"role":"superuser","is_superuser":true},{"username":"test","md5_password":"...","org_id":1,"role":"user","is_superuser":false}]`
  - Пользователь `o.lebedev` уже явно сконфигурирован с `"role":"superuser"` и `"is_superuser":true`.
  - Удаление хардкода `or username == "o.lebedev"` из кода не нарушит его привилегии в dev/production при корректном `AUTH_USERS`.

### 3. Baseline проверок и тестов (п. 4)
- `pytest` (`MenuBuilder/backend`): 178 passed (12 warnings) за 81.74s. Все тесты зелёные.
- `ruff check .` (`MenuBuilder/backend`): All checks passed.
- `pyright .` (`MenuBuilder/backend`): 0 errors, 0 warnings.
- `npm run build` (`MenuBuilder/frontend`): сборка успешна (4944 modules transformed, vite v6.4.3).

### 4. Классификация роутеров backend (п. 5)
Вход для Шага 6:

| Роутер | Префикс | Категория | Текущая зависимость | Примечание |
|---|---|---|---|---|
| `auth.py` | `/api/auth` | Auth / Public | `/login`, `/refresh` (public/cookie); `/logout`, `/me` (`get_current_user`) | Требуется сохранять единый токен и убрать master token |
| `admin_organizations.py` | `/api/admin/organizations` | Суперюзерский | `dependencies=[Depends(require_superuser)]` | Только платформа |
| `admin_terminals.py` | `/api/admin` | Суперюзерский | `dependencies=[Depends(require_superuser)]` | Только платформа |
| `admin_tenants.py` | `/api/admin/tenants` | Суперюзерский | `Depends(require_superuser)` | `/available`, `/switch` |
| `admin_users.py` | `/api/admin/users` | Суперюзерский | `Depends(require_superuser)` на всех эндпоинтах | Управление пользователями и сессиями |
| `billing.py` | `/api/billing` | Тенантный | `Depends(get_current_billing_user)` | Требует `org_id > 0` |
| `groups.py` | `/api/groups` | Тенантный | `Depends(get_current_user)` | Ad-hoc проверки `org_id`, нет `require_tenant_context` |
| `services.py` | `/api/services` | Тенантный | `Depends(get_current_user)` | Ad-hoc проверки `org_id`, нет `require_tenant_context` |
| `catalog.py` | `/api/catalog` | Тенантный | `Depends(get_current_user)` | Ad-hoc проверки `org_id`, нет `require_tenant_context` |
| `menu_variants.py` | `/api/menu-variants` | Тенантный | `Depends(get_current_user)` | Ad-hoc проверки `org_id`, нет `require_tenant_context` |
| `terminal_bindings.py` | `/api` | Тенантный | `Depends(get_current_user)` | Ad-hoc проверки `org_id`, нет `require_tenant_context` |
| `dashboard.py` | `/api` | Тенантный | `Depends(get_current_user)` | Ad-hoc проверки `org_id`, нет `require_tenant_context` |
| `monitoring.py` | `/api` | Тенантный | `Depends(get_current_user)` | Ad-hoc проверки `org_id`, нет `require_tenant_context` |
| `reports.py` | `/api/reports` | Тенантный | `Depends(get_current_user)` | Ad-hoc проверки `org_id`, нет `require_tenant_context` |
| `integrations.py` | `/api/integrations` | Гибридный | `Depends(get_current_user)` | Использует локальный хелпер `_resolve_target_org_id` |
| `profile.py` | `/api/profile` | Пользовательский | `Depends(get_current_user)` | Профиль и персональные токены |
| `mcp_proxy.py` | `/api/mcp` | Внутренний MCP | `_validate_jti` (`X-Auth-Jti`) | Проксирование к MCP |

**Итог Шага 0:** Baseline зафиксирован. Кодовая база в полностью рабочем состоянии. Переходим к Шагу 1.

---

## Шаг 1. Backend hardening без изменения контрактов (D1, D2, D3)

**Дата:** 2026-09-03  
**Статус:** Выполнен  

### Что изменено:
1. `MenuBuilder/backend/app/config.py`:
   - Добавлен флаг `trust_proxy_identity_headers: bool = False`.
2. `MenuBuilder/backend/app/auth.py`:
   - В функции `get_current_user` ветка доверия заголовкам `X-User-Id`, `X-Org-Id`, `X-User-Role` теперь выполняется исключительно при `settings.trust_proxy_identity_headers is True`. Без токена по умолчанию возвращается `401 Unauthorized` (`Authentication required`).
   - Убран захардкоженный суперюзер `or username == "o.lebedev"` из вычисления `is_su`. Признак суперюзера определяется только из `is_superuser`, `role in ("superuser", "admin")` или `role_id == 1`.
3. `MenuBuilder/backend/app/user_store.py`:
   - В `ConfigUserStore.get_by_username` убран хардкод `or username == "o.lebedev"`. Роль и права считываются строго из конфигурации (`settings.auth_users`).
4. `MenuBuilder/backend/app/services/jwt_issuer.py`:
   - В `JwtIssuerClient` реализован single-flight механизм на базе `dict[tuple[int, int], asyncio.Future]`: одновременные вызовы `issue_tokens` для одинаковой пары `(user_id, org_id)` дедуплицируются и ожидают результат одного исходящего HTTP-запроса.
   - Реализована логика одного retry при `httpx.RequestError` или 5xx с паузой 0.5 с.
   - Добавлено логирование длительности вызова: `logger.info("jwt issuer call user_id=%s org_id=%s took=%.2fs")`.
5. Тесты:
   - В `tests/test_tenant_switch.py` тест `test_canonical_nginx_headers_preserve_switched_tenant_context` обновлен: проверено, что без токена при `trust_proxy_identity_headers=False` возвращается 401, а при `True` — 200 с контекстом пользователя.
   - Добавлен тест `test_user_store_no_hardcoded_superuser` (пользователь `o.lebedev` без флага `is_superuser` в конфиге не является суперюзером).
   - В `tests/test_jwt_issuer.py` добавлены тесты `test_single_flight_deduplication`, `test_jwt_issuer_retry_on_5xx`, `test_jwt_issuer_retry_on_network_error`.

### Что проверено:
- `pytest` (`MenuBuilder/backend`): 182 passed за 78.83s (все 182 теста зелёные).
- `ruff check .` (`MenuBuilder/backend`): All checks passed.
- `pyright .` (`MenuBuilder/backend`): 0 errors, 0 warnings.
- `npm run build` (`MenuBuilder/frontend`): сборка успешна (35.91s).

### Что осталось:
- Шаг 2: Сессия — источник истины про активный тенант (`active_org_id`, `last_org_id`, миграция Alembic в ProcessingBackend).

---

## Шаг 2. Сессия — источник истины про активный тенант (D4, D5)

**Дата:** 2026-09-03  
**Статус:** Выполнен  

### Что изменено:
1. `shared/etranprocessing_db/models/auth.py`:
   - В модель `User` добавлено поле `last_org_id: Mapped[int | None] = mapped_column(Integer, nullable=True)`.
   - В модель `UserSession` добавлено поле `active_org_id: Mapped[int | None] = mapped_column(Integer, nullable=True, index=True)`.
   - Добавлен индекс `idx_user_sessions_active_org_id` на `user_sessions.active_org_id`.
2. Alembic-миграция:
   - Создана миграция `ProcessingBackend/backend/alembic/versions/021_add_session_active_org_and_user_last_org.py` (`upgrade` и `downgrade` для `active_org_id`, индекса и `last_org_id`).
3. `MenuBuilder/backend/app/user_store.py`:
   - `UserRecord`: добавлено поле `last_org_id: int | None = None`.
   - `DatabaseUserStore`:
     - `create_session` принимает параметр `active_org_id: int | None = None` и сохраняет его в БД и in-memory структуре.
     - `get_session_by_refresh_token` корректно возвращает сессию с `active_org_id`.
     - Добавлен метод `set_session_active_org(session_id, org_id)`.
     - Добавлен метод `set_user_last_org(user_id, org_id)` с in-memory кешированием `_in_memory_user_last_org` для устойчивости оффлайн/тестовых запусков без БД.
     - `ConfigUserStore.get_by_id` и `get_by_username` поддерживают `last_org_id`.
4. `MenuBuilder/backend/app/routers/auth.py`:
   - В `login`:
     - Для суперюзера вычисляется `initial_org_id = user.last_org_id or 0` (с валидацией активности организации в БД при наличии соединения). Для обычного пользователя проверяется `user.org_id` (если `None` или `<= 0` -> 403 «Пользователь не привязан к организации»).
     - Вызывается `create_session(..., active_org_id=initial_org_id)`.
   - В `refresh_token`:
     - Полностью удален блок декодирования истёкшего `accessToken`.
     - Источником активного тенанта для суперюзера является `session.active_org_id` (fallback на `user.last_org_id or 0`). Для обычного пользователя принудительно берется `user.org_id`.
     - При ротации refresh-токена новая сессия наследует `active_org_id`.
   - Разделены хелперы cookie: `_set_access_cookie` и `_set_refresh_cookie`.
5. `MenuBuilder/backend/app/routers/admin_tenants.py`:
   - `switch_tenant`:
     - Ищет существующую сессию по `refreshToken` cookie или телу запроса (если нет сессии — 401).
     - Вызывает issuer с целевым `org_id`.
     - Обновляет `active_org_id` текущей сессии через `set_session_active_org` и `last_org_id` пользователя через `set_user_last_org`.
     - НЕ создает новую сессию и НЕ перезаписывает `refreshToken` cookie. Обновляет только `accessToken` cookie через `_set_access_cookie`.
     - Поддерживает `org_id = 0` («Платформа», выход из тенанта без проверки Org в БД) с ответом `org_id=0, org_name="Платформа"`.
     - В схему ответа `SwitchTenantResponse` добавлены поля `is_superuser` и `is_impersonated`.
6. Тесты:
   - В `tests/test_tenant_switch.py`:
     - `test_admin_tenants_switch_endpoint`: проверена работа с существующей сессией и флаги `is_superuser`, `is_impersonated`.
     - Добавлен `test_step2_superuser_switch_refresh_and_session_count`: подтверждено сохранение `orgId=223` при refresh с истёкшим access token, неизменность количества сессий после switch, и работа `switch(0)` в платформенный контекст.
     - Добавлен `test_step2_superuser_login_with_last_org`: проверен вход суперюзера с `last_org_id=223` сразу в нужный тенант за один вызов issuer'а.
   - В `tests/test_timezone_and_reports.py`:
     - `test_switch_tenant_returns_timezone` обновлен для работы с активной сессией.

### Что проверено:
- `pytest` (`MenuBuilder/backend`): 184 passed за 53.31s (все 184 теста зелёные).
- `ruff check .` (`MenuBuilder/backend`): All checks passed.
- `pyright .` (`MenuBuilder/backend`): 0 errors, 0 warnings.
- `npm run build` (`MenuBuilder/frontend`): сборка успешна (38.40s).

### Что осталось:
- Шаг 3: Единый токен: удаление локального master token (D6, D7 частично).

---

## Шаг 3. Единый токен: удаление локального master token (D6, D7 частично)

**Дата:** 2026-09-03  
**Статус:** Выполнен  

### Что изменено:
1. `MenuBuilder/backend/app/routers/auth.py`:
   - В эндпоинтах `login` и `refresh_token` удалена локальная генерация master-токена (`create_master_token`). В схеме ответа `TokenResponse` поле `master_token` теперь возвращает `None` (deprecated для обратной совместимости).
2. `MenuBuilder/backend/app/auth.py`:
   - Функции `create_master_token`, `create_tenant_token` и `create_access_token` помечены как `@deprecated` (сохранены только для использования во вспомогательных тестах).
   - В зависимости `require_tenant_context` обновлена проверка: при `org_id in (None, 0)` или `org_id <= 0` возвращается `403 Forbidden` с точным текстом `detail="Выберите организацию"`.
   - В `require_superuser` подтверждена проверка прав исключительно по `is_superuser` и роли (`"superuser"`, `"admin"`), без привязки к `token_type`.
3. `MenuBuilder/frontend/src/api/adminTenants.ts`:
   - В интерфейс `SwitchTenantResponse` добавлены поля `is_superuser?: boolean` и `is_impersonated?: boolean`.
4. `MenuBuilder/frontend/src/routes/login.tsx`:
   - Удалено сохранение ключа `mb_master_token` в `localStorage`.
5. `MenuBuilder/frontend/src/routes/layout.tsx`:
   - Удалено обращение к `mb_master_token`.
   - Реализован платформенный режим (`isPlatformMode = Boolean(isSuperuser && currentUser?.org_id === 0)`):
     - В боковом меню при `org_id === 0` отображаются только суперюзерские разделы («Управление устройствами», «Администрирование»).
     - При переходе на тенантные разделы в платформенном контексте в рабочей области выводится подсказка «Выберите организацию».
6. `MenuBuilder/frontend/src/components/OrgSwitcher.tsx`:
   - В список вариантов выбора тенанта добавлен пункт `{ value: 0, label: "Платформа / выйти из тенанта" }`.
   - При `org_id === 0` бейдж отображается золотым цветом с текстом «Платформа».
   - Функция `handleSwitch` корректно обрабатывает выбор `selectedOrgId === 0` и вызывает `switchTenant(0)`.
7. Тесты:
   - В `MenuBuilder/backend/tests/test_tenant_switch.py`:
     - Добавлен тест `test_step3_admin_accessible_with_impersonated_tenant_token`: подтверждено Правило авторизации №1 (суперюзерские ручки доступны с токеном имперсонации `orgId=223, is_superuser=True`).
     - Добавлен тест `test_step3_require_tenant_context_detail`: подтвержден возврат `403` с сообщением `detail="Выберите организацию"` для `org_id=0` и `org_id=None`.

### Что проверено:
- `pytest` (`MenuBuilder/backend`): 186 passed за 53.34s (все 186 тестов зелёные).
- `ruff check .` (`MenuBuilder/backend`): All checks passed.
- `pyright .` (`MenuBuilder/backend`): 0 errors, 0 warnings.
- `npm run build` (`MenuBuilder/frontend`): сборка успешна (37.47s).

### Что осталось:
- Шаг 4: TTL и проактивный silent refresh (D8, D9).

---

## Шаг 4. TTL и проактивный silent refresh (D8, D9)

**Дата:** 2026-09-03  
**Статус:** Выполнен  

### Что изменено:
1. `MenuBuilder/backend/app/config.py`:
   - `jwt_expire_minutes` увеличен с 15 до 60 минут (увеличение срока жизни access-токена в 4 раза).
   - `jwt_refresh_expire_days` увеличен с 7 до 30 дней.
2. Метрика вызовов issuer'а за час активной сессии (п. 4):
   - До изменений: при TTL 15 минут требовалось минимум 4 обращения к внешнему issuer'у в час на каждого активного пользователя (плюс повторные вызовы при каждом switch тенанта из-за пересоздания сессий).
   - После изменений: при TTL 60 минут требуется ровно 1 обращение к issuer'у в час активной сессии (сокращение нагрузки и сетевых задержек в 4 раза).
3. `MenuBuilder/backend/app/routers/auth.py` и `app/auth.py`:
   - В зависимости `get_current_user` поле `exp` из claim'ов токена передается в объект пользователя.
   - В схему `UserInfo` добавлено поле `expires_at: str | None = None`.
   - Эндпоинт `GET /auth/me` вычисляет и возвращает точное время истечения токена в формате ISO 8601 (`datetime.fromtimestamp(exp, tz=UTC).isoformat()`).
   - Гарантировано наличие целочисленного поля `expires_in` (в секундах) во всех ответах `login`, `refresh` и `switch`.
4. `MenuBuilder/frontend/src/api/session.ts`:
   - Реализована функция `scheduleRefresh(expiresInSec)`: таймер упреждающего фонового обновления токена запускается за 300 секунд (5 минут) до истечения access token (минимальный порог 30 секунд).
   - Обработка `visibilitychange`: при переключении вкладки в видимый режим проверяется оставшееся время жизни токена — если осталось менее 5 минут, инициируется немедленный silent refresh. В неактивных вкладках (`document.hidden`) таймер не нагружает сеть.
   - Межвкладочная синхронизация на базе `BroadcastChannel("mb-session")`: события `token-refreshed`, `tenant-switched`, `logout`. При наступлении `tenant-switched` соседние открытые вкладки автоматически перезагружаются в новом тенанте, а при `logout` перенаправляются на `/login`.
   - Предусмотрен feature flag отката `VITE_SILENT_REFRESH=false`.
5. Интеграция во фронтенде:
   - `src/api/client.ts`: реактивный refresh по 401 сохранен как fallback, после успешного обновления планируется следующий цикл `scheduleRefresh` и рассылается событие в канал.
   - `src/routes/login.tsx`: при успешном входе вызывается `scheduleRefresh(result.expires_in)`.
   - `src/components/OrgSwitcher.tsx`: при смене тенанта вызывается `scheduleRefresh(result.expires_in)` и отправляется событие `tenant-switched`.
   - `src/routes/layout.tsx`: при логауте транслируется событие `logout`.
   - `src/api/auth.ts`: в тип `UserInfo` добавлено поле `expires_at`.
6. Тесты:
   - В `MenuBuilder/backend/tests/test_tenant_switch.py` добавлен тест `test_step4_ttl_and_me_expires_at` (проверка значений TTL в настройках и корректности ISO-формата `expires_at` в `/auth/me`).

### Что проверено:
- `pytest` (`MenuBuilder/backend`): 187 passed за 50.57s (все 187 тестов зелёные).
- `ruff check .` (`MenuBuilder/backend`): All checks passed.
- `pyright .` (`MenuBuilder/backend`): 0 errors, 0 warnings.
- `npm run build` (`MenuBuilder/frontend`): сборка успешна (22.58s).

### Что осталось:
- Шаг 5: Один транспорт: HttpOnly cookie для браузера (D7, D10).
- Шаг 6: Аудит границы авторизации на backend (D11).
- Шаг 7: (Опционально) Кэш токенов issuer'а.
- Шаг 8: Документация и финальный отчёт (D12).

---

## Шаг 5. Один транспорт: HttpOnly cookie для браузера (D7, D10)

**Дата:** 2026-09-03  
**Статус:** Выполнен  

### Что изменено:
1. `MenuBuilder/backend/app/auth.py`:
   - В `get_current_user` удалена поддержка устаревшей cookie `access_token` (snake_case). Порядок извлечения токена: заголовок `Authorization: Bearer` -> cookie `accessToken`.
2. `MenuBuilder/backend/app/main.py`:
   - Добавлен `csrf_protection_middleware`: для мутирующих запросов (`POST`, `PUT`, `PATCH`, `DELETE`), аутентифицированных через cookie (при отсутствии заголовка Bearer), обязательно требуется заголовок `X-Requested-With: XMLHttpRequest`. При его отсутствии возвращается `403 Forbidden` (`{"detail": "CSRF check failed"}`).
   - Эндпоинты `/api/auth/login` и `/api/auth/refresh` исключены из проверки CSRF.
3. `MenuBuilder/backend/app/routers/auth.py` и `admin_tenants.py`:
   - Добавлен хелпер `_is_secure_request(request)`, проверяющий как `request.url.scheme == "https"`, так и заголовок прокси `X-Forwarded-Proto == "https"`.
   - В `_set_access_cookie` параметр `max_age` установлен строго равным `expires_in` (убран устаревший fallback `max(expires_in, 1800)`).
4. `MenuBuilder/frontend/src/session/SessionContext.tsx`:
   - Создан `SessionContext` и `SessionProvider` с хуком `useSession()`.
   - Реализована миграция старых клиентов: при старте приложения ключи `mb_token` и `mb_master_token` автоматически удаляются из `localStorage` (если не включен флаг `VITE_AUTH_TRANSPORT=bearer`).
   - Функция `refreshUser(forceFresh)` получает данные пользователя через `/auth/me` и сохраняет в контексте.
5. `MenuBuilder/frontend/src/api/client.ts`:
   - Добавлен заголовок по умолчанию `X-Requested-With: XMLHttpRequest`.
   - В интерцепторе запросов удалена автоматическая подстановка `Authorization: Bearer` из `localStorage` при дефолтном cookie-транспорте. Поддержка Bearer сохранена под флагом `VITE_AUTH_TRANSPORT=bearer`.
   - В интерцепторе 401 запросы на `/api/auth/refresh` выполняются с заголовком `X-Requested-With` и без сохранения токенов в `localStorage` в cookie-режиме.
6. `MenuBuilder/frontend/src/App.tsx`:
   - Роуты обернуты в `<SessionProvider>`.
   - Компонент `RequireAuth` переведен с проверки `localStorage.mb_token` на проверку `useSession().user` (источник `/auth/me`). При 401 выполняется редирект на `/login`.
7. `MenuBuilder/frontend/src/routes/`:
   - `login.tsx`: больше не записывает токены и флаги в `localStorage`. После успешного `/auth/login` вызывает `refreshUser(true)` и `scheduleRefresh(expires_in)`.
   - `layout.tsx`: читает состояние пользователя из `useSession()`. При логауте вызывает серверный `/auth/logout`, рассылает событие через `notifySessionEvent({ type: "logout" })` и перенаправляет на `/login`.
   - `OrgSwitcher.tsx`: переведен на `useSession().refreshUser(true)` без сохранения токенов в `localStorage`.
   - `admin-layout.tsx`, `devices/index.tsx`, `admin-terminals.tsx`: переведены на получение роли и активного тенанта из `useSession()`.
8. Тесты:
   - В `MenuBuilder/backend/tests/test_tenant_switch.py` добавлен тест `test_step5_csrf_and_cookie_auth`:
     - POST-запрос с cookie без `X-Requested-With` блокируется с кодом 403 (`detail: CSRF check failed`).
     - POST-запрос с cookie и `X-Requested-With: XMLHttpRequest` успешен (200).
     - POST-запрос с `Authorization: Bearer` без `X-Requested-With` успешен (200, API-клиенты не блокируются).
     - Cookie `access_token` (snake_case) игнорируется (401).
   - В `MenuBuilder/backend/tests/test_user_auth_and_sessions.py` тест `test_auth_refresh_and_logout` обновлен с передачей обязательного CSRF-заголовка для запроса logout.

### Что проверено:
- `pytest` (`MenuBuilder/backend`): 188 passed за 51.05s (все 188 тестов зелёные).
- `ruff check .` (`MenuBuilder/backend`): All checks passed.
- `pyright .` (`MenuBuilder/backend`): 0 errors, 0 warnings.
- `npm run build` (`MenuBuilder/frontend`): сборка успешна (23.79s).

### Что осталось:
- Шаг 6: Аудит границы авторизации на backend (D11).
- Шаг 7: (Опционально) Кэш токенов issuer'а.
- Шаг 8: Документация и финальный отчёт (D12).

---

## Шаг 6. Аудит границы авторизации на backend (D11)

**Дата:** 2026-09-03  
**Статус:** Выполнен  

### Что изменено:
1. `MenuBuilder/backend/app/auth.py`:
   - Реализована функция `resolve_org_id(user: dict, requested: int | None = None) -> int`:
     - Токен/сессия является единственным доверенным источником `org_id`.
     - При отсутствии активного тенанта (`org_id in (None, 0)` или `<= 0`) возвращается `403 Forbidden` с сообщением `detail="Выберите организацию"`.
     - Если клиент передает параметр `requested` и он не совпадает с `user["org_id"]`, генерируется `403 Forbidden` (как для обычных пользователей, так и для имперсонированных суперюзеров, работающих в выбранном тенанте).
   - Внедрен аудит действий имперсонированного суперюзера: для всех мутирующих запросов (`POST`, `PUT`, `PATCH`, `DELETE`) при наличии флага `is_impersonated` в лог записывается событие:
     `audit impersonated action user=%s orig_sub=%s org=%s method=%s path=%s`.
2. Аудит и обновление тенантных роутеров:
   - `reports.py`: все 4 эндпоинта (`/inkass`, `/payments`, `/balance-by-terminal`, `/balance-by-tsp`) переведены на `Depends(require_tenant_context)` и `resolve_org_id(user, org_id)`.
   - `dashboard.py`: эндпоинт `/stats` переведен на `Depends(require_tenant_context)` и `resolve_org_id(user)`.
   - `monitoring.py`: эндпоинт `/monitoring` переведен на `Depends(require_tenant_context)` и `resolve_org_id(user)`.
   - `groups.py`: эндпоинты списка, получения, создания, изменения и удаления групп переведены на `Depends(require_tenant_context)` и `resolve_org_id(user)`.
   - `services.py`: эндпоинты списка сервисов, свободных ТСП, создания, изменения и удаления сервисов переведены на `Depends(require_tenant_context)` и `resolve_org_id(user)`.
   - `catalog.py`: управление категориями и элементами каталога переведено на `Depends(require_tenant_context)` и `resolve_org_id(user)`.
   - `menu_variants.py`: эндпоинты вариантов меню переведены на `Depends(require_tenant_context)` и `resolve_org_id(user)`.
   - `terminal_bindings.py`: привязки терминалов и вариантов меню переведены на `Depends(require_tenant_context)` и `resolve_org_id(user)`.
   - `integrations.py`: защищены эндпоинты управления API-ключами с проверкой тенантных границ.
3. Тесты:
   - В `MenuBuilder/backend/tests/test_tenant_switch.py` добавлен тест `test_step6_authorization_matrix_and_audit`, покрывающий матрицу авторизации:
     - Обычный пользователь: совпадение org -> 200, несовпадение query org -> 403, админ-ручка -> 403.
     - Имперсонированный суперюзер: совпадение org -> 200, несовпадение query org -> 403, админ-ручка -> 200, мутации фиксируются в аудит-логе.
     - Суперюзер на уровне платформы (`org_id=0`): тенантная ручка -> 403 («Выберите организацию»), админ-ручка -> 200.

### Что проверено:
- `pytest` (`MenuBuilder/backend`): 189 passed за 51.32s (все 189 тестов зелёные).
- `ruff check .` (`MenuBuilder/backend`): All checks passed.
- `pyright .` (`MenuBuilder/backend`): 0 errors, 0 warnings.
- `npm run build` (`MenuBuilder/frontend`): сборка успешна (23.74s).

### Что осталось:
- Шаг 7: (Опционально) Кэш токенов issuer'а.
- Шаг 8: Документация и финальный отчёт (D12).

---

## Шаг 7. (Опционально, за флагом) Кэш токенов issuer'а

**Дата:** 2026-09-03  
**Статус:** Выполнен  

### Что изменено:
1. `MenuBuilder/backend/app/config.py`:
   - Добавлен флаг конфигурации `jwt_issuer_token_cache_enabled: bool = False` (по умолчанию отключен в соответствии с требованиями безопасности).
2. `MenuBuilder/backend/app/services/jwt_issuer.py`:
   - В `JwtIssuerClient` реализован in-process кэш токенов по ключу `(user_id, org_id) -> (token_data, exp_timestamp)`.
   - Кэшированный токен отдается только при условии, что до его истечения остается более 10 минут (`exp_timestamp - now > 600 с`).
   - Реализован метод `invalidate_cache_for_user(user_id: int)`, сбрасывающий кэш токенов для конкретного пользователя.
3. `MenuBuilder/backend/app/routers/auth.py`:
   - В эндпоинте `POST /api/auth/logout` при выходе пользователя вызывается инвалидация кэшированных токенов через `jwt_issuer_client.invalidate_cache_for_user(user_id)`.
4. Тесты:
   - В `MenuBuilder/backend/tests/test_jwt_issuer.py` добавлены:
     - `test_jwt_issuer_cache_hit_and_miss`: подтверждено сохранение токена в кэше, отсутствие повторного исходящего HTTP-запроса при одинаковом ключе и промах кэша при смене `org_id`.
     - `test_jwt_issuer_cache_invalidation`: подтверждена очистка кэша пользователя при инвалидации.
     - `test_jwt_issuer_cache_margin_expiration`: подтверждено, что токены с остатком жизни менее 10 минут признаются устаревшими и запрашиваются заново.

### Что проверено:
- `pytest` (`MenuBuilder/backend`): 192 passed за 52.34s (все 192 теста зелёные).
- `ruff check .` (`MenuBuilder/backend`): All checks passed.
- `pyright .` (`MenuBuilder/backend`): 0 errors, 0 warnings.
- `npm run build` (`MenuBuilder/frontend`): сборка успешна (24.16s).

### Что осталось:
- Шаг 8: Документация и финальный отчёт (D12).

---

## Шаг 9. Стабилизация и доработки switch-alias, auth hardening

**Дата:** 2026-09-03  
**Статус:** Выполнен  

### Что сделано:
1. Выполнены ручные изменения архитектуры Шага 9:
   - В `app/routers/admin_tenants.py`: реализован хелпер `_resolve_current_session` (приоритет: claim `sid` -> refresh token из тела/куки), добавлен `auth_alias_router` с маршрутом `POST /api/auth/switch-tenant` (решение ограничения path `/api/auth` для HttpOnly cookie `refreshToken`).
   - В `app/user_store.py`: добавлен метод `DatabaseUserStore.get_session_by_id()`.
   - В `app/main.py`: подключен `admin_tenants.auth_alias_router` под префиксом `/api`.
   - В `app/config.py`: включен флаг `jwt_verify_audience: bool = True`.
   - В `app/auth.py::decode_token`: пути fallback (HS256 с `mock_secret` и unverified-claims) теперь строго заблокированы при отключенном `jwt_issuer_mock_enabled`; RS256 проверяет `aud` и `iss`.
   - В `app/auth.py::get_current_user`: числовые роли ("1"/"2"/"3") нормализуются в строковые ("superuser", "admin", "user"); `sid` пробрасывается в словарь пользователя; `is_imp` берется из claim либо вычисляется как `is_superuser and org_id > 0`.
   - Во frontend: `api/adminTenants.ts` переведен на `POST /auth/switch-tenant`; `api/session.ts` перепланирует таймер по `token-refreshed` без прямого чтения `mb_token`; `session/SessionContext.tsx` планирует фоновое обновление по `expires_at` из `/auth/me`; `api/auth.ts::logout` упрощен.
2. Стабилизация и исправления (Фаза A):
   - Удалена паразитная строка-комментарий `# ... existing code ...` в `user_store.py`.
   - Исправлена обработка `request.method` в `app/auth.py` при аудите мутаций имперсонированных пользователей для безопасной работы с объектами `Request` без явного `method` в `scope`.
   - Добавлены переводы строк в конце файлов, форматирование кодовой базы через `ruff format`.
   - В `MenuBuilder/frontend/src/routes/login.tsx` сохранено обращение к `mb_token` строго под флагом `VITE_AUTH_TRANSPORT === "bearer"`.
   - Добавлены тесты в `tests/test_tenant_switch.py`:
     - `test_step9_switch_via_auth_alias_with_cookie`: успешное переключение в платформу (`org_id=0`) и тенант через `/api/auth/switch-tenant` с передачей CSRF-заголовка и проверкой `is_impersonated`.
     - `test_step9_switch_by_sid_claim_without_refresh_cookie`: помечен `xfail(strict=True)` до реализации передачи `sid` в `_generate_mock_tokens` в Шаге 10.
     - `test_step9_hs256_rejected_when_mock_disabled`: проверка отклонения (401) HS256 токенов при `jwt_issuer_mock_enabled=False`.
     - `test_step9_numeric_role_normalized`: нормализация числовых ролей ("1" -> superuser, "3" -> user).
     - `test_step9_is_imp_derived_for_v1_token`: вывод флага имперсонации `is_imp` для токенов v1 без явного claim.

### Что проверено:
- `pytest` (`MenuBuilder/backend`): 196 passed, 1 xfailed (sid switch) за 69.01s.
- `ruff check .` (`MenuBuilder/backend`): All checks passed.
- `ruff format --check .` (`MenuBuilder/backend`): All files formatted.
- `pyright .` (`MenuBuilder/backend`): 0 errors, 0 warnings.
- `npm run build` (`MenuBuilder/frontend`): сборка успешна (24.16s).
- Поиск `mb_token` в `src/`: только ветки под флагом `bearer` и зачистка в `SessionContext` / `client.ts`.

### Что осталось:
- Шаг 10 (Фаза B): Отправка v2-полей в issuer, зеркало mock к контракту issuer v2.
- Шаг 13 (Фаза C): Ротация сессии на месте.
- Шаг 11/12 (Фаза D): Деплой и smoke.
- Шаг 14 (Фаза E): Документация.

---

## Шаг 10. Backend отправляет v2-поля, mock = зеркало контракта issuer v2

**Дата:** 2026-09-03  
**Статус:** Выполнен  

### Что изменено:
1. `app/services/jwt_issuer.py`:
   - Метод `build_signed_request` расширен новыми опциональными параметрами: `username, is_superuser, is_imp, orig_sub, sid, access_ttl_minutes, refresh_ttl_days`. Опциональные поля сериализуются в camelCase в compact JSON `requestBody` (без пробелов) строго если значение не `None`. Верхнеуровневый словарь `params` не расширялся (issuer читает опциональные поля только из тела запроса). Подпись HMAC-SHA256 рассчитывается по обновленному `requestBody`.
   - В `issue_tokens` и `_issue_tokens_with_retry` добавлены kwargs `sid: int | str | None = None` и `orig_sub: str | None = None`. Автоматически вычисляется `is_imp = bool(is_superuser and (org_id or 0) > 0)` и передается в `build_signed_request` вместе с TTL (`jwt_expire_minutes`, `jwt_refresh_expire_days`).
   - Кэш токенов (`jwt_issuer_token_cache_enabled`): ключ кэша обновлен до тройки `(user_id, org_id or 0, sid)`, предотвращая отдачу токена с чужим `sid`. Single-flight дедупликация in-flight запросов сохраняет ключ `(user_id, org_id or 0)`. Метод `invalidate_cache_for_user` очищает все закэшированные варианты сессий пользователя.
   - Метод `_generate_mock_tokens` приведен к полному зеркальному соответствию контракту issuer v2:
     - `sub`: строго `str(user_id)`.
     - `role`: строго строковый идентификатор роли `str(role_id)`.
     - `org`: строго `str(effective_org_id)`.
     - `userId`, `orgId`, `roleId`: целочисленные значения.
     - Добавлены claims: `nbf`, `jti`, `token_type="tenant"`, `is_imp`, `is_superuser`, а также `username`, `orig_sub` и `sid` (если заданы).
     - Удалены дублирующие snake_case claims `user_id`, `org_id`, `role_id`.
   - Добавлены константы `ISSUER_V2_ACCESS_MANDATORY_CLAIMS` и `ISSUER_V2_ACCESS_CLAIMS`.
2. Роутеры:
   - `routers/auth.py::login`: передает `sid=None` в `issue_tokens` (сессия создается после вызова issuer'а; первый access-токен не несет `sid`, поэтому alias `/api/auth/switch-tenant` использует refresh-cookie).
   - `routers/auth.py::refresh_token`: передает `sid=session.id`.
   - `routers/admin_tenants.py::switch_tenant`: передает `sid=session.id` и `orig_sub=user.get("orig_sub") or username`.
   - `routers/auth.py::me`: если `username` в токене отсутствует или представлен числовым `sub`, а `user_id > 0`, подставляется имя пользователя из `get_user_store().get_by_id(user_id)`. В схему `UserInfo` добавлено поле `full_name: str | None = None`.
   - `MenuBuilder/frontend/src/api/auth.ts`: в тип `UserInfo` добавлено поле `full_name?: string | null`.
3. Тесты:
   - В `tests/test_jwt_issuer.py`:
     - Добавлен тест `test_mock_access_claims_match_issuer_v2_contract`, проверяющий строгое соответствие claims mock-токена контракту v2 и отсутствие устаревших дубликатов snake_case.
     - Добавлен тест `test_build_signed_request_v2_fields`: проверка включения v2 полей в `requestBody`, отсутствия полей со значением `None` и корректности HMAC-подписи.
     - Обновлены тесты кэша `test_jwt_issuer_cache_hit_and_miss` (проверка изоляции по `sid`) и `test_jwt_issuer_cache_margin_expiration`.
   - В `tests/test_tenant_switch.py`:
     - Снят маркер `xfail` с теста `test_step9_switch_by_sid_claim_without_refresh_cookie` (тест проходит успешно).
     - В `test_admin_tenants_switch_endpoint` обновлены ожидания согласно контракту v2 (`sub == "1"`, `username == "o.lebedev"`, `orgId == 223`).

### Что проверено:
- `pytest` (`MenuBuilder/backend`): 199 passed за 58.35s (все 199 тестов зелёные).
- `ruff check .` (`MenuBuilder/backend`): All checks passed.
- `ruff format --check .` (`MenuBuilder/backend`): All files formatted.
- `pyright .` (`MenuBuilder/backend`): 0 errors, 0 warnings.
- `npm run build` (`MenuBuilder/frontend`): сборка успешна (39.39s).

### Что осталось:
- Шаг 13 (Фаза C): Ротация сессии на месте.
- Шаг 11/12 (Фаза D): Деплой и smoke.
- Шаг 14 (Фаза E): Документация.

---

## Шаг 13. Ротация сессии на месте и фоновая очистка просроченных сессий

**Дата:** 2026-09-03  
**Статус:** Выполнен  

### Что изменено:
1. `app/user_store.py`:
   - Реализован метод `DatabaseUserStore.rotate_session(session_id: int, new_refresh_token: str, expires_in_seconds: int) -> None`:
     - Обновляет ту же строку в таблице `user_sessions`: `refresh_token`, `refresh_token_hash`, `expires_at`, `last_used_at`.
     - Значение `active_org_id` и первичный ключ `session.id` (`sid`) остаются неизменными на протяжении всей жизни сессии.
     - In-memory fallback: перекладывает запись под новый хэш с сохранением того же `id`.
   - Реализован метод `DatabaseUserStore.cleanup_expired_sessions() -> int`:
     - Выполняет запрос `DELETE FROM user_sessions WHERE (is_revoked AND created_at < now()-7d) OR expires_at < now()-7d`.
2. `app/routers/auth.py::refresh_token`:
   - Блок `revoke_session_by_token + create_session` заменен на вызов `store.rotate_session(session.id, new_refresh_token, refresh_expires_in)`.
   - Сессия пользователя больше не плодится при периодических запросах refresh.
3. `app/config.py`:
   - Добавлен флаг `session_cleanup_enabled: bool = True`.
   - В тестах (`tests/conftest.py`) флаг выключен по умолчанию.
4. `app/main.py::lifespan`:
   - Добавлена фоновая задача `_cleanup_expired_sessions_task`: при включенном `session_cleanup_enabled` раз в 6 часов выполняет очистку устаревших сессий с логированием результата и перехватом ошибок. Корректно отменяется при завершении приложения.
5. Тесты:
   - В `tests/test_tenant_switch.py`:
     - Добавлен тест `test_step13_sequential_refresh_preserves_session_id_and_active_org`: 3 последовательных вызова `/api/auth/refresh` подтверждают, что количество сессий не растет, `session.id` (`sid`) не меняется, а `active_org_id=223` надежно сохраняется в access-токене.
     - Добавлен тест `test_step13_cleanup_expired_sessions`: подтверждено удаление отозванных (>7 дней) и просроченных (>7 дней) сессий и сохранение активных.

### Что проверено:
- `pytest` (`MenuBuilder/backend`): 201 passed за 50.46s (все 201 тестов зелёные).
- `ruff check .` (`MenuBuilder/backend`): All checks passed.
- `ruff format --check .` (`MenuBuilder/backend`): All files formatted.
- `pyright .` (`MenuBuilder/backend`): 0 errors, 0 warnings.
- `npm run build` (`MenuBuilder/frontend`): сборка успешна (44.03s).

### Что осталось:
- Шаг 11/12 (Фаза D): Деплой и smoke.
- Шаг 14 (Фаза E): Документация.
