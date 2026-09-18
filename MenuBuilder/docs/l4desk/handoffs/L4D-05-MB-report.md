# Handoff Report: Реализовать саморегистрацию и email confirmation (L4D-05-MB)

## Candidate H-L4D-05-MB-v1

<!-- HANDOFF:H-L4D-05-MB-v1:BEGIN -->
```yaml
handoff_id: H-L4D-05-MB-v1
status: READY_FOR_REVIEW
contract_kinds:
  - API
  - DEPLOYMENT
producer_prompt_id: L4D-05-MB
producer_scope_project: MenuBuilder
producer_report_path: MenuBuilder/docs/l4desk/handoffs/L4D-05-MB-report.md
producer_branch: l4desk/l4d-05-mb
producer_commit: 8902059485471f64fa4bf5f4605c633bcfeaa7b5
accepted_at_utc: 2026-09-18T14:15:00Z
contract_version: 1.0.0
schema_revision: "027"
artifact_version: 0.1.1
artifact_paths:
  - MenuBuilder/backend/app/routers/registration.py
  - MenuBuilder/backend/app/services/registration_service.py
  - MenuBuilder/backend/app/repositories/l4desk_repository.py
  - MenuBuilder/backend/tests/test_l4desk_registration.py
  - MenuBuilder/frontend/src/routes/register.tsx
  - MenuBuilder/frontend/src/routes/register-confirm.tsx
artifact_sha256:
  - 8c091b91b37ed0df5835d6d7c4267d178541f2c83fdff23c65338cb3668209ef
  - 986ca0b5acf6fae6f774cbf8bfc5119c9c8ad5c8bc2fe75ddb9b54799927e40e
  - 4cc8b00440f3217ebb592e7dacaa5c37c84de2fa607f08f655cd141fbba3c08d
  - 0150737921c0d5ffeb3b917d64b703ec96365742e7f40372df59741685a54693
  - bfd8a5419ff8714744fed4d507691fdea7fece9b22ec5c5e25f34273e3388519
  - ff684f883240171cc02919da75888ac4fc302dda9c468485c4529acdb3e57786
consumed_contracts:
  - handoff_id: H-L4D-04C-MB-v1
    contract_id: menubuilder_expand_schema_v1
    contract_version: 1.0.0
    schema_revision: "027"
    producer: MenuBuilder
    alembic_head: "027"
    deployed_host: 87.242.100.34
compatibility:
  backward_compatible_with:
    - 0.1.0
    - 0.1.1
  breaking_changes: false
  notes: "L4Desk public self-registration and email confirmation endpoints implemented in MenuBuilder under dark mode feature flag (l4desk_registration_enabled). Anti-enumeration responses prevent email discovery. Verification tokens are one-time use and hashed with SHA-256 in database. Atomic provisioning on confirmation creates Org, OrgBillingSettings (free package), OrgStatus, L4DeskTenantProfile, User with role_id=5 (l4desk_owner), L4DeskMembership (is_owner=True), and immutable audit event. Replay confirmation is idempotent. Rate limiting protects IP and resend cooldown. Malicious return URLs are sanitized against open redirect vulnerabilities. Existing auth and tenant sessions remain backward-compatible."
deployment_status: DEPLOYED
deployed_environment: production
feature_flags:
  l4desk_registration_enabled: false
  l4desk_billing_enabled: false
  l4desk_ui_enabled: false
  schema_compatibility_check_enabled: true
  required_alembic_revision: "027"
contract_payload:
  endpoints:
    - method: POST
      path: /api/auth/register
      description: "Public self-registration endpoint for tenant owners (anti-enumeration generic 200 response)"
    - method: POST
      path: /api/auth/register/confirm
      description: "One-time token verification and atomic tenant + user role 5 provisioning"
    - method: POST
      path: /api/auth/register/resend
      description: "Resend verification email with cooldown rate limits"
    - method: GET
      path: /api/auth/register/status
      description: "Public feature flag status check"
  roles:
    role_id_5:
      name: l4desk_owner
      permissions: ALL_PERMISSIONS
      is_tenant_admin: true
  invariants:
    - "Dark mode: endpoints guarded by l4desk_registration_enabled flag (returns 403 when disabled)"
    - "Security: token in database is hashed with SHA-256 (plaintext never persisted)"
    - "Anti-enumeration: registration and resend return generic message regardless of email existence"
    - "Atomicity: single PostgreSQL transaction creates Org, OrgBillingSettings, OrgStatus, TenantProfile, User (role=5), Membership, and Audit"
    - "Idempotency: replaying confirmation token returns already_confirmed without duplicate entities"
    - "Open redirect safety: return_url validated against whitelist and restricted to safe relative paths"
supersedes: []
known_risks:
  - "L4Desk registration remains disabled in production until explicitly activated via configuration flag"
  - "Billing payment processing and terminal provisioning are deferred to subsequent prompts (L4D-06A-PB / L4D-06B-MB)"
consumers:
  - L4D-06A-PB
next_prompt_id: L4D-06A-PB
```
<!-- HANDOFF:H-L4D-05-MB-v1:END -->

---

## 1. Резюме шага и контекст выполнения

Шаг `L4D-05-MB` выполнен строго в рамках изолированного репозиторного каталога `MenuBuilder` (`scope_project: MenuBuilder`, `scope_root: MenuBuilder/`).
Цель шага — реализовать публичную саморегистрацию L4Desk, подтверждение email и атомарное создание пользователя `role_id=5` (`l4desk_owner`) + организации (tenant) + owner membership под feature flag.

### Основные результаты:
1. **Contract Gate:** Проверен и принят входной handoff `H-L4D-04C-MB-v1` (все 4 SHA256 хэша артефактов верифицированы).
2. **Registration State & Token Security (`app/services/registration_service.py`):**
   - Реализована нормализация email (`clean.strip().lower()`), валидация структуры email и длины пароля (>= 8 символов).
   - Токены подтверждения генерируются криптографически стойким генератором (`secrets.token_urlsafe(32)`), в БД сохраняется исключительно SHA-256 хэш (`token_hash`), срок действия 24 часа.
   - Защита от перечисления пользователей (generic anti-enumeration): запросы на регистрацию и повторную отправку для уже существующих или неподтвержденных адресов возвращают одинаковый обобщенный ответ 200 OK без раскрытия наличия учетной записи.
   - Rate limiting: ограничение частоты запросов с IP (максимум 5 в окно 10 минут) и задержка между повторными отправками письма (cooldown 60 секунд, не более 3 в час).
3. **Atomic Tenant & User Provisioning:**
   - Эндпоинт `POST /api/auth/register/confirm` выполняет проверку токена и в единой транзакции PostgreSQL создает:
     - `Org` (новый `org_id`, `is_email_verified=True`, `email_verified_at=now`);
     - `OrgBillingSettings` (бесплатный пакет: `monthly_price_minor=0`, `billing_mode='prepaid'`, `cert_billing_mode='none'`, `allowed_billing_periods='1,3,6,12'`);
     - `OrgStatus` (`status='active'`);
     - `L4DeskTenantProfile` (`tenant_id`, `timezone`);
     - `User` (`role_id=5`, `role='l4desk_owner'`, `is_active=True`, пароль MD5);
     - `L4DeskMembership` (`tenant_id`, `user_id`, `role_id=5`, `is_owner=True`);
     - Обновление `L4DeskRegistration` (`consumed_at=now`, `user_id`, `tenant_id`);
     - `L4DeskAuditEvent` (`event_type='registration.confirmed'`, `actor='user:<id>'`, `outcome='success'`).
   - Идемпотентность: повторный вызов с уже использованным токеном возвращает `already_confirmed` со ссылкой на существующий tenant/user без дублирования сущностей.
4. **Безопасность возврата (Safe Return URL):**
   - Функция `validate_safe_return_url` блокирует open-redirect атаки (`//evil.com`, `/\evil.com`, `\evil.com`, `javascript:`), допуская только безопасные относительные пути (`/monitoring`, `/settings`) либо доверенные домены.
5. **Интеграция Auth & RBAC (`app/auth.py`, `app/security/permissions.py`):**
   - Добавлена поддержка `ROLE_L4DESK_OWNER = 5` (`l4desk_owner`).
   - Владельцы тенантов получают полный административный доступ к ресурсам своей организации (`require_tenant_admin`, `ALL_PERMISSIONS`).
   - Строгая изоляция тенантов и приведение строкового `org_id` в JWT к целочисленному `int`.
6. **Frontend UX (`src/routes/register.tsx`, `src/routes/register-confirm.tsx`):**
   - Страница регистрации с проверкой feature flag, таймером повторной отправки, валидацией пароля и выбором часового пояса.
   - Страница подтверждения с обработкой всех состояний: проверка (accessibility `aria-live`), успех, повторное подтверждение, истекший токен (с формой запроса нового письма), недействительный токен, обработка сетевых сбоев.
   - Ссылка на регистрацию добавлена на форму входа (`/login`) при включенном флаге.
7. **Тесты и качество кода:**
   - 335 passed в `MenuBuilder/backend` (`pytest`).
   - 26 passed в `MenuBuilder/frontend` (`vitest`).
   - `ruff check`, `ruff format`, `pyright` — 0 ошибок.
   - Frontend production build (`npm run build`) успешен.
8. **Развёртывание и Production Smoke:**
   - Артефакты развёрнуты на `87.242.100.34` в контейнерах `menubuilder-backend` и `nginx-default`.
   - Проверено поведение под выключенным флагом (HTTP 403 Forbidden).
   - Внутриконтейнерный E2E smoke тест проверил сквозной цикл регистрации и атомарного создания тенанта/пользователя на реальной PostgreSQL с последующей очисткой.
