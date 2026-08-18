# Анализ расширения биллинга лицензий: организационный PIN-driven сертификатный поток

**Тип документа:** Архитектурный и продуктовый анализ (read-only)  
**Область:** Биллинг разрешения на создание PIN и выпуск сертификата через существующий `certificates` flow  
**Статус:** Обновлён под уточнённую реальную механику  
**Дата:** 2026-08-18

---

## 1. Executive Summary

Ключевая корректировка: **checkout/payment не выпускает сертификат напрямую**.  
В текущей реализации (`ProcessingBackend/backend/app/routers/certificates.py`) вызов CA (`sign_csr`) возможен только в `function=setup`, когда терминал уже прислал CSR/PKCS#10.

Поэтому предмет биллинга:
- не «выпуск сертификата в webhook»,
- а **разрешение на операцию создания PIN для конкретного терминала** (первичная установка/перевыпуск).

Далее сертификат выпускается по существующему terminal-driven lifecycle:
1. сервер создаёт PIN;
2. PIN вручную вводится на терминале;
3. терминал делает `check`;
4. терминал локально генерирует CSR и делает `setup`;
5. backend вызывает CA, возвращает сертификат, PIN помечается использованным.

### Разделение понятий

1. **Организационная тарифная политика** — цены/режимы на уровне организации.
2. **Разрешение на создание PIN** — отдельная биллинговая операция для конкретного терминала.
3. **PIN enrollment lifecycle** — `pending/used/expired/cancelled` + TTL.
4. **X.509 credential lifecycle** — CSR от терминала, CA-выпуск в `setup`, установка сертификата.
5. **Terminal access decision** — прикладные проверки терминала/лицензии и сертификатные атрибуты.

---

## 2. Подтверждённая текущая механика (по коду)

### 2.1 Certificates flow

Источник: `ProcessingBackend/backend/app/routers/certificates.py`, `ProcessingBackend/docs/certificates-flow.md`.

- `GET /api/certificates/?function=check&pin=...`
  - lookup `certificate_pins` по `pin` и `status='pending'`;
  - возврат DN/sign (legacy XML).
- `POST /api/certificates/?function=setup&pin=...`
  - lookup `certificate_pins` по `pending`;
  - CSR приходит от терминала в body (PKCS#10);
  - только здесь вызывается `sign_csr()`;
  - по успеху: `terminals.cert_serial` обновляется, PIN -> `used`.

**Важно:** приватный ключ не хранится в backend/CA; он остаётся на терминале, backend получает только CSR.

### 2.2 Billing data today

Источник: `ProcessingBackend/backend/app/models.py`, `ProcessingBackend/backend/app/routers/billing.py`.

- `org_billing_settings` уже существует (организационный уровень для месячного тарифа).
- `billing_orders`/`billing_order_items` реализованы для лицензионных платежей.
- `certificate_pins` сейчас минимальная (`pin`, `terminal_id`, `status`, `created_at`, `used_at`).

Вывод: модель уже естественно расширяется в сторону **org-level cert policy + per-terminal operational PIN records**.

---

## 3. Целевая модель

### 3.1 Организационная тарифная политика (источник истины)

Предпочтительно расширить существующую таблицу `org_billing_settings`:

```sql
ALTER TABLE org_billing_settings
    ADD COLUMN cert_billing_mode VARCHAR(20) NOT NULL DEFAULT 'none',
    ADD COLUMN cert_price_minor BIGINT NULL,
    ADD COLUMN tenant_pin_creation_enabled BOOLEAN NOT NULL DEFAULT FALSE,
    ADD COLUMN cert_charge_primary_issue BOOLEAN NOT NULL DEFAULT TRUE,
    ADD COLUMN cert_charge_reissue BOOLEAN NOT NULL DEFAULT TRUE;

ALTER TABLE org_billing_settings
    ADD CONSTRAINT ck_org_cert_price_non_negative
    CHECK (cert_price_minor IS NULL OR cert_price_minor >= 0);

ALTER TABLE org_billing_settings
    ADD CONSTRAINT ck_org_cert_mode
    CHECK (cert_billing_mode IN ('none', 'per_operation'));
```

Семантика:
- `cert_billing_mode='none'`, `cert_price_minor IS NULL` — сертификатный тариф не применяется;
- `cert_billing_mode='per_operation'`, `cert_price_minor=0` — тариф применяется, операция бесплатна;
- `cert_billing_mode='per_operation'`, `cert_price_minor>0` — операция платная.

Месячная/годовая лицензионная политика также остаётся организационной.  
Пер-терминальные записи лицензий и операции остаются, но **не вводятся per-terminal tariff overrides** для сертификатного тарифа.

### 3.2 Per-terminal operational data

На уровне терминала сохраняем только операционные данные:
- `terminals.cert_serial`;
- `terminals.cert_not_valid_after` (рекомендуемое расширение);
- `certificate_pins`;
- `terminal_cert_history`;
- order/order item конкретной операции;
- вычисляемый статус.

`terminal_cert_entitlements` как хранилище тарифа/режима не рекомендуется.

---

## 4. Расширение `certificate_pins`

```sql
ALTER TABLE certificate_pins
    ADD COLUMN org_id INT NOT NULL,
    ADD COLUMN order_item_id INT NULL REFERENCES billing_order_items(id),
    ADD COLUMN created_by VARCHAR(100) NULL,
    ADD COLUMN creation_source VARCHAR(20) NOT NULL DEFAULT 'system',
    ADD COLUMN payment_required BOOLEAN NOT NULL DEFAULT FALSE,
    ADD COLUMN expires_at TIMESTAMPTZ NOT NULL DEFAULT (NOW() + INTERVAL '24 hours');

ALTER TABLE certificate_pins
    ADD CONSTRAINT ck_certificate_pins_status
    CHECK (status IN ('pending', 'used', 'expired', 'cancelled'));

ALTER TABLE certificate_pins
    ADD CONSTRAINT ck_certificate_pins_creation_source
    CHECK (creation_source IN ('tenant', 'global_admin', 'system'));

CREATE INDEX idx_cert_pins_org ON certificate_pins(org_id);
CREATE INDEX idx_cert_pins_order_item ON certificate_pins(order_item_id);
CREATE UNIQUE INDEX uq_cert_pins_one_pending_per_terminal
    ON certificate_pins(terminal_id)
    WHERE status = 'pending';
```

### Invariants

1. PIN tenant-scoped: `certificate_pins.org_id == terminals.org_id`.
2. Не более одного активного `pending` PIN на терминал.
3. PIN имеет TTL (`expires_at`).
4. Повторный запрос идемпотентно возвращает текущий `pending` PIN либо незавершённый order.
5. Переход `pending -> used` атомарен при успешном `setup`.
6. Global-admin bypass оплаты допустим только с аудитом причины.

---

## 5. Billing orders и snapshots

Для certificate-операции в `billing_order_items` добавляется `operation='cert_pin'` (или эквивалент).  
Запись должна хранить snapshot организационной политики на момент запроса:

```sql
ALTER TABLE billing_order_items
    ADD COLUMN cert_policy_snapshot JSONB NULL;
```

Минимум в snapshot:
- `cert_billing_mode`;
- `cert_price_minor`;
- `currency`;
- признак `primary_issue`/`reissue` для операции.

Это исключает изменение уже созданного order после изменения org-тарифа.

Idempotency:
- request PIN: ключ `(org_id, terminal_id, operation_type, idempotency_key)`;
- webhook: provider event id + проверка текущего `billing_orders.status`;
- создание PIN после оплаты: транзакционная проверка существующего `pending` PIN/связки order_item.

---

## 6. API и UI

### 6.1 Новый tenant endpoint

`POST /api/terminals/{terminal_id}/certificate-pin`

Поведение:
- mode=`none` -> `pin_ready` сразу;
- mode=`per_operation`, price=`0` -> `pin_ready` сразу + аудит zero-price;
- mode=`per_operation`, price=`>0` -> создать order, вернуть `payment_required`.

`pin_ready` пример:

```json
{
  "status": "pin_ready",
  "payment_required": false,
  "terminal_id": 42,
  "pin": "773773",
  "expires_at": "2026-08-19T12:00:00Z"
}
```

`payment_required` пример:

```json
{
  "status": "payment_required",
  "payment_required": true,
  "terminal_id": 42,
  "order_id": "uuid",
  "amount_minor": 500000,
  "currency": "RUB",
  "payment_url": "https://..."
}
```

### 6.2 Получение результата после оплаты

- `GET /api/billing/orders/{order_id}` (или отдельный endpoint статуса cert-operation).
- После подтверждения webhook backend создаёт PIN, UI получает `pin_ready` через polling.

### 6.3 UI

- Кнопка tenant: «Получить PIN» / «Перевыпустить сертификат».
- После `pin_ready` показать инструкцию ручного ввода PIN на терминале.
- PIN не выводить в небезопасных логах/аудитах (маскирование).

---

## 7. Исправленная orchestration-модель

Старая (ошибочная):
`payment webhook -> sign_csr -> certificate issued`

Новая (корректная):
`request -> policy/price snapshot -> optional payment -> PIN_READY -> manual PIN entry -> terminal CHECK -> terminal SETUP(with CSR) -> CA -> CERT_INSTALLED`

### Правила

1. Payment webhook **не вызывает CA**.
2. `sign_csr()` остаётся только в `certificates.py` handler `setup`.
3. Payment reconciliation и certificate issuance retry разделены:
   - без нового CSR нельзя безопасно повторить CA-выпуск;
   - по умолчанию CSR не хранить;
   - retry setup = повторная отправка CSR терминалом (или отдельное будущее решение по короткому безопасному хранению CSR).

### Состояния процесса

- `REQUESTED`
- `PAYMENT_PENDING`
- `PAYMENT_CONFIRMED`
- `PIN_READY`
- `PIN_EXPIRED`
- `CSR_RECEIVED`
- `CERT_ISSUED` / `CERT_INSTALLED`
- `CERT_ISSUE_FAILED`

---

## 8. Сценарии A и B (как организационные режимы)

### Сценарий A

Организация использует помесячную лицензию + отдельную сертификатную операцию (`cert_pin`).

### Сценарий B

Организация использует годовую лицензию, синхронизируемую с `notAfter` фактически установленного сертификата.

Критичный момент: между оплатой и `setup` есть лаг (PIN уже есть, сертификата ещё нет).  
Следовательно, до успешного `setup` нельзя финально выставлять `license.expires_at = notAfter`.

Нужен промежуточный статус `license_cert_sync_pending` и правило временного доступа (см. открытые решения).

---

## 9. Migration и staged rollout

1. Добавить org-level cert settings в нейтральном режиме: `cert_billing_mode='none'`.
2. Расширить `certificate_pins` (org, order_item, source, TTL, расширенные статусы).
3. Добавить `terminals.cert_not_valid_after` и `terminal_cert_history`.
4. Включить логику без начислений по умолчанию.
5. Затем включать tenant self-service feature flag по организациям.

Защита от случайных начислений:
- пока mode=`none`, cert-операции не требуют оплаты и не попадают в charge flow;
- все старые организации начинают с `none`.

---

## 10. Security и tenant isolation

1. Tenant может запрашивать PIN только для терминалов своей организации.
2. Rate limiting для endpoint создания PIN + TTL PIN.
3. PIN не логировать в открытом виде (маска в логах, например `***377`).
4. Для legacy lookup PIN по plaintext сохраняется; при необходимости обсуждать hash/encryption отдельно.
5. Global-admin bypass оплаты — обязательный аудит (`created_by`, причина, source=`global_admin`).
6. Защита от параллельных запросов и повторной оплаты (idempotency + уникальные ограничения).

---

## 11. Observability

Минимальный набор метрик:
- `cert_pin_request_total{result=pin_ready|payment_required|rejected}`
- `cert_pin_ready_total{source=tenant|global_admin|system}`
- `cert_pin_expired_total`
- `cert_pin_setup_success_total`
- `cert_pin_setup_failed_total`
- `cert_payment_webhook_total{result=confirmed|duplicate|invalid}`
- `cert_payment_to_pin_ready_latency_seconds`

Логирование и аудит:
- без plaintext PIN;
- связь `order_id -> order_item_id -> certificate_pin.id`;
- отдельный аудит bypass.

---

## 12. Test plan (обновлённый)

### 12.1 Unit/Service

- immediate PIN creation для `mode='none'`;
- immediate PIN creation для `price=0`;
- paid flow создаёт order и `payment_required`;
- idempotent request: повтор не создаёт второй pending PIN/order;
- pending uniqueness на терминал;
- TTL expiry переводит `pending -> expired`.

### 12.2 Integration/API

- tenant isolation (чужой terminal_id -> 404/403);
- webhook подтверждает оплату и создаёт PIN, но **не вызывает CA**;
- повтор webhook идемпотентен;
- terminal `check/setup` после оплаты работает через существующий router;
- при `setup` сохраняются `cert_serial`, `cert_not_valid_after`, история;
- scenario B: синхронизация лицензии только после успешного `setup`;
- отсутствует CA retry без CSR.

### 12.3 Negative/Concurrency

- параллельные POST certificate-pin для одного terminal -> один pending PIN;
- оплаченная операция + истекший неиспользованный PIN -> поведение по бизнес-правилу (open decision);
- `setup` с просроченным/cancelled PIN -> отказ.

---

## 13. Открытые бизнес-решения

1. TTL PIN (например 15 минут / 1 час / 24 часа).
2. Разрешён ли tenant self-service PIN для каждой организации (`tenant_pin_creation_enabled`).
3. Политика global-admin bypass (кто, когда, по какой причине).
4. Тарификация: первичная установка и перевыпуск одинаковы или различаются.
5. Что делать с оплаченной операцией, если PIN истёк неиспользованным.
6. Временный доступ в Scenario B между оплатой и `setup`.
7. Может ли одна оплата порождать новый PIN после истечения предыдущего.
8. Нужны ли в будущем terminal-level исключения из org-policy (сейчас — нет).
9. Нужна ли отдельная компенсация/refund политика для `PAYMENT_CONFIRMED`, но `PIN_EXPIRED`.

---

## 14. Итоговая рекомендация

- Принять **организационную** модель cert-тарифа (без per-terminal tariff override).
- Биллинговать **разрешение на создание PIN**, а не CA-выпуск в webhook.
- Сохранить действующий `certificates check/setup` как единственный путь выпуска сертификата.
- Расширить per-terminal данные только операционными сущностями (`certificate_pins`, `terminal_cert_history`, `cert_not_valid_after`).
- Внедрять поэтапно с `mode='none'` по умолчанию.

---

## 15. Prompt для следующего coding agent

```text
Задача: реализовать организационную PIN-driven модель сертификатного биллинга в ProcessingBackend.

Ключевые требования:
1) Не вызывать CA из checkout/payment webhook. Вызов CA (sign_csr) должен оставаться только в /api/certificates setup handler.
2) Расширить org-level billing settings (предпочтительно org_billing_settings):
   - cert_billing_mode: none | per_operation
   - cert_price_minor: NULL | 0 | >0
   - tenant_pin_creation_enabled
3) Реализовать tenant endpoint:
   - POST /api/terminals/{terminal_id}/certificate-pin
   - mode=none -> pin_ready сразу
   - per_operation + price=0 -> pin_ready сразу (аудит)
   - per_operation + price>0 -> payment_required + order
4) Расширить certificate_pins:
   - terminal_id, org_id, status(pending|used|expired|cancelled), order_item_id nullable,
     created_by, creation_source(tenant|global_admin|system), payment_required,
     expires_at, created_at, used_at
   - partial unique index: не более одного pending PIN на terminal
   - TTL и tenant invariants
5) Добавить idempotency для request PIN, webhook, create PIN after payment.
6) Добавить/обновить terminal_cert_history и terminals.cert_not_valid_after как операционные данные.
7) Для сценария B синхронизировать license.expires_at только после успешного setup (когда известен notAfter).
8) Обновить тесты:
   - free/non-applicable immediate PIN
   - paid flow: webhook creates PIN but does not call CA
   - tenant isolation
   - pending uniqueness
   - TTL expiration
   - idempotent request/webhook
   - setup after payment and history/not_valid_after persistence
   - no CA retry without CSR

Ограничения:
- не хранить приватный ключ в backend;
- CSR приходит от терминала и по умолчанию не сохраняется для retry;
- не логировать PIN в открытом виде.
```
