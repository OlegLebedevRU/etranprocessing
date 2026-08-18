# Анализ расширения биллинга лицензий: сертификатный контур

**Тип документа:** Архитектурный и продуктовый анализ (read-only)  
**Область:** Расширение существующего биллинга лицензий терминалов за счёт клиентского сертификата X.509  
**Статус:** Черновик, требует подтверждения открытых бизнес-решений  
**Дата:** 2026-08-18

---

## Содержание

1. [Executive Summary](#1-executive-summary)
2. [Карта текущего certificate lifecycle](#2-карта-текущего-certificate-lifecycle)
3. [Разделение понятий](#3-разделение-понятий)
4. [Два новых бизнес-сценария](#4-два-новых-бизнес-сценария)
5. [Пробелы и риски текущей реализации](#5-пробелы-и-риски-текущей-реализации)
6. [Сравнение архитектурных вариантов](#6-сравнение-архитектурных-вариантов)
7. [Рекомендуемая целевая модель](#7-рекомендуемая-целевая-модель)
8. [Предлагаемая схема данных](#8-предлагаемая-схема-данных)
9. [State machine](#9-state-machine)
10. [Формулы задолженности и прогноза](#10-формулы-задолженности-и-прогноза)
11. [Правила взаимодействия с пользовательским отключением](#11-правила-взаимодействия-с-пользовательским-отключением)
12. [API design](#12-api-design)
13. [Checkout / Payment / CA orchestration](#13-checkout--payment--ca-orchestration)
14. [UI/UX proposal](#14-uiux-proposal)
15. [Migration и staged rollout](#15-migration-и-staged-rollout)
16. [Security и tenant isolation](#16-security-и-tenant-isolation)
17. [Observability и операционные процедуры](#17-observability-и-операционные-процедуры)
18. [Test plan](#18-test-plan)
19. [Реестр открытых бизнес-решений](#19-реестр-открытых-бизнес-решений)
20. [Итоговая рекомендация по этапам реализации](#20-итоговая-рекомендация-по-этапам-реализации)
21. [Prompt для следующего coding agent](#21-prompt-для-следующего-coding-agent)

---

## 1. Executive Summary

В системе уже реализован облегчённый биллинг прикладных лицензий терминалов
(`License.expires_at`). Терминал блокируется через XML-сигнал `<state>error</state>`
на `/api/licensebilling`. Параллельно существует второй физический контур допуска —
клиентский сертификат X.509, чей срок действия (`notAfter`) CA возвращает при каждом
выпуске, но в БД не сохраняет.

Новые бизнес-сценарии требуют:
- **(Сценарий A)** ежегодного перевыпуска сертификата с отдельным тарифом (от 0) при
  сохранении помесячной прикладной лицензии;
- **(Сценарий B)** годовой лицензии, бизнес-дата которой жёстко привязана к `notAfter`
  сертификата.

**Ключевые выводы:**

| Вопрос | Ответ |
|---|---|
| Как nginx блокирует терминал по сертификату? | `ssl_verify_client optional_no_ca` — TLS-блокировки нет. Просроченный сертификат проходит до приложения. |
| Хранится ли `not_valid_after` в БД? | **Нет.** Только `cert_serial` в `terminals.cert_serial`. |
| Кто знает срок сертификата? | CA при выпуске, backend — только в логе (`cert_logger.info`). |
| Текущий риск | Истёкший сертификат сейчас не блокирует терминал ни на TLS-уровне, ни в приложении. |

**Рекомендуемый вариант:** Вариант 2 — отдельная лёгкая сущность
`TerminalCertificateEntitlement` рядом с `License`. Это добавляет одну таблицу,
не ломает ни текущий billing service, ни `/api/licensebilling`, обратно совместимо
и поддерживает оба сценария.

---

## 2. Карта текущего certificate lifecycle

### 2.1 Выпуск сертификата

```
terminal → GET /api/certificates/?function=check&pin=...
  → ProcessingBackend/backend/app/routers/certificates.py : _handle_check()
    → lookup: certificate_pins WHERE pin=? AND status='pending'
    → вычислить sign = MD5(decode(tosign) + SIGN_KEY)
    → вернуть DN, prov, pin, sign (XML, windows-1251)

terminal → POST /api/certificates/?function=setup&pin=...&cpserial=...  body=PKCS10
  → ProcessingBackend/backend/app/routers/certificates.py : _handle_setup()
    → lookup: certificate_pins WHERE pin=? AND status='pending'
    → обернуть PKCS10 в PEM-заголовки
    → POST CA (services/ca.py : sign_csr())
        headers: X-Ssl-Client-Csr, X-Ssl-Client-Exp-Days=365, X-Sign, X-CN
        resp JSON: { cert, ca_pem, serial_number, not_valid_after }
    → terminal.cert_serial = ca_result.serial_number      ← ТОЛЬКО serial сохраняется
    → cert_pin.status = 'used'; cert_pin.used_at = now()
    → COMMIT
    → cert_logger.info("SETUP OK: serial=%s, valid_until=%s", …)   ← not_valid_after ТОЛЬКО В ЛОГЕ
    → вернуть PKCS#7 chain (Base64, XML)
```

Файлы с точными ссылками:

| Действие | Файл | Строки (приблизительно) |
|---|---|---|
| CHECK handler | `ProcessingBackend/backend/app/routers/certificates.py` | `_handle_check()` |
| SETUP handler | `ProcessingBackend/backend/app/routers/certificates.py` | `_handle_setup()` |
| CA HTTP client | `ProcessingBackend/backend/app/services/ca.py` | `sign_csr()`, поле `not_valid_after` в `CAResponse` |
| Сохранение serial | `routers/certificates.py` | `terminal.cert_serial = ca_result.serial_number` |
| Логирование valid_until | `routers/certificates.py` | `cert_logger.info(…, ca_result.not_valid_after)` |
| Модели | `ProcessingBackend/backend/app/models.py` | `Terminal.cert_serial`, `CertificatePin` |

### 2.2 Использование сертификата при обычных запросах

```
terminal → HTTPS (port 4443) → nginx
  → nginx-mutual-ssl.conf:
      ssl_verify_client optional_no_ca;   ← НЕТ обязательной проверки сертификата!
      proxy_set_header X-Client-Cert-DN $ssl_client_s_dn;
      proxy_set_header X-Client-Cert-Serial $ssl_client_serial;

  → ProcessingBackend/backend/app/dependencies.py : get_current_terminal()
      CN (sn) + cert_serial → SELECT terminals WHERE sn=CN AND cert_serial=serial
      если не найдено → 401
      если terminal.is_active=false → 403
```

Файл nginx: `ProcessingBackend/docs/nginx-mutual-ssl.conf`

### 2.3 Блокировка по лицензии

```
terminal → /api/licensebilling/ → nginx → ProcessingBackend
  → dependencies.py : get_terminal_license_state()
      → licenses WHERE terminal_id=? AND is_active=true
      → если expires_at < now() → state='error'
      → если renewal_enabled=false и срок прошёл → state='error'
  → licensebilling.py : license_check()
      → XML: <state>ok|error</state>
```

### 2.4 Что делает CA

CA (Yandex Cloud Functions serverless, `ProcessingBackend/backend/app/services/ca.py`):
- принимает PEM CSR, `X-Ssl-Client-Exp-Days` (по умолчанию 365 из `CERT_VALIDITY_DAYS`);
- возвращает `serial_number` (hex), `not_valid_after` (строка `"YYYY-MM-DD HH:MM:SS"`);
- приватный ключ терминала НЕ передаётся и НЕ хранится в backend.

### 2.5 Что хранится в БД

| Таблица | Поля, связанные с сертификатом |
|---|---|
| `terminals` | `cert_serial VARCHAR(100)` — обновляется при каждом SETUP |
| `certificate_pins` | `pin, terminal_id, status (pending/used), created_at, used_at` |

**Дата истечения сертификата в БД отсутствует.** Получить её можно только из CA-лога или запросив CA повторно.

### 2.6 Отсутствие TLS-блокировки по сроку сертификата

`ssl_verify_client optional_no_ca` означает: nginx принимает соединение с истёкшим или
отсутствующим клиентским сертификатом. Переменная `$ssl_client_verify` может принимать
значения `SUCCESS`, `FAILED:…`, `NONE`. Приложение `get_current_terminal()` не проверяет
эту переменную — значит, терминал с истёкшим сертификатом **в текущей реализации не
блокируется ни TLS-уровнем, ни приложением**.

> **ВЫВОД (подтверждённый кодом):** первый блокирующий контур (X.509) фактически не работает
> в production. Блокирует только прикладная лицензия.

---

## 3. Разделение понятий

| Понятие | Описание | Где живёт сейчас |
|---|---|---|
| **Financial entitlement** | Коммерческое право пользоваться терминалом на период — оплачено или нет | `licenses.expires_at`, `BillingOrder`, `billing_period_months`, `renewal_enabled` |
| **X.509 credential** | Физический криптографический токен (сертификат), выданный CA; имеет `notBefore`/`notAfter` | `terminals.cert_serial` (только serial); `not_valid_after` — только в логе |
| **Terminal access decision** | Технический допуск: комбинация TLS-проверки nginx + серийный номер в БД + проверка лицензии в приложении | nginx `ssl_verify_client`, `get_current_terminal()`, `get_terminal_license_state()` |

Цель архитектуры — сохранить чёткое разделение этих трёх понятий, не смешивая финансовую дату и
криптографическую дату в одном поле.

---

## 4. Два новых бизнес-сценария

### Сценарий A: помесячная лицензия + ежегодный сертификатный контур

- Прикладная лицензия оплачивается помесячно (или иным коротким периодом);
- Сертификат перевыпускается ежегодно;
- За перевыпуск взимается отдельный тариф `cert_renewal_price_minor ≥ 0`;
- Нулевой тариф — полноценное бизнес-значение («бесплатный перевыпуск»), не отсутствие
  конфигурации;
- Истечение сертификата независимо блокирует терминал, **даже если лицензия оплачена**.

### Сценарий B: годовая лицензия, привязанная к сертификату

- Терминал лицензируется на год;
- Бизнес-дата окончания годовой лицензии **равна** `notAfter` сертификата;
- Отдельный годовой тариф;
- При успешной оплате:
  1. backend выпускает новый сертификат с новым `notAfter`;
  2. `license.expires_at` устанавливается = `cert.not_valid_after`;
  3. нет расхождения дат — единый источник истины (сертификат).

---

## 5. Пробелы и риски текущей реализации

| # | Пробел / Риск | Файл | Серьёзность |
|---|---|---|---|
| 1 | `not_valid_after` не сохраняется в БД | `routers/certificates.py` | Критично для новых сценариев |
| 2 | `ssl_verify_client optional_no_ca` — TLS не блокирует | `docs/nginx-mutual-ssl.conf` | Высокая (сертификатный контур не работает) |
| 3 | Нет таблицы истории сертификатов; `cert_serial` перезаписывается | `models.py: Terminal.cert_serial` | Средняя (нет аудита, нет отслеживания серийных номеров) |
| 4 | Нет тарифа на перевыпуск сертификата | весь биллинг | Средняя (Сценарий A невозможен) |
| 5 | Нет статуса `cert_expired` / `cert_due_soon` в billing status machine | `services/billing.py` | Высокая (UI не видит сертификатного риска) |
| 6 | При Сценарии B нет механизма синхронизации `license.expires_at` с `notAfter` | — | Высокая (скрытое расхождение дат) |
| 7 | Нет идемпотентного workflow: оплачен → CA → установлен | — | Средняя (риск двойного начисления при сбое CA) |
| 8 | Нет флага `cert_renewal_enabled` аналогичного `renewal_enabled` | — | Средняя |
| 9 | `BillingOrderItem.operation` хранит один тип операции — нет типа `cert_renewal` | `models.py: BillingOrderItem` | Средняя |
| 10 | Нет разграничения «тариф не настроен» vs «тариф = 0» | — | Высокая (Сценарий A) |

---

## 6. Сравнение архитектурных вариантов

### Вариант 1 — Расширение `License` дополнительными cert-полями

Добавить в таблицу `licenses`:
- `cert_renewal_price_minor BIGINT NULL`
- `cert_expires_at TIMESTAMPTZ NULL`
- `cert_renewal_enabled BOOL`
- `cert_renewal_mode VARCHAR(20)` — `'annual'` / `'none'` / `'tied_to_cert'`

### Вариант 2 — Отдельная сущность `TerminalCertificateEntitlement`

Новая таблица `terminal_cert_entitlements` рядом с `licenses`:
- хранит коммерческий entitlement на сертификат (тариф, дата истечения);
- `cert_not_valid_after` — дата истечения X.509;
- `renewal_price_minor BIGINT` — тариф (0 = бесплатно, NULL = не применяется);
- связана с `terminals` через `terminal_id`.

### Вариант 3 — Унифицированные billable components

Ввести обобщённую таблицу `billing_entitlements` с `entitlement_type` (`license` / `cert_renewal`)
и отдельную `credential_lifecycles` для X.509 истории.

---

### Сравнительная таблица

| Критерий | Вариант 1 (расширить License) | Вариант 2 (отдельная сущность) | Вариант 3 (unified components) |
|---|---|---|---|
| **Сложность модели** | Низкая (1 таблица, +4 поля) | Средняя (+1 таблица, +1 история) | Высокая (+2-3 таблицы, абстракция) |
| **Риск путаницы дат** | **Высокий**: `expires_at` лицензии и `cert_expires_at` в одной строке | **Низкий**: разные таблицы, явная семантика | Низкий, но скрыт за `entitlement_type` |
| **Поддержка Сценария A** | Да, но модель «распухает» | Да, чисто | Да |
| **Поддержка Сценария B** | Да, риск расхождения дат в одной строке | Да, через foreign key и invariant | Да |
| **Влияние на billing service** | Нужно менять все функции (новые аргументы) | Новые функции изолированы | Полный рефакторинг |
| **Влияние на checkout/orders** | Нужен новый `operation` тип в BillingOrderItem | Новый `item_type` в BillingOrderItem, или отдельная структура | Серьёзные изменения |
| **Влияние на UI** | Один объект терминала, но поля неоднородны | Два явных раздела: лицензия + сертификат | Риск сложной логики на фронте |
| **Deploy/migration risk** | Низкий (ALTER TABLE) | Средний (CREATE TABLE + backfill) | Высокий |
| **Тестируемость** | Средняя (много условий в одном объекте) | **Высокая** (изолированные расчёты) | Средняя |
| **Долгосрочная расширяемость** | Плохая (поля накапливаются) | Хорошая | Избыточная на текущем масштабе |
| **Риск избыточности** | Низкий | Низкий | **Высокий** |

---

## 7. Рекомендуемая целевая модель

**Рекомендуется Вариант 2** — отдельная сущность `TerminalCertificateEntitlement`.

### Обоснование

1. **Семантическая чистота**: финансовый entitlement на сертификат — отдельная коммерческая единица,
   не атрибут лицензии. Смешивать их в одну строку значит создавать скрытое расхождение дат.

2. **Изоляция изменений**: биллинг-сервис лицензий (`services/billing.py`) не меняется.
   Добавляются отдельные функции для сертификатного расчёта.

3. **Обратная совместимость**: `/api/licensebilling` не нужно менять. `BillingTerminalRead` можно
   расширить дополнительными полями без breaking change.

4. **Поддержка обоих сценариев**:
   - Сценарий A: у терминала есть `License` с коротким периодом + `TerminalCertificateEntitlement`
     с `mode='annual'`.
   - Сценарий B: у терминала есть `License` с `mode='tied_to_cert'` + `TerminalCertificateEntitlement`
     как source of truth для даты.

5. **Минимальный шум в UI**: добавляется компактный блок «Сертификат» на той же странице лицензий.

---

## 8. Предлагаемая схема данных

### 8.1 Новые таблицы

#### `terminal_cert_entitlements`

```sql
CREATE TABLE terminal_cert_entitlements (
    id                      SERIAL PRIMARY KEY,
    terminal_id             INT NOT NULL REFERENCES terminals(id) ON DELETE CASCADE,
    org_id                  INT NOT NULL,

    -- Коммерческий режим
    mode                    VARCHAR(20) NOT NULL DEFAULT 'annual',
    -- 'annual'        : ежегодный перевыпуск, тариф cert_renewal_price_minor
    -- 'tied_to_cert'  : лицензия привязана к cert, date = cert.not_valid_after
    -- 'none'          : сертификатный биллинг не применяется к терминалу

    -- Тариф (NULL = режим 'none'; 0 = бесплатно)
    cert_renewal_price_minor BIGINT NULL
        CONSTRAINT ck_cert_price_non_negative CHECK (cert_renewal_price_minor >= 0),

    -- X.509 дата истечения последнего выпущенного сертификата
    cert_not_valid_after     TIMESTAMPTZ NULL,

    -- Коммерческая дата следующего обязательного перевыпуска
    renewal_due_at           TIMESTAMPTZ NULL,

    -- Флаг управления: false = терминал отключён от сертификатного контура
    renewal_enabled          BOOL NOT NULL DEFAULT TRUE,

    -- Последний известный серийный номер сертификата
    last_cert_serial         VARCHAR(100) NULL,

    -- Временные метки
    created_at               TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at               TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    CONSTRAINT uq_cert_entitlement_terminal UNIQUE (terminal_id)
);

CREATE INDEX idx_cert_ent_terminal ON terminal_cert_entitlements (terminal_id);
CREATE INDEX idx_cert_ent_org ON terminal_cert_entitlements (org_id);
CREATE INDEX idx_cert_ent_renewal_due ON terminal_cert_entitlements (renewal_due_at)
    WHERE renewal_enabled = TRUE AND mode != 'none';
```

#### `terminal_cert_history`

```sql
CREATE TABLE terminal_cert_history (
    id               SERIAL PRIMARY KEY,
    terminal_id      INT NOT NULL REFERENCES terminals(id) ON DELETE CASCADE,
    org_id           INT NOT NULL,
    cert_serial      VARCHAR(100) NOT NULL,
    not_valid_before TIMESTAMPTZ NULL,
    not_valid_after  TIMESTAMPTZ NOT NULL,
    issued_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    order_id         UUID NULL REFERENCES billing_orders(id),
    -- NULL = выпущен вне биллинга (первоначальная установка)
    issued_by        VARCHAR(100) NULL  -- username или 'system'
);

CREATE INDEX idx_cert_hist_terminal ON terminal_cert_history (terminal_id);
CREATE INDEX idx_cert_hist_serial ON terminal_cert_history (cert_serial);
```

### 8.2 Изменения в существующих таблицах

#### `billing_order_items` — добавить `item_type`

```sql
ALTER TABLE billing_order_items
    ADD COLUMN item_type VARCHAR(30) NOT NULL DEFAULT 'license_renewal';
-- Значения: 'license_renewal', 'cert_renewal', 'reactivation'
```

#### `terminals` — добавить `cert_not_valid_after` (денормализация для быстрого доступа)

```sql
ALTER TABLE terminals
    ADD COLUMN cert_not_valid_after TIMESTAMPTZ NULL;
```

> **Invariant**: `terminals.cert_not_valid_after` = `terminal_cert_history.not_valid_after`
> последней записи для этого терминала. Обновляется атомарно в той же транзакции, что и
> `terminals.cert_serial`.

### 8.3 Constraints и invariants

| # | Invariant | Таблица / поле | Уровень |
|---|---|---|---|
| I-1 | `mode='none'` ↔ `cert_renewal_price_minor IS NULL` | `terminal_cert_entitlements` | CHECK + application |
| I-2 | `mode IN ('annual','tied_to_cert')` → `cert_renewal_price_minor IS NOT NULL` | `terminal_cert_entitlements` | application |
| I-3 | `terminals.cert_serial` = последний `terminal_cert_history.cert_serial` | FK нет → application | application + reconciliation job |
| I-4 | `terminals.cert_not_valid_after` обновляется в той же транзакции, что и `cert_serial` | атомарная запись | application |
| I-5 | `BillingOrderItem.amount_minor >= 0` | CHECK | DB |
| I-6 | При Сценарии B: `license.expires_at` = `terminal_cert_entitlements.cert_not_valid_after` после оплаты | application | application + reconciliation |
| I-7 | `renewal_enabled=false` → терминал не попадает в расчёт долга по сертификату | application | billing service |

---

## 9. State machine

### 9.1 Состояния сертификатного entitlement

```
                  [режим 'none']
                       │
                  CERT_EXEMPT ─────────────────────────────────────┐
                                                                   │
 создан ──────> CERT_PENDING ─── CA выпустил ──> CERT_ACTIVE       │
                     │                               │             │
                     │                        renewal_due          │
                     │                         приближается        │
                     │                               │             │
                     │                          CERT_DUE_SOON      │
                     │                               │             │
                  CA failed                    срок истёк          │
                     │                               │             │
               CERT_ISSUE_FAILED            CERT_EXPIRED           │
                                                     │             │
                                             оплачен +             │
                                             CA выпустил           │
                                                     │             │
                                              CERT_ACTIVE ◄────────┘
                                              
      renewal_enabled=false → CERT_DEACTIVATED (сертификат не продлевается)
```

### 9.2 Состояния лицензии (существующие + дополненные)

Существующий `BillingStatus` (файл `ProcessingBackend/backend/app/services/billing.py`):

```
ACTIVE → DUE_SOON → OVERDUE
ACTIVE → DEACTIVATION_SCHEDULED → DISABLED
ADMIN_DISABLED (terminal.is_active=false)
```

Для Сценария B добавляется дополнительное измерение (не новый статус, а флаг):
`license_cert_sync_pending` — лицензия оплачена, но CA ещё не выпустил сертификат.

### 9.3 Комбинированный статус терминала для UI

| Лицензия | Сертификат | UI-статус |
|---|---|---|
| ACTIVE | CERT_ACTIVE | 🟢 Активен |
| DUE_SOON | CERT_ACTIVE | 🟡 Лицензия истекает через N дней |
| ACTIVE | CERT_DUE_SOON | 🟡 Сертификат истекает через N дней |
| ACTIVE | CERT_EXPIRED | 🔴 Сертификат истёк |
| OVERDUE | CERT_ACTIVE | 🔴 Лицензия просрочена |
| OVERDUE | CERT_EXPIRED | 🔴 Оба контура заблокированы |
| OVERDUE | CERT_DUE_SOON | 🔴 Лицензия просрочена + сертификат скоро истечёт |
| ACTIVE | CERT_ISSUE_FAILED | 🟠 Оплата прошла, сертификат не выпущен |
| DEACTIVATION_SCHEDULED | CERT_ACTIVE | 🔵 Деактивация запланирована до {date} |
| DISABLED | * | ⚫ Отключён |
| ADMIN_DISABLED | * | ⚫ Административно заблокирован |
| * | CERT_EXEMPT | Сертификатный контур не применяется |

> **Примечание:** UI-статус вычисляется в frontend или в отдельном поле ответа API —
> не в billing service (pure function), чтобы не усложнять вычисления.

### 9.4 State machine оплаты и перевыпуска

```
CHECKOUT_CREATED ──[ошибка провайдера]──> CHECKOUT_FAILED (terminal: без изменений)
       │
  [провайдер подтвердил]
       │
PAYMENT_CONFIRMED ──[CA sign_csr]──> CA_PENDING
                                          │
                             [CA вернул cert]         [CA failed]
                                          │                 │
                             CERT_ISSUED (→ CERT_ACTIVE)  CERT_ISSUE_FAILED
                             license.expires_at обновлён   ↓
                                                    reconciliation job / ручной ретрай
```

---

## 10. Формулы задолженности и прогноза

### 10.1 Долг по лицензии (существующий)

```python
# Из services/billing.py : calculate_terminal_debt()
debt_license = periods_due * period_price_minor
# period_price_minor = monthly_price_minor * billing_period_months
```

### 10.2 Долг по сертификату (новый)

```python
def calculate_cert_debt(
    cert_not_valid_after: datetime | None,
    renewal_price_minor: int | None,  # None = mode='none'
    renewal_enabled: bool,
    as_of: datetime,
) -> int:
    """Долг по сертификату — целые единицы валюты."""
    if not renewal_enabled:
        return 0
    if renewal_price_minor is None:  # mode='none'
        return 0
    if cert_not_valid_after is None:
        return 0
    if cert_not_valid_after > as_of:
        return 0
    # Сертификат истёк → нужно продление
    return renewal_price_minor  # единица = 1 перевыпуск = 1 год
```

Пример 1: `cert_not_valid_after=2026-01-01`, `renewal_price_minor=500000` (5000 руб в копейках),
`as_of=2026-08-18` → долг = 500 000 коп.

Пример 2: `cert_not_valid_after=2026-01-01`, `renewal_price_minor=0`, `as_of=2026-08-18` →
долг = 0 (бесплатный перевыпуск, но перевыпуск всё равно нужен — создаётся задача, не долг).

Пример 3: `renewal_price_minor=None` (mode='none') → долг = 0, в прогноз не включается.

### 10.3 Прогноз по сертификату

```python
def build_cert_forecast_entry(
    cert_not_valid_after: datetime,
    renewal_price_minor: int,
    as_of: datetime,
    forecast_months: list[str],  # ["2026-08", "2026-09", "2026-10"]
) -> list[ForecastMonth]:
    """Сертификат продлевается раз в год. Включить платёж в нужный месяц."""
    if cert_not_valid_after <= as_of:
        return []  # просроченный — в долге, не в прогнозе
    month_key = f"{cert_not_valid_after.year}-{cert_not_valid_after.month:02d}"
    if month_key not in forecast_months:
        return []
    return [ForecastMonth(month=month_key, amount_minor=renewal_price_minor, terminal_count=1)]
```

### 10.4 Агрегированный итог

```python
total_overdue = debt_license + debt_cert
# debt_cert = 0 если mode='none' или renewal_price_minor=0 (бесплатный перевыпуск)
```

> **Важно**: `renewal_price_minor=0` не создаёт финансовый долг, но должен создавать
> операционный сигнал «требуется перевыпуск» в UI и reconciliation job.

### 10.5 Прогноз на 3 месяца с примером

Терминал T1: лицензия 1000 коп/мес, период 1 мес, `expires_at=2026-09-01`;
сертификат, `cert_not_valid_after=2026-10-15`, `renewal_price_minor=500000` коп.

| Месяц | Лицензия | Сертификат | Итого |
|---|---|---|---|
| 2026-08 | 0 | 0 | 0 |
| 2026-09 | 1 000 | 0 | 1 000 |
| 2026-10 | 1 000 | 500 000 | 501 000 |

---

## 11. Правила взаимодействия с пользовательским отключением

> Пользовательское отключение = `renewal_enabled=false` на `License`, NOT изменение `terminal.is_active`.

| Ситуация | Правило |
|---|---|
| Пользователь отключил терминал (`renewal_enabled=false`) | `cert_renewal_enabled` в `TerminalCertificateEntitlement` тоже устанавливается в `false` — сертификатный долг и прогноз не начисляются |
| Лицензия истекла, renewal_enabled=false | Терминал в статусе DISABLED, сертификатный контур не начисляется |
| Лицензия отключена, но сертификат ещё действует | В UI показывать как «отключён» — не как «сертификат активен» |
| Пользователь реактивировал терминал | `renewal_enabled=true`; `cert_renewal_enabled` возвращается в `true`, если mode != 'none' |
| Отключённый терминал — что делать с уже выпущенным сертификатом | Сертификат физически остаётся на терминале, отзыв (OCSP/CRL) — **открытый вопрос #7** |
| `terminal.is_active=false` (административная блокировка) | Никаких начислений ни по лицензии, ни по сертификату; перевыпуск сертификата невозможен |

---

## 12. API design

> Все endpoints tenant-scoped через `jwt-org` header (текущий механизм,
> `ProcessingBackend/backend/app/dependencies.py : get_current_user_jwt()`).

### 12.1 Расширение `BillingTerminalRead`

```json
{
  "terminal_id": 42,
  "sn": "A99D2F...",
  "billing_status": "active",
  "...existing fields...",

  "cert": {
    "mode": "annual",
    "cert_not_valid_after": "2026-10-15T00:00:00Z",
    "renewal_due_at": "2026-10-15T00:00:00Z",
    "renewal_price_minor": 500000,
    "cert_status": "cert_active",
    "days_until_cert_expiry": 58,
    "renewal_enabled": true
  }
}
```

Обратная совместимость: поле `cert` добавляется как необязательное (null если mode='none').

### 12.2 `GET /api/billing/summary` — добавить секцию cert

```json
{
  "...existing fields...",
  "cert_overdue_count": 2,
  "cert_due_soon_count": 3,
  "cert_overdue_amount_minor": 1000000
}
```

### 12.3 `POST /api/billing/checkout` — расширить `CheckoutItemRequest`

```json
{
  "items": [
    {
      "terminal_id": 42,
      "advance_periods": 0,
      "include_cert_renewal": true
    }
  ]
}
```

Ответ `CheckoutItemResponse`:

```json
{
  "terminal_id": 42,
  "periods_due": 1,
  "advance_periods": 0,
  "amount_minor": 3000,
  "new_expires_at": "2026-09-01T00:00:00Z",
  "cert_renewal": {
    "included": true,
    "price_minor": 500000,
    "new_cert_not_valid_after": null,
    "status": "pending_issuance"
  }
}
```

### 12.4 `GET /api/billing/terminals/{terminal_id}/cert-history`

```json
[
  {
    "id": 1,
    "cert_serial": "52B8E528000400002E2D",
    "not_valid_before": "2025-10-15T00:00:00Z",
    "not_valid_after": "2026-10-15T00:00:00Z",
    "issued_at": "2025-10-14T12:00:00Z",
    "order_id": "uuid-order",
    "issued_by": "system"
  }
]
```

### 12.5 Обратная совместимость `/api/licensebilling`

**Не изменяется**. Терминал с истёкшим сертификатом может вообще не дойти до endpoint
(если nginx будет настроен на `ssl_verify_client on`). Если дойдёт — отвечать текущим
форматом `<state>ok|error</state>` по лицензии.

---

## 13. Checkout / Payment / CA orchestration

### 13.1 Флоу оплаты с перевыпуском сертификата

```
1. User: POST /api/billing/checkout
     { items: [{terminal_id: 42, include_cert_renewal: true}] }

2. Backend:
   a. Вычислить license_amount + cert_renewal_amount
   b. Создать BillingOrder (status='pending')
   c. Создать BillingOrderItem (item_type='license_renewal') + BillingOrderItem (item_type='cert_renewal')
   d. Вызвать payment provider (если amount > 0)
   e. Вернуть { order_id, payment_url }

3. Payment provider webhook: POST /api/billing/webhooks/payment
   a. Найти BillingOrder по provider_order_id
   b. Проверить idempotency: если BillingOrder.status='paid' → 200 (уже обработан)
   c. Обновить BillingOrder.status='paid', paid_at=now()
   d. Обновить license.expires_at (для license_renewal items)
   e. Для cert_renewal items: вызвать CA (sign_csr)
      - Успех: обновить terminals.cert_serial, terminals.cert_not_valid_after,
               terminal_cert_entitlements.cert_not_valid_after,
               добавить запись в terminal_cert_history
               BillingOrderItem.cert_status='issued'
      - Неудача: BillingOrderItem.cert_status='issue_failed', cert_logger.error
                 → task для reconciliation job

4. (Случай amount=0 для бесплатного перевыпуска)
   a. BillingOrder.status='paid' сразу (нет провайдера)
   b. Перейти к шагу 3e немедленно
```

### 13.2 Idempotency

- `BillingOrder.id` = UUID v4, создаётся при checkout — используется как idempotency key.
- Webhook повторно вызывается → backend проверяет `BillingOrder.status` перед обработкой.
- CA вызов: если `terminal_cert_history` уже содержит запись с `order_id=?` → пропустить.

### 13.3 Reconciliation job

Периодический фоновый процесс (celery beat / APScheduler):
- Найти BillingOrderItem с `item_type='cert_renewal'` и `cert_status='issue_failed'` за
  последние 24 часа → повторить `sign_csr` → обновить cert_serial.
- Найти `terminal_cert_entitlements` с `cert_not_valid_after < now() - 1 day` и
  `renewal_enabled=true` → создать алерт.
- Найти `terminals.cert_serial != terminal_cert_history.cert_serial` (последняя запись) →
  логировать расхождение.

### 13.4 Сценарий B — синхронизация дат

При успешном выпуске сертификата для терминала с `mode='tied_to_cert'`:
```python
license.expires_at = ca_result.not_valid_after  # строго равны
cert_entitlement.cert_not_valid_after = ca_result.not_valid_after
```
Выполняется в одной транзакции. Нет расхождения — сертификат является source of truth.

---

## 14. UI/UX proposal

### 14.1 Основной принцип: минимальный шум

- Одна страница «Лицензии» — не создавать отдельный «Сертификатный биллинг».
- Компактный блок предупреждений по сертификату — только если нужно внимание пользователя.
- Единый итог к оплате — с раскрываемой структурой суммы.

### 14.2 Структура страницы

```
┌─ Биллинг организации ─────────────────────────────────────────────────────┐
│  Долг: 15 000 ₽        [Оплатить просрочку]                               │
│  Ближайший платёж: 1 000 ₽  (01.09.2026)                                  │
│                                                                            │
│  ⚠ 2 терминала: сертификат истекает через 30 дней   [Показать]           │
│  ⚠ 1 терминал: сертификат истёк, перевыпуск не оплачен                   │
│                                                                            │
│  Прогноз:  Авг: 5 000 ₽  │  Сен: 12 000 ₽  │  Окт: 507 000 ₽           │
│  (hover: расшифровка по терминалам и типам)                                │
└────────────────────────────────────────────────────────────────────────────┘

┌─ Терминалы ───────────────────────────────────────────────────────────────┐
│  Фильтр: [Все] [Просрочка] [Скоро истекает] [Отключены]                  │
│  Поиск:  [____________]                                                   │
│                                                                           │
│  T-773  │ 🔴 Лицензия просрочена │ до: 01.07.2026 │ Долг: 3 000 ₽       │
│         │ 🟡 Сертификат до: 15.10.2026  500 ₽/год                        │
│         │ [Оплатить]  [Отключить]                                        │
│                                                                           │
│  T-774  │ 🟢 Активен            │ до: 01.09.2026 │                       │
│         │ 🟢 Сертификат до: 15.10.2026                                   │
└────────────────────────────────────────────────────────────────────────────┘
```

### 14.3 Checkout modal — единая корзина с раскрываемой структурой

```
Оплата за T-773:
  ✓ Лицензия × 1 мес                         1 000 ₽
  ✓ Перевыпуск сертификата (годовой)          5 000 ₽
  ─────────────────────────────────────────────────
  Итого:                                      6 000 ₽
  [Оплатить]
```

### 14.4 Статусы в UI (компактные)

| Иконка | Текст | Условие |
|---|---|---|
| 🟢 | Активен | license=ACTIVE, cert=CERT_ACTIVE |
| 🟡 | Лицензия истекает через N дн. | license=DUE_SOON |
| 🟡 | Сертификат истекает через N дн. | cert=CERT_DUE_SOON |
| 🔴 | Лицензия просрочена | license=OVERDUE |
| 🔴 | Сертификат истёк | cert=CERT_EXPIRED |
| 🔴 | Оба контура заблокированы | license=OVERDUE, cert=CERT_EXPIRED |
| 🟠 | Оплачен, сертификат не установлен | cert=CERT_ISSUE_FAILED |
| 🔵 | Деактивация запланирована | license=DEACTIVATION_SCHEDULED |
| ⚫ | Отключён | license=DISABLED |
| ⚫ | Административно заблокирован | terminal.is_active=false |
| — | Сертификат не управляется | mode='none' (CERT_EXEMPT) |

### 14.5 Блок сертификата в карточке терминала

Показывается только если `cert.mode != 'none'`. Компактно: дата истечения + статус иконка.
При `renewal_price_minor=0` — показать «Бесплатно» вместо суммы.

---

## 15. Migration и staged rollout

### 15.1 Фазы миграции

**Фаза 0 (pre-migration, no code changes):**
- Проверить logs/CA: восстановить `cert_not_valid_after` для существующих терминалов из логов.
- Составить CSV-маппинг: `terminal_id → cert_serial, not_valid_after, mode`.

**Фаза 1 (схема, backward compatible):**
- Migration: создать `terminal_cert_entitlements`, `terminal_cert_history`;
- Migration: добавить `billing_order_items.item_type DEFAULT 'license_renewal'`;
- Migration: добавить `terminals.cert_not_valid_after NULL`;
- Backfill `terminal_cert_entitlements` всем терминалам с `mode='none'` — нейтральное значение.
- Никаких начислений пока не происходит.

**Фаза 2 (feature flag: `CERT_BILLING_ENABLED=false`):**
- Развернуть код с новыми endpoints;
- При `CERT_BILLING_ENABLED=false`: `/api/billing/summary` не включает cert-поля в ответ;
- Тестировать на staging.

**Фаза 3 (migration существующих терминалов):**
- Администратор вручную или скриптом устанавливает `mode` для каждого терминала:
  - `mode='none'` → нет изменений;
  - `mode='annual'` → нужно также установить `cert_renewal_price_minor` и `cert_not_valid_after`;
  - `mode='tied_to_cert'` → требует явной конфигурации, перевод осторожный.
- Никаких автоматических начислений без явной конфигурации тарифа.

**Фаза 4 (CERT_BILLING_ENABLED=true):**
- Включить cert-поля в API ответы;
- Включить cert_renewal в checkout;
- Включить reconciliation job.

### 15.2 Защита от случайных начислений

- Новые терминалы создаются с `terminal_cert_entitlements.mode='none'` по умолчанию.
- Начисление долга по сертификату происходит только если `mode != 'none'` AND
  `cert_renewal_price_minor IS NOT NULL` AND `renewal_enabled=true`.
- Миграция устанавливает `mode='none'` для всех существующих терминалов.

---

## 16. Security и tenant isolation

| Требование | Реализация |
|---|---|
| Tenant isolation по `org_id` | Все запросы проверяют `terminal.org_id == jwt.org_id` (существующий паттерн, `billing.py`) |
| Приватный ключ не в backend | CA хранит ключ в Yandex Cloud Functions; backend получает только `cert_pem` + `serial_number` + `not_valid_after` |
| Тарифы не хардкодятся | `cert_renewal_price_minor` — в БД (конфигурируется администратором) |
| CA URL не хардкодится | `CA_URL = os.getenv("CA_URL", "https://ca.internal/sign-csr")` — уже реализовано в `services/ca.py` |
| Аудит выпуска | `terminal_cert_history` с `issued_by`, `order_id` |
| Webhook security | HMAC signature validation от payment provider (существующий паттерн checkout) |

---

## 17. Observability и операционные процедуры

### 17.1 Метрики (добавить к существующим)

| Метрика | Тип | Описание |
|---|---|---|
| `cert_renewal_issued_total` | Counter | Успешных перевыпусков |
| `cert_renewal_failed_total` | Counter | Неудачных перевыпусков |
| `cert_expires_soon_count` | Gauge | Терминалов с сертификатом, истекающим в 30 дней |
| `cert_expired_count` | Gauge | Терминалов с истёкшим сертификатом |
| `cert_issue_lag_seconds` | Histogram | Время от оплаты до выпуска сертификата |

### 17.2 Логирование

- `cert_logger.info` уже есть (`routers/certificates.py`). Добавить `not_valid_after` в
  аудитный лог при каждом SETUP.
- Reconciliation job: логировать все расхождения `cert_serial` vs `cert_history`.

### 17.3 Alerts

- `cert_expired_count > 0` с `renewal_enabled=true` — алерт оператору.
- `cert_renewal_failed_total` growing — алерт (CA недоступен).
- `billing_order.status='paid'` + `cert_status='issue_failed'` более 1 часа — критичный алерт.

### 17.4 Reconciliation job

Запускается раз в час:
1. Найти `BillingOrderItem` с `item_type='cert_renewal'` и `cert_status='issue_failed'` → retry CA.
2. Найти терминалы с `cert_not_valid_after < now() - 7 days` и `renewal_enabled=true` → alert.
3. Проверить invariant I-3: `terminals.cert_serial == terminal_cert_history.last().cert_serial`.

---

## 18. Test plan

### 18.1 Unit тесты (`tests/test_cert_billing.py`)

```python
# Pure functions, no DB
test_calculate_cert_debt_expired()          # price>0, cert истёк
test_calculate_cert_debt_active()           # cert не истёк → 0
test_calculate_cert_debt_mode_none()        # mode='none' → 0
test_calculate_cert_debt_price_zero()       # price=0, cert истёк → 0
test_calculate_cert_debt_renewal_disabled() # renewal_enabled=false → 0
test_build_cert_forecast_in_window()        # cert истекает в окне прогноза
test_build_cert_forecast_outside_window()   # cert истекает за пределами → 0
test_combined_debt_license_plus_cert()      # агрегация
test_cert_status_transitions()              # state machine
```

### 18.2 Integration тесты (с testDB)

```python
test_checkout_with_cert_renewal_creates_order_items()
test_checkout_cert_renewal_price_zero_no_payment_url()
test_checkout_cert_renewal_mode_none_not_included()
test_payment_webhook_updates_cert_serial_and_history()
test_payment_webhook_idempotent()           # повторный webhook → 200, no double charge
test_ca_failure_sets_issue_failed_status()
test_reconciliation_retries_failed_cert_issuance()
test_deactivate_terminal_disables_cert_renewal()
test_scenario_b_license_expires_at_equals_cert_not_valid_after()
```

### 18.3 Contract тесты

```python
test_billing_summary_response_cert_fields_optional()   # mode='none' → cert=null
test_billing_terminal_read_backward_compatible()        # старые поля присутствуют
test_licensebilling_endpoint_unchanged()                # /api/licensebilling не изменился
```

### 18.4 E2E тесты

- Терминал с Сценарием A: создать entitlement → дождаться истечения (freeze time) →
  проверить статус CERT_EXPIRED → оплатить → проверить новый cert_serial в историии.
- Терминал с Сценарием B: оплатить годовую лицензию → проверить `license.expires_at ==
  cert.not_valid_after`.
- Деактивация: `renewal_enabled=false` → cert не попадает в долг и прогноз.

---

## 19. Реестр открытых бизнес-решений

| # | Вопрос | Файл / место | Почему нельзя вывести из кода |
|---|---|---|---|
| B-1 | Должен ли `ssl_verify_client` быть изменён с `optional_no_ca` на `on` для включения TLS-блокировки истёкших сертификатов? | `nginx-mutual-ssl.conf` | Требует решения об уровне защиты и обратной совместимости |
| B-2 | Какой тариф `cert_renewal_price_minor` устанавливается для терминалов Сценария A по умолчанию? | `terminal_cert_entitlements` | Бизнес-решение |
| B-3 | При Сценарии B — кто инициирует выпуск первого сертификата (при переходе терминала на годовую лицензию)? | — | Неясен workflow онбординга |
| B-4 | Неоплата сертификатного продления = финансовый долг, предупреждение или немедленная блокировка после `notAfter`? | — | Зависит от коммерческой политики |
| B-5 | Нужен ли отзыв (OCSP/CRL) при отключении терминала пользователем? | CA, nginx | Требует решения об инфраструктуре CA |
| B-6 | При `cert_renewal_price_minor=0` — нужен ли checkout-процесс или CA вызывается автоматически? | — | Зависит от UX-решения |
| B-7 | Сколько `advance_periods` разрешено для cert renewal (max 1 год или можно на 2 года вперёд)? | — | Зависит от политики CA |
| B-8 | Как классифицировать существующие терминалы при backfill: в Сценарий A или Сценарий B? | — | Требует анализа коммерческих договорённостей |
| B-9 | Нужна ли отдельная страница «История сертификатов» или достаточно раскрываемого блока? | MenuBuilder frontend | UX-решение |
| B-10 | При истечении сертификата создавать финансовый долг по cert_renewal или только оперативный алерт? | — | Коммерческая политика |

---

## 20. Итоговая рекомендация по этапам реализации

### Этап 0: Подготовка (1–2 дня, без деплоя)

1. Подтвердить открытые бизнес-решения B-1, B-4, B-6, B-8 с владельцем продукта.
2. Восстановить `cert_not_valid_after` для существующих терминалов из CA-логов.
3. Зафиксировать маппинг `terminal_id → mode` (Сценарий A / B / none).

### Этап 1: Схема данных и инфраструктура (3–4 дня)

1. Создать migration: `terminal_cert_entitlements`, `terminal_cert_history`,
   `terminals.cert_not_valid_after`, `billing_order_items.item_type`.
2. Обновить `routers/certificates.py`: при каждом SETUP сохранять `cert_not_valid_after`
   в `terminals` и добавлять запись в `terminal_cert_history`.
3. Backfill: все существующие терминалы → `mode='none'`.
4. Деплой + проверка: CA-логи начинают создавать записи истории.

### Этап 2: Cert billing service (3–4 дня)

1. Добавить `services/cert_billing.py` с pure functions (debt, forecast, status).
2. Расширить `BillingTerminalRead` полем `cert` (nullable).
3. Обновить `GET /api/billing/summary` и `GET /api/billing/terminals` с cert-агрегацией.
4. Feature flag `CERT_BILLING_ENABLED=false` — фронтенд не показывает cert-блок.
5. Покрыть unit тестами.

### Этап 3: Checkout с cert renewal (2–3 дня)

1. Расширить checkout endpoint: `include_cert_renewal` в `CheckoutItemRequest`.
2. Добавить `item_type='cert_renewal'` в `BillingOrderItem`.
3. Реализовать webhook: при `status='paid'` + cert items → вызов CA → обновление cert_serial.
4. Реализовать reconciliation job для retry CA failures.
5. Integration тесты.

### Этап 4: UI в MenuBuilder (3–5 дней)

1. Добавить cert-блок в карточку терминала.
2. Добавить cert-предупреждения в сводку (summary panel).
3. Расширить checkout modal — раскрываемая структура суммы.
4. Фильтр `cert_expired` / `cert_due_soon`.

### Этап 5: Включение cert контура (1 день)

1. Включить `CERT_BILLING_ENABLED=true` на staging, затем production.
2. Перевести нужные терминалы из `mode='none'` в `mode='annual'` или `mode='tied_to_cert'`.
3. При необходимости изменить `ssl_verify_client optional_no_ca` → `ssl_verify_client on`
   (только после B-1).

---

## 21. Prompt для следующего coding agent

```
Ты coding agent для репозитория OlegLebedevRU/etranprocessing (ProcessingBackend/backend).

ЗАДАЧА: Реализовать Этап 1 и Этап 2 расширения биллинга сертификатов согласно архитектурному
документу docs/billing-certificate-licensing-analysis/ANALYSIS.md (раздел 20, Этапы 1–2).

ОТКРЫТЫЕ ВОПРОСЫ (должны быть отвечены до кодирования Этапа 3):
- B-4: финансовый долг или алерт при неоплате cert renewal?
- B-6: checkout для price=0 или автоматический CA вызов?
Для Этапов 1–2 эти вопросы НЕ БЛОКИРУЮТ — backfill устанавливает mode='none', начислений нет.

SCOPE:
1. Migration: создать таблицы terminal_cert_entitlements, terminal_cert_history;
   добавить terminals.cert_not_valid_after TIMESTAMPTZ NULL;
   добавить billing_order_items.item_type VARCHAR(30) DEFAULT 'license_renewal'.
2. Обновить routers/certificates.py _handle_setup(): после успешного CA ответа сохранять
   terminals.cert_not_valid_after = ca_result.not_valid_after (parse ISO datetime),
   создавать запись в terminal_cert_history (cert_serial, not_valid_after, issued_at, order_id=NULL).
3. Backfill migration: INSERT INTO terminal_cert_entitlements (terminal_id, org_id, mode)
   SELECT id, org_id, 'none' FROM terminals ON CONFLICT DO NOTHING.
4. Создать app/services/cert_billing.py с pure functions:
   - calculate_cert_debt(cert_not_valid_after, renewal_price_minor, renewal_enabled, as_of) -> int
   - build_cert_forecast(cert_not_valid_after, renewal_price_minor, forecast_months) -> list[ForecastMonth]
   - resolve_cert_status(cert_not_valid_after, renewal_enabled, mode, as_of) -> CertBillingStatus (Enum)
   CertBillingStatus: CERT_EXEMPT, CERT_ACTIVE, CERT_DUE_SOON, CERT_EXPIRED, CERT_DEACTIVATED
5. Расширить app/schemas/billing.py: добавить CertSummary (nullable) в BillingTerminalRead.
6. Обновить routers/billing.py GET /api/billing/terminals: включать cert данные из
   terminal_cert_entitlements, вычислять cert_status.
7. Обновить GET /api/billing/summary: добавить cert_overdue_count, cert_due_soon_count,
   cert_overdue_amount_minor.

ЗАПРЕТЫ:
- НЕ изменять /api/licensebilling контракт
- НЕ изменять существующие модели License, BillingOrder (только добавлять поля)
- НЕ удалять и НЕ изменять существующие тесты
- НЕ добавлять третьи библиотеки без необходимости
- НЕ хардкодить тарифы, URL CA, secrets
- НЕ хранить приватные ключи терминалов

ОБЯЗАТЕЛЬНЫЕ ТЕСТЫ (ProcessingBackend/backend/tests/):
- test_cert_billing.py: unit тесты всех pure functions из cert_billing.py
  - calculate_cert_debt: expired/active/mode_none/price_zero/renewal_disabled (min 5 cases)
  - resolve_cert_status: все 5 статусов
  - build_cert_forecast: in-window и out-of-window
- test_billing_api.py: расширить (или создать) integration тесты:
  - GET /api/billing/terminals возвращает cert поле (null для mode='none')
  - GET /api/billing/summary содержит cert_overdue_count

ACCEPTANCE CRITERIA:
1. Migration применяется без ошибок на пустой и непустой БД.
2. После SETUP сертификата terminals.cert_not_valid_after заполнено (не null).
3. terminal_cert_history содержит запись после каждого SETUP.
4. Для терминала с mode='none': cert поле в BillingTerminalRead = null.
5. Для терминала с mode='annual', cert_not_valid_after истёк, price=500000:
   cert_status='cert_expired', в summary cert_overdue_amount_minor=500000.
6. calculate_cert_debt(cert_not_valid_after=истёкший, renewal_price_minor=0,
   renewal_enabled=True, as_of=now) == 0.
7. Все новые тесты проходят, существующие тесты не сломаны.
8. ruff check, ruff format, pyright — без ошибок в изменённых файлах.
```

---

*Документ создан: 2026-08-18. Версия 1.0.*  
*Все ссылки на файлы — относительно корня репозитория `etranprocessing/`.*  
*Пометки "ВЫВОД (подтверждённый кодом)" означают факты, верифицированные чтением исходников.*  
*Пометки "открытый вопрос #N" — см. раздел 19.*
