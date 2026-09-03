---

# Отчёт: Deploy Readiness Review — TenantArchitectureAccessBackend

**Объём рефакторинга:** 2 коммита (`1c324ad`, `2090e94`), 41 файл, +2871/-1008 строк
**Реализовано:** Шаги 0–7 из 8 (Шаг 8 — документация и финальный отчёт — не завершён)
**Тесты:** 192 passed, ruff clean, pyright 0 errors

---

## CRITICAL — блокируют деплой (3)

### C1. `auth.py:118-127` — HS256 mock_secret fallback = обход аутентификации

```python
# 2. Try HS256 with mock secret (for unit tests / mock mode)
return jwt.decode(token, "mock_secret", algorithms=["HS256"], ...)
```

Fallback НЕ защищён флагом `jwt_issuer_mock_enabled`. Атакующий, знающий строку `"mock_secret"`, может подписать JWT с `alg:HS256, is_superuser:true, org_id:<any>` и получить полный доступ. RS256 декод упадёт, HS256 — succeeds.

**Исправление:** обернуть в `if settings.jwt_issuer_mock_enabled:` (как строка 130 для unverified).

### C2. `config.py:39` — Хардкод `service_to_yc_service_secret`

```python
service_to_yc_service_secret: str = "CHANGE_ME_SERVICE_TO_SERVICE_SECRET"
```

Если env-переменная не задана, HMAC-подпись запросов к issuer'у вычисляется с публично известным значением. Любой может forge-нуть запрос к Yandex Cloud Function и получить токен на任意ного пользователя.

**Исправление:** default → `""`, fail loudly если не сконфигурировано.

### C3. `reports.py:435,650` — Raw `org_id` вместо `effective_org_id` в SQL

Обе функции (`get_payments_report`, `get_balance_by_tsp`) корректно вычисляют `effective_org_id = resolve_org_id(user, org_id)`, но в запросе org_service_rows используют сырой `org_id` из Query параметра:

```python
# Строка 435 и 650:
{"org_id": org_id, "tsp_codes": tsp_codes}  # ← org_id может быть None
```

Когда клиент не передаёт `org_id` (стандартный случай для обычного пользователя), запрос `WHERE mv.org_id = NULL` возвращает 0 строк → имена услуг ТСП не резолвятся, отчёт молча отдаёт данные с generic-именами.

**Исправление:** заменить `org_id` на `effective_org_id` в обоих местах.

---

## WARNING — исправить до деплоя (7)

### W1. `session.ts:64-65` — Токен пишется в localStorage в cookie-режиме

`doSilentRefresh()` безусловно делает `localStorage.setItem("mb_token", result.access_token)` — нарушает модель безопасности Step 5 (cookie-only transport). Все остальные места (`login.tsx:21`, `OrgSwitcher.tsx:70`, `client.ts:82`) корректно guarded флагом `VITE_AUTH_TRANSPORT === "bearer"`.

### W2. `auth.py:178` — `or`-цепочка пропускает `org_id=0`

```python
raw_org_id = payload.get("orgId") or payload.get("org_id") or payload.get("org")
```

`0` — falsy в Python, поэтому `orgId=0` (platform mode) пропускается и падает на следующий fallback. Работает только потому что issuer всегда выставляет все три поля консистентно. Нужно явное сравнение с `None`.

### W3. `jwt_issuer.py:176` vs `admin_tenants.py:143` — `is_imp` threshold

- `jwt_issuer.py:176`: `is_imp = bool(is_superuser and effective_org_id > 1)`
- `admin_tenants.py:143`: `is_imp = bool(is_superuser and target_org_id > 0)`

Для `org_id=1` mock-токен даёт `is_imp=False`, а switch-ответ — `is_imp=True`. Расхождение в тестах vs production.

### W4. `user_store.py:117-121` — Прямой MD5-хеш как пароль

Если атакующий получит MD5-хеш из БД, он может аутентифицироваться, отправив хеш напрямую вместо пароля.

### W5. `routers/auth.py:247-259` — Токены в JSON body + HttpOnly cookie

`TokenResponse` возвращает `access_token` и `refresh_token` в JSON body. XSS может прочитать токены из body, что обесценивает HttpOnly защиту cookie.

### W6. `admin_tenants.py:107,110` — Fallback на `user_id=1` и `username="superuser"`

Если JWT payload не содержит `user_id`, токен выпускается с `user_id=1` (первый админ). Нужно raise 400/403.

### W7. `jwt_issuer.py:42` — Unbounded in-memory token cache

Нет TTL-eviction и size limit. В long-running процессе словарь растёт без ограничения.

---

## INFO — желательно исправить (6)

| # | Где | Что |
|---|-----|-----|
| I1 | `auth.py:115-116` | Silent `except Exception: pass` при RS256 decode — конфигурационные ошибки не видны |
| I2 | `routers/auth.py:440-478` | `MagicMock`/`AsyncMock` type checking в production коде (`/me` endpoint) |
| I3 | `admin_tenants.py:47,95` | `== True` вместо `.is_(True)` для nullable boolean |
| I4 | `jwt_issuer.py:184` | `headers = {"alg": "none"}` — misleading, jwt.encode переопределяет |
| I5 | `session.ts:68-71` | `token-refreshed` BroadcastChannel event отправляется, но обработчик его игнорирует |
| I6 | `auth.py:111-112,118` | Дублирующийся индекс на `active_org_id` (`index=True` + explicit `Index`) |

---

## Что сделано хорошо

- **Миграция 021** — expand-only, nullable columns, чистый downgrade. Безопасно для rolling deploy.
- **Alembic ownership** — миграция в `ProcessingBackend`, как требуется по `database-ownership.md`.
- **`resolve_org_id`** — единый хелпер, token = sole source of truth. 7 из 10 тенантных роутеров полностью чисты.
- **CSRF middleware** — корректно защищает cookie-authenticated mutations, исключает login/refresh.
- **Session-based tenant persistence** — `active_org_id` в сессии, суперюзер не теряет тенант при refresh.
- **Single-flight deduplication** в `JwtIssuerClient` — корректная дедупликация параллельных вызовов.
- **BroadcastChannel** — корректная межвкладочная синхронизация (tenant-switched, logout).
- **Документы обновлены** — `multi-tenant-auth-architecture.md`, `database-ownership.md`, `menubuilder-frontend-architecture.md` актуальны.
- **192 теста** зелёных, включая новые для каждого шага.

---

## Вердикт

**НЕ ГОТОВ к деплою.** 3 CRITICAL findings требуют исправления:

1. Закрыть HS256 mock_secret fallback флагом (`auth.py:118`)
2. Убрать хардкод secret из default (`config.py:39`)
3. Исправить `org_id` → `effective_org_id` в `reports.py:435,650`

После исправления CRITICAL — рекомендую также закрыть W1 (session.ts localStorage leak) и W3 (is_imp inconsistency), т.к. они затрагивают security model и консистентность аудита.