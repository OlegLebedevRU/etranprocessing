# ПРОМПТ: Целевая архитектура доступа фронтенда к API в разрезе тенанта (MenuBuilder)

## 0. Роль и правила работы

Ты — senior backend/frontend инженер, реализующий архитектурное решение в монорепозитории
`etranprocessing` (каталоги `MenuBuilder/backend` — FastAPI, `MenuBuilder/frontend` — React 19 +
TypeScript + Vite + Ant Design + Axios, `shared/etranprocessing_db` — общие SQLAlchemy-модели,
`ProcessingBackend/backend/alembic` — единственное место Alembic-миграций).

Правила:
1. Работай строго по шагам из раздела 4. Каждый шаг — отдельный коммит (или серия коммитов) с
   работоспособной системой в конце. Не начинай следующий шаг, пока критерии приёмки текущего не выполнены.
2. Перед правкой файла — прочитай его целиком. Перед правкой контракта API — найди всех потребителей
   (`MenuBuilder/frontend/src/api/*`, тесты в `MenuBuilder/backend/tests/*`).
3. Не ломай обратную совместимость внутри шага: старый клиент должен работать с новым backend до тех пор,
   пока шаг явно не говорит обратное.
4. После каждого шага: `cd MenuBuilder/backend && pytest`, `ruff check .`, (если настроен) `pyright`;
   `cd MenuBuilder/frontend && npm run build`. Падающие тесты — чинить, а не удалять; если тест
   закрепляет поведение, которое шаг сознательно меняет — переписать тест и объяснить в отчёте.
5. Не трогай контракт внешнего JWT issuer (`app/services/jwt_issuer.py::build_signed_request`) —
   это внешняя система. Issuer'ский `refreshToken` — непрозрачная строка, в issuer его не отправляем,
   он живёт только в нашей таблице `user_sessions`.
6. Не создавай цепочку миграций внутри `MenuBuilder`. Все миграции — в `ProcessingBackend/backend/alembic`
   согласно `docs/database-ownership.md` (владелец `users`, `user_sessions` — MB, но файлы миграций
   живут в PB). Только expand → migrate → contract.
7. Никогда не логируй токены, секреты, пароли. Не храни refresh token в `localStorage`.
8. В конце каждого шага пиши краткий отчёт (что изменено, что проверено, что осталось) в
   `docs/multi-tenant-review/tenant-access-implementation-log.md` (создать при отсутствии).
   Это защита от потери контекста между сессиями.

## 1. Исходные условия задачи (не подлежат изменению)

1. Внешний JWT issuer (RS256, Yandex Cloud Function) отвечает 2–3 секунды на выдачу токена.
2. Обычные пользователи имеют доступ ИСКЛЮЧИТЕЛЬНО к API с жёстким фильтром по одному тенанту (`org_id`).
3. Несколько суперюзеров должны переключаться в любой тенант И иметь доступ к выделенным суперюзерским
   сервисам (`/api/admin/*`, `/api/devices/*` и т.п.).
4. Суперюзер, переключившись в тенант, может работать там долго (часы/дни) — контекст не должен
   «слетать» сам по себе.
5. Частая ротация access/refresh JWT не требуется.

## 2. Целевая архитектура и ключевые выводы (СОХРАНИТЬ ИДЕЮ)

### 2.1. Главное решение: тенант живёт внутри JWT («tenant-in-token»)

Рассматривались два варианта:
- **A. Tenant-in-token** — `org` является claim'ом JWT; смена тенанта = новый токен от issuer.
- **B. Server-side context** — JWT только про identity, активный org хранится в серверной сессии
  или передаётся заголовком.

**Выбран вариант A.** Причины: токен самодостаточен — Nginx (`auth_jwt`), MenuBuilder backend,
ProcessingBackend и любые будущие downstream-сервисы читают `org` из claim'ов без похода в БД;
простой аудит; жёсткий фильтр для обычных пользователей — просто claim, который клиент не может подменить.
Цена варианта A — 2–3 с issuer'а на каждый switch/refresh. Вывод: **эту цену прячем в редкие и
фоновые операции** (длинный TTL + проактивный silent refresh + org из сессии), а не меняем модель.
Явный switch со спиннером 2–3 с — приемлемый UX; 2–3 с посреди рабочего запроса — нет.

### 2.2. Модель токенов

Все токены выдаёт **один и тот же внешний issuer**. Локальной подписи токенов в production быть не должно
(сейчас `create_master_token` подписывает локально приватным ключом либо `mock_secret` HS256 — это второй
контур доверия, который Nginx с публичным ключом issuer'а не проверит, и его надо убрать).

| Токен | Кому | Ключевые claims | Что открывает |
|---|---|---|---|
| Tenant token | обычный пользователь | `orgId=<fixed>`, `is_superuser=false`, `token_type=tenant` | только тенантные `/api/*` по своему `org` |
| Platform (master) token | суперюзер вне тенанта | `orgId=0`, `is_superuser=true` | `/api/admin/*`, `/api/auth/*`; тенантные ручки → 403 «выберите организацию» |
| Tenant token (impersonated) | суперюзер в тенанте | `orgId=X`, `is_superuser=true`, `is_imp=true`, `orig_sub` | тенантные ручки по X **и** `/api/admin/*` |

**Правило авторизации №1:** суперюзерские сервисы проверяют claim `is_superuser` (через
`require_superuser`), а НЕ `token_type`. Тогда суперюзер в тенанте не жонглирует двумя токенами:
у фронта всегда ровно **один активный токен**. «Master token» — это просто tenant token с `orgId=0`,
а не отдельная сущность. Поле `master_token` в ответах API и ключ `mb_master_token` во фронте — удалить.

**Правило авторизации №2:** `org` для обычного пользователя берётся ТОЛЬКО из токена. Любой `org_id`
из query/body/headers для не-суперюзера либо игнорируется, либо при несовпадении даёт 403.

### 2.3. Сессия — источник истины про активный тенант

- Одна серверная сессия (`user_sessions`) на один login. Switch тенанта НЕ создаёт новую сессию и
  НЕ переписывает refresh cookie — он обновляет поле `user_sessions.active_org_id`.
- `/auth/refresh` берёт `orgId` для issuer'а из `session.active_org_id`, а НЕ из текущего access token
  (который к моменту refresh обычно уже истёк и не декодируется → сейчас суперюзер молча вылетает из
  тенанта — прямое нарушение условия 4).
- В `users.last_org_id` сохраняется последний выбранный тенант суперюзера: при login issuer вызывается
  один раз с `orgId = last_org_id ?? 0`, и суперюзер сразу оказывается в привычном тенанте (один поход в
  issuer вместо двух).

### 2.4. Нейтрализация задержки issuer'а

1. Access TTL 60 минут (сейчас 15), refresh 7–30 дней — в 4 раза меньше походов в issuer.
2. Проактивный silent refresh на фронте по `expires_in`/`exp` за 3–5 минут до истечения (при активной
   вкладке и на `visibilitychange`), реактивный refresh по 401 с очередью — только fallback.
3. Single-flight в `JwtIssuerClient` по ключу `(user_id, org_id)` — несколько вкладок → один вызов issuer'а;
   один retry при сетевой ошибке.
4. (Опционально, за флагом) кэш выданных токенов `(user_id, org_id) → token` до `exp − margin` для
   мгновенного возврата в недавно посещённый тенант.

### 2.5. Один транспорт токена

Сейчас параллельно живут `localStorage.mb_token` + `Authorization: Bearer` и HttpOnly cookie
`accessToken`, которую backend ставит на login/refresh/switch. Два источника истины про «текущий тенант»
расходятся (switch в одной вкладке меняет cookie для всех вкладок, а `localStorage` соседней вкладки
не знает). **Целевое состояние: HttpOnly cookie `accessToken` — единственный носитель для браузера**,
`Authorization: Bearer` остаётся для API-клиентов/тестов/интеграций. Фронт токен не хранит и не читает;
сведения о сессии получает из `GET /auth/me`. CSRF-защита: `SameSite=Lax` + обязательный заголовок
`X-Requested-With: XMLHttpRequest` на мутациях, аутентифицированных cookie. Это одновременно закрывает
XSS-риск из `docs/menubuilder-frontend-architecture.md` §9.3.

### 2.6. Границы безопасности backend

- `get_current_user` не должен доверять заголовкам `X-User-Id / X-Org-Id / X-User-Role` (сейчас при
  отсутствии токена они принимаются как аутентификация — обход при прямом доступе к backend минуя Nginx).
- Захардкоженный суперюзер `username == "o.lebedev"` в `app/auth.py` и `app/user_store.py` — убрать;
  роль только из данных пользователя (БД/конфиг) и claim'ов.
- Мутации с `is_imp=true` логируются с `orig_sub` и `org` (аудит действий суперюзера в чужом тенанте).

## 3. Найденные дефекты текущей реализации (карта → шаги)

| # | Где | Что не так | Шаг |
|---|---|---|---|
| D1 | `app/auth.py::get_current_user` | доверие `X-User-*` заголовкам без токена | 1 |
| D2 | `app/auth.py`, `app/user_store.py::ConfigUserStore` | хардкод `o.lebedev` как суперюзера | 1 |
| D3 | `app/services/jwt_issuer.py::issue_tokens` | нет retry и дедупликации параллельных вызовов | 1 |
| D4 | `app/routers/auth.py::refresh_token` | org берётся из (истёкшего) access token через `suppress(Exception)` → суперюзер теряет тенант | 2 |
| D5 | `app/routers/admin_tenants.py::switch_tenant` | каждый switch создаёт новую `user_sessions` запись и переписывает `refreshToken` cookie | 2 |
| D6 | `app/auth.py::create_master_token` + `routers/auth.py` | master token подписывается локально; второй контур доверия | 3 |
| D7 | `frontend/src/routes/login.tsx`, `routes/layout.tsx` | хранение `mb_master_token`, `mb_is_superuser` как источник ролей | 3, 5 |
| D8 | `app/config.py` | `jwt_expire_minutes=15` при требовании редкой ротации | 4 |
| D9 | `frontend/src/api/client.ts` | только реактивный refresh по 401; пользовательский запрос ждёт issuer 2–3 с | 4 |
| D10 | `client.ts` + `_set_auth_cookies` | два транспорта токена (localStorage Bearer + cookie) | 5 |
| D11 | тенантные роутеры | не везде `require_tenant_context`; `org_id` из клиента может использоваться напрямую | 6 |
| D12 | `docs/multi-tenant-auth-architecture.md` | устарел (HS256, `auth_jwt_extract_var_claims`, master token) | 7 |

## 4. Пошаговый план

Формат каждого шага: Цель → Изменения → Критерии приёмки → Откат → Вектор (что приблизилось к цели).

---

### Шаг 0. Разведка и baseline (без изменений кода)

**Цель:** зафиксировать фактическое состояние, чтобы измерять приращение.

**Действия:**
1. Прочитать: `MenuBuilder/backend/app/auth.py`, `app/routers/auth.py`, `app/routers/admin_tenants.py`,
   `app/services/jwt_issuer.py`, `app/user_store.py`, `app/config.py`, `app/models.py`,
   `shared/etranprocessing_db/models/auth.py`, `MenuBuilder/frontend/src/api/client.ts`,
   `src/api/auth.ts`, `src/api/adminTenants.ts`, `src/routes/login.tsx`, `src/routes/layout.tsx`,
   `src/components/OrgSwitcher.tsx`, `src/App.tsx`, `docs/multi-tenant-auth-architecture.md`,
   `docs/menubuilder-frontend-architecture.md`, `docs/database-ownership.md`,
   тесты `MenuBuilder/backend/tests/test_tenant_switch.py`, `test_timezone_and_reports.py` и любые тесты
   с `auth` в имени.
2. Найти конфигурацию Nginx для MenuBuilder (ищи `auth_jwt`, `menubuilder-backend`, `proxy_pass` в
   `*.conf`, `docker-compose*.yml`, каталогах deploy). Зафиксировать: используется ли `auth_jwt` реально,
   откуда Nginx берёт токен (header/cookie), какие заголовки форвардит в backend.
3. Найти, где определён `auth_users` (`.env`, compose) и есть ли у записи `o.lebedev` явный
   `"is_superuser": true` / `"role": "superuser"`. Если нет — это блокер для Шага 1 (D2), нужно
   добавить в конфиг деплоя.
4. Прогнать `pytest`, `ruff`, `npm run build` — записать baseline (сколько тестов, что падает).
5. Перечислить все роутеры backend с классификацией: тенантный / суперюзерский / auth / публичный;
   для каждого — какая зависимость сейчас используется (`get_current_user` / `require_superuser` /
   `require_tenant_context` / ничего). Это вход для Шага 6.

**Приёмка:** создан `docs/multi-tenant-review/tenant-access-implementation-log.md` с разделом
«Baseline» (результаты п.2–5). Кода не менялось.

---

### Шаг 1. Backend hardening без изменения контрактов (D1, D2, D3)

**Цель:** закрыть дыры аутентификации и сделать клиент issuer'а устойчивым. Контракты API не меняются.

**Изменения:**
1. `app/config.py`: добавить `trust_proxy_identity_headers: bool = False`.
2. `app/auth.py::get_current_user`: ветка «нет токена, есть `X-User-Id`» выполняется только при
   `settings.trust_proxy_identity_headers`. По умолчанию — 401 `Authentication required`.
   Убрать `or username == "o.lebedev"` из вычисления `is_su`. `is_su` = `payload.is_superuser`
   или `role in ("superuser","admin")` или `role_id == 1`.
3. `app/user_store.py::ConfigUserStore.get_by_username`: убрать `or username == "o.lebedev"`.
   Убедиться (Шаг 0 п.3), что конфиг содержит явный признак; иначе — добавить в `.env`/compose и
   отметить в логе как деплой-требование.
4. `app/services/jwt_issuer.py::JwtIssuerClient`:
   - single-flight: словарь `dict[tuple[int,int], asyncio.Future]` по ключу `(user_id, org_id or 0)`;
     параллельные вызовы `issue_tokens` с одним ключом ждут один HTTP-запрос;
   - один retry при `httpx.RequestError` / 5xx с паузой 0.5 с; логировать длительность вызова
     (`logger.info("jwt issuer call user_id=%s org_id=%s took=%.2fs")`) — это метрика для оценки Шага 4;
   - mock-режим не трогать.
5. Тесты: (a) без токена и с `X-User-Id` → 401 при выключенном флаге, 200 при включённом;
   (b) `o.lebedev` без флага суперюзера в конфиге → не суперюзер; (c) два параллельных `issue_tokens`
   с одним ключом → один вызов `httpx.AsyncClient.post` (мокать httpx).

**Приёмка:** все тесты зелёные; логин обычного пользователя и суперюзера в dev работает как раньше;
`OrgSwitcher` виден суперюзеру (по данным `/auth/me`).
**Откат:** флаг `trust_proxy_identity_headers=true` возвращает старое поведение для заголовков.
**Вектор:** закрыты обходы аутентификации; issuer стал дешевле под нагрузкой; роли — только из данных.

---

### Шаг 2. Сессия — источник истины про активный тенант (D4, D5)

**Цель:** суперюзер не теряет тенант при refresh; switch не плодит сессии; login возвращает в последний тенант.

**Изменения:**
1. Схема (expand, nullable, безопасно для старых версий):
   - `shared/etranprocessing_db/models/auth.py`: `UserSession.active_org_id: Mapped[int|None]`
     (Integer, nullable, index), `User.last_org_id: Mapped[int|None]` (Integer, nullable).
   - Alembic-миграция в `ProcessingBackend/backend/alembic/versions/` (следовать соглашениям соседних
     файлов): `ADD COLUMN ... NULL`, индекс на `user_sessions.active_org_id`. Downgrade — drop columns.
   - Обновить матрицу в `docs/database-ownership.md` не требуется (владелец MB не меняется), но
     добавить дату проверки в лог.
2. `app/user_store.py::DatabaseUserStore`:
   - `create_session(..., active_org_id: int | None = None)`;
   - `set_session_active_org(session_id: int, org_id: int) -> None`;
   - `set_user_last_org(user_id: int, org_id: int) -> None` (только для суперюзеров, вызывается роутером);
   - in-memory fallback поддерживает эти поля;
   - `get_session_by_refresh_token` возвращает `active_org_id`.
3. `app/routers/auth.py::login`:
   - для суперюзера `initial_org_id = user.last_org_id or 0` (если `last_org_id` указывает на
     неактивную/удалённую org — использовать 0); для обычного — `user.org_id` (если `None` → 403
     «пользователь не привязан к организации», не выдавать токен без тенанта обычному пользователю);
   - `create_session(..., active_org_id=initial_org_id)`.
4. `app/routers/auth.py::refresh_token`:
   - удалить блок декодирования текущего access token;
   - `effective_org_id = session.active_org_id if session.active_org_id is not None else (user.org_id or 0)`;
     для обычного пользователя всегда `user.org_id` (сессионное значение игнорируется — защита от
     рассинхронизации);
   - при ротации refresh token новая сессия наследует `active_org_id`.
5. `app/routers/admin_tenants.py::switch_tenant`:
   - найти текущую сессию по `refreshToken` cookie (или body); если нет — 401;
   - вызвать issuer с `org_id=X`; `set_session_active_org(session.id, X)`; `set_user_last_org(user_id, X)`;
   - НЕ вызывать `create_session`, НЕ переписывать `refreshToken` cookie; обновить только `accessToken`
     cookie (разбить `_set_auth_cookies` на `_set_access_cookie` и `_set_refresh_cookie`);
   - разрешить `org_id = 0` как «выйти из тенанта на платформенный уровень» (без проверки Org в БД);
     ответ: `org_id=0, org_name="Платформа"`;
   - в ответ добавить `is_superuser`, `is_impersonated` (для фронта).
6. Тесты: (a) суперюзер: login → switch(223) → refresh с ИСТЁКШИМ access token → новый токен содержит
   `orgId=223`; (b) switch не увеличивает число записей `user_sessions`; (c) обычный пользователь:
   попытка `/admin/tenants/switch` → 403; (d) login суперюзера с `last_org_id=223` → первый токен с
   `orgId=223` и один вызов issuer; (e) `switch(0)` → токен `orgId=0`.

**Приёмка:** тесты зелёные; ручной сценарий: суперюзер входит в тенант, ждёт истечения access token
(в dev выставить `JWT_EXPIRE_MINUTES=1`), выполняет запрос → остаётся в том же тенанте.
**Откат:** миграция downgrade; код терпит `NULL` в новых колонках (fallback на `user.org_id`).
**Вектор:** условие 4 выполнено на backend; число сессий стабильно; login суперюзера — один поход в issuer.

---

### Шаг 3. Единый токен: удаление локального master token (D6, D7 частично)

**Цель:** один активный токен у клиента; все токены только от issuer; суперюзерские права — по claim.

**Изменения:**
1. Backend:
   - `TokenResponse.master_token` — оставить в схеме как `None` (deprecated) на один релиз, чтобы старый
     фронт не падал; удалить вызовы `create_master_token` из `login`/`refresh`;
   - `create_master_token`, `create_tenant_token`, `create_access_token` в `app/auth.py` — пометить
     deprecated; удалить, если не используются тестами; если используются — переписать тесты на
     mock-issuer;
   - `require_superuser` проверяет только `is_superuser`/`role` (уже так) — убедиться, что нигде нет
     проверки `token_type == "master"` как условия доступа к admin-ручкам;
   - `require_tenant_context` даёт 403 с `detail="Выберите организацию"` при `org_id in (None, 0)`.
2. Frontend:
   - `routes/login.tsx`: убрать запись `mb_master_token`; `mb_is_superuser` и `mb_current_org_id` пока
     оставить (удалятся в Шаге 5);
   - `routes/layout.tsx` (около строки с `mb_master_token`): выяснить, для чего он используется
     (вероятно «выйти из тенанта»); заменить на вызов `switchTenant(0)`;
   - `OrgSwitcher.tsx`: добавить действие «Платформа / выйти из тенанта» (`switchTenant(0)`), показывать
     `Tag` «Платформа» при `org_id === 0`; при `org_id === 0` навигация показывает только admin-разделы,
     тенантные разделы ведут на подсказку «Выберите организацию»;
   - `api/auth.ts`/`api/adminTenants.ts`: обновить типы под ответ Шага 2 п.5.
3. Тесты backend: admin-ручки доступны с токеном `orgId=223, is_superuser=true` (impersonated) — это
   ключевое свойство Правила №1.

**Приёмка:** `npm run build` зелёный; суперюзер: login → сразу в последнем тенанте → admin-раздел
доступен без выхода из тенанта → «Платформа» → тенантные экраны показывают подсказку → switch обратно.
Обычный пользователь ничего из этого не видит (`OrgSwitcher` → `null`).
**Откат:** вернуть `create_master_token` в `login` (поле в схеме сохранено).
**Вектор:** ровно один токен у клиента; один контур доверия (issuer); условие 3 выполнено по claim.

---

### Шаг 4. TTL и проактивный silent refresh (D8, D9)

**Цель:** пользователь никогда не ждёт issuer посреди работы.

**Изменения:**
1. `app/config.py`: `jwt_expire_minutes: int = 60`, `jwt_refresh_expire_days: int = 30` (уточнить у
   владельца issuer'а, управляет ли он TTL сам — если да, синхронизировать; `expires_in` из ответа issuer
   имеет приоритет, что уже так).
2. Backend: во всех ответах с токеном (`login`, `refresh`, `switch`) гарантировать поле `expires_in`
   (секунды). `GET /auth/me` дополнить `expires_at` (ISO, из `exp` текущего токена) — понадобится
   в Шаге 5, когда фронт перестанет видеть токен.
3. Frontend `src/api/client.ts` (или новый `src/api/session.ts`):
   - `scheduleRefresh(expiresInSec)` — таймер на `expiresIn − 300 с` (минимум 30 с); вызывать после
     login/refresh/switch; при `visibilitychange → visible` проверять, не осталось ли < 5 мин — тогда
     refresh немедленно; при `document.hidden` таймер не стрелять (ленивый refresh при возврате);
   - реактивный refresh по 401 с очередью — оставить как fallback;
   - `BroadcastChannel("mb-session")`: события `token-refreshed`, `tenant-switched`, `logout`;
     остальные вкладки на `tenant-switched`/`logout` делают `window.location.reload()`;
   - `OrgSwitcher.handleSwitch` публикует `tenant-switched`.
4. В лог Шага записать: сколько вызовов issuer'а за час активной сессии до/после (по логам из Шага 1 п.4).

**Приёмка:** в dev с `JWT_EXPIRE_MINUTES=2` пользователь работает 10 минут без единого видимого
ожидания/401; открытые две вкладки: switch в одной → вторая перезагружается в новом тенанте.
**Откат:** значения TTL через env; планировщик отключается флагом `VITE_SILENT_REFRESH=false`.
**Вектор:** условия 1 и 5 закрыты: задержка issuer'а не видна пользователю; ротация редкая.

---

### Шаг 5. Один транспорт: HttpOnly cookie для браузера (D7, D10)

**Цель:** фронт не хранит и не читает токен; cookie — единственный источник для браузера;
Bearer остаётся для API-клиентов и тестов.

**Изменения (backend, обратно совместимые):**
1. `get_current_user`: порядок — `Authorization: Bearer` → cookie `accessToken` (уже так). Убрать
   поддержку cookie `access_token` (snake_case), если она не используется.
2. CSRF middleware: если аутентификация прошла по cookie (а не по Bearer) и метод в
   `POST/PUT/PATCH/DELETE`, требовать `X-Requested-With: XMLHttpRequest`; иначе 403
   `CSRF check failed`. `/auth/login` и `/auth/refresh` — исключения (login без cookie; refresh cookie
   ограничена path `/api/auth`, `SameSite=Lax`).
3. `_set_access_cookie`: `secure` — по `X-Forwarded-Proto == https` (не только `request.url.scheme`,
   за Nginx схема будет `http`); `max_age = expires_in` (убрать `max(expires_in, 1800)`).
4. `TokenResponse.access_token` оставить (Bearer-клиенты), но фронт его больше не сохраняет.
5. Логаут: сервер уже чистит cookie; фронт дополнительно чистит только UI-состояние.

**Изменения (frontend):**
1. `client.ts`: убрать request-interceptor с `mb_token`; добавить заголовок `X-Requested-With:
   XMLHttpRequest` по умолчанию; `withCredentials: true` (уже).
2. `login.tsx`: не сохранять `mb_token`/`mb_is_superuser`/`mb_current_org_id`; после успешного логина
   вызвать `/auth/me`, положить результат в единый `SessionContext` (React context в `src/session/`),
   запустить `scheduleRefresh(expires_in)`.
3. `RequireAuth` (`App.tsx`): вместо проверки `localStorage.mb_token` — запрос `/auth/me` при старте
   (с кэшем 60 с, который уже есть); 401 → `/login`.
4. `layout.tsx`, `OrgSwitcher.tsx`: читать `is_superuser`, `org_id`, `org_name`, `timezone` только
   из `SessionContext` (источник — `/auth/me`). `org_timezone` в `localStorage` допустимо оставить
   как кэш для форматтеров, но обновлять из `/auth/me`.
5. Удалить все чтения `mb_token`, `mb_master_token`, `mb_is_superuser`, `mb_current_org_id`,
   `mb_current_org_name` (grep по `src/`); миграция для старых клиентов: при старте, если в
   `localStorage` есть `mb_token` — удалить.
6. Feature flag `VITE_AUTH_TRANSPORT=cookie|bearer` (default `cookie`) на один релиз для быстрого отката.

**Nginx (деплой-заметка, вне репозитория кода, если конфиг не найден в Шаге 0):**
- если используется `auth_jwt` — `auth_jwt_location COOKIE=accessToken;` (или отключить проверку на
  Nginx и оставить её backend'у — но тогда убедиться, что backend не доверяет `X-User-*`, см. Шаг 1);
- `proxy_set_header X-User-Id ""; X-Org-Id ""; X-User-Role "";` — зачистка входящих заголовков;
- CSP без `unsafe-inline` для скриптов.

**Приёмка:** `npm run build`; в DevTools → Application → Local Storage нет токенов; все сценарии
smoke-листа из `docs/menubuilder-frontend-architecture.md` §12 проходят; `curl` с Bearer в backend
напрямую работает (API-клиенты не сломаны); `curl` с cookie без `X-Requested-With` на POST → 403.
**Откат:** `VITE_AUTH_TRANSPORT=bearer` возвращает старый интерцептор (код старого пути удалить только
в следующем релизе).
**Вектор:** один источник истины про сессию и тенант; XSS не крадёт токен; multi-tab согласованность.

---

### Шаг 6. Аудит границы авторизации на backend (D11) — независим, можно после Шага 1

**Цель:** ни один тенантный эндпоинт не принимает `org_id` от клиента как источник истины; все
суперюзерские — под `require_superuser`; действия impersonated суперюзера аудируются.

**Изменения:**
1. По списку из Шага 0 п.5: тенантные роутеры → `Depends(require_tenant_context)`; внутри — `org_id`
   только из `user["org_id"]`. Если ручка принимает `org_id` параметром (например для отчётов), то для
   не-суперюзера: несовпадение → 403; для суперюзера: разрешить только если равен `user["org_id"]`
   (суперюзер тоже работает в выбранном тенанте — иначе теряется смысл switch и аудита). Исключение —
   явно платформенные ручки `/api/admin/*`.
2. Общий хелпер `resolve_org_id(user, requested: int | None) -> int` в `app/auth.py` с этой логикой;
   заменить дублирующийся код.
3. Middleware/зависимость аудита: для мутаций с `user["is_impersonated"]` логировать
   `audit impersonated action user=%s orig_sub=%s org=%s method=%s path=%s` (без тел запросов).
4. Тесты: матрица «роль × тип ручки × org совпадает/нет» → ожидаемый статус.

**Приёмка:** тесты зелёные; ручной: обычный пользователь с подменённым `org_id` в query получает 403.
**Вектор:** условие 2 гарантировано сервером, а не UI.

---

### Шаг 7. (Опционально, за флагом) Кэш токенов issuer'а

Только если по логам Шага 4 switch между двумя тенантами остаётся частым сценарием у суперюзеров.
`JwtIssuerClient`: in-process кэш `(user_id, org_id) → (token_data, exp)`; отдавать при
`exp − now > 10 мин`; инвалидация по `user_id` при logout/деактивации пользователя; флаг
`jwt_issuer_token_cache_enabled: bool = False`. Включать только после ревью безопасности (кэш
ослабляет отзыв). Тесты на попадание/промах/инвалидацию.

---

### Шаг 8. Документация и финальный отчёт (D12)

1. Переписать `docs/multi-tenant-auth-architecture.md` под целевую архитектуру из раздела 2:
   RS256 от внешнего issuer (не HS256), один активный токен, `active_org_id`/`last_org_id`, потоки
   login/refresh/switch/exit (sequence-диаграммы), CSRF, транспорт cookie, правила авторизации №1 и №2,
   таблицу claims. Раздел «Отвергнутые альтернативы» с вариантом B и причинами отказа — обязателен.
2. Обновить `docs/menubuilder-frontend-architecture.md`: §6.1 (нет Bearer-интерцептора, silent
   refresh, BroadcastChannel), §6.2 (диаграмма), §9 (SessionContext, `/auth/me` — источник истины),
   §14 (снять пп. 3 и 4 тех.долга), §16 (новые решения).
3. `docs/database-ownership.md`: добавить дату проверки и упоминание новых колонок.
4. Финальный отчёт в `tenant-access-implementation-log.md`: таблица D1–D12 → статус; метрики
   (вызовы issuer в час до/после; число сессий на пользователя); список деплой-требований
   (env: `JWT_EXPIRE_MINUTES`, `TRUST_PROXY_IDENTITY_HEADERS`, `auth_users` с явным `is_superuser`;
   Nginx-правки; порядок применения миграции expand → deploy).

## 5. Чего НЕ делать

- Не переходить на вариант B (org в заголовке/сессии без claim) «потому что быстрее» — это ломает
  форвардинг `org` в downstream и аудит.
- Не выдавать токен без `orgId` обычному пользователю; не выдавать обычному пользователю `orgId=0`.
- Не проверять доступ к admin-ручкам по `token_type` — только по `is_superuser`.
- Не добавлять локальную подпись JWT в production-путь.
- Не удалять Bearer-поддержку на backend (API-клиенты, тесты, интеграции).
- Не хранить refresh token в `localStorage` или в ответе для браузера иначе как HttpOnly cookie.
- Не делать миграции внутри `MenuBuilder`; не делать contract-шаг (drop/rename) в рамках этого плана.
- Не удалять тесты для «зелёности».

## 6. Формат отчёта по каждому шагу (в implementation-log)
