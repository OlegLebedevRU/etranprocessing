
# Анализ проблемы дублирования моделей MenuBuilder и ProcessingBackend

## Суть проблемы

Оба бекенда подключаются к **одной и той же PostgreSQL базе** и определяют свои SQLAlchemy ORM модели для **одних и тех же таблиц** в отдельных `app/models.py`. Между моделями уже есть дрейф — расхождения в полях, constraints, defaults.

## Текущее состояние

### Полностью дублированные модели (13 штук)

| Модель | Таблица | Расхождения |
|--------|---------|-------------|
| `Org` | `orgs` | MenuBuilder имеет доп. поля: `email`, `phone`, `notify_by_email` |
| `OrgBillingSettings` | `org_billing_settings` | MenuBuilder имеет доп. поля: `billing_mode`, `min_billing_periods`, `allowed_billing_periods`, `default_selection_mode`; разные `server_default` |
| `OrgStatus` | `org_statuses` | Идентичны |
| `TerminalType` | `terminal_types` | Идентичны |
| `Terminal` | `terminals` | ProcessingBackend имеет доп. `__table_args__` (индексы) |
| `License` | `licenses` | ProcessingBackend имеет доп. `__table_args__` (индексы, CHECK constraints) |
| `MenuVariant` | `menu_variants` | MenuBuilder имеет relationships; ProcessingBackend нет |
| `Group` | `groups` | MenuBuilder имеет relationships + `UniqueConstraint`; ProcessingBackend нет |
| `Service` / `ServiceMenu` | `services` | Разное имя класса; MenuBuilder имеет FK+relationship; ProcessingBackend нет FK |
| `TerminalMenuBinding` | `terminal_menu_bindings` | MenuBuilder имеет relationship; ProcessingBackend нет |
| `BillingOrder` | `billing_orders` | Идентичны |
| `BillingOrderItem` | `billing_order_items` | Идентичны |
| `CertificatePin` | `certificate_pins` | Разный default `creation_source` (`"global_admin"` vs `"system"`); ProcessingBackend имеет доп. индексы + CHECK |

### Модели только в ProcessingBackend (10 штук)

`GateGaugeRecord`, `TechGateRecord`, `Tsp`, `TspParameterCode`, `Payment`, `PaymentParam`, `BalanceTerminalTsp`, `ApiToken`, `TerminalCertHistory`, `TerminalCertDiscovery`

### Прочее дублирование

- `database.py` — **байт в байт идентичны** в обоих бекендах (16 строк)
- `_build_menu_tree()` — дублируется в `ProcessingBackend/backend/app/routers/list_menu.py` и `MenuBuilder/backend/app/main.py`
- `cert_billing.py` / `mask_pin()` — пересекающийся код

### Ключевые архитектурные факты

- Monorepo, оба бекенда на Python 3.14, FastAPI, SQLAlchemy 2.0+, asyncpg
- Alembic миграции **только в ProcessingBackend** (12 миграций)
- MenuBuilder использует `Base.metadata.create_all()` при старте (не может ALTER TABLE)
- MenuBuilder использует 10+ raw SQL `text()` запросов к таблицам ProcessingBackend (payments, tech_gate_records, gate_gauge_records, balance_terminal_tsp, api_tokens)

---

## Варианты решения

### Вариант 1: Shared `etranprocessing_db` Python пакет (рекомендуемый)

Создать локальный Python пакет в репозитории, на который оба бекенда будут ссылаться как зависимость.

**Структура:**
```
etranprocessing/
  shared/
    pyproject.toml              # имя: etranprocessing_db
    etranprocessing_db/
      __init__.py
      base.py                   # только Base (DeclarativeBase)
      models/
        __init__.py             # ре-экспорт всех 23 моделей
        org.py                  # Org, OrgBillingSettings, OrgStatus
        terminal.py             # TerminalType, Terminal, License, TerminalCertHistory, TerminalCertDiscovery
        menu.py                 # MenuVariant, Group, Service, TerminalMenuBinding
        billing.py              # BillingOrder, BillingOrderItem, CertificatePin
        payment.py              # Tsp, TspParameterCode, Payment, PaymentParam, BalanceTerminalTsp
        telemetry.py            # GateGaugeRecord, TechGateRecord
        auth.py                 # ApiToken
```

**Принцип:** `Base` общий, engine/session — нет. Каждый бекенд создаёт свой engine из своего `DATABASE_URL`.

**Плюсы:**
- Единый источник правды для моделей
- Минимальные изменения в импортах (путь меняется с `app.models` на `etranprocessing_db.models`)
- Alembic остаётся в ProcessingBackend, просто指向 на shared `Base.metadata`
- Оба бекенда сохраняют независимость (разные `.env`, разные engine)

**Минусы:**
- Нужно поддерживать shared пакет
- MenuBuilder теряет `create_all()` (но это правильно — миграции через Alembic)

**Принятие решений по merge:**

| Модель | Решение |
|--------|---------|
| `Org` | Включить доп. поля MenuBuilder (`email`, `phone`, `notify_by_email`). В БД они уже есть. |
| `OrgBillingSettings` | Включить доп. поля MenuBuilder. Использовать MenuBuilder `server_default` (соответствуют Alembic миграциям). |
| `Terminal` | Использовать `__table_args__` ProcessingBackend (индексы). |
| `License` | Использовать `__table_args__` ProcessingBackend (индексы + CHECK). |
| `Group` | Включить relationships MenuBuilder (`parent`, `children`, `services`, `menu_variant`). |
| `Service` | Имя класса = `Service`. Включить FK MenuBuilder + `UniqueConstraint`. |
| `CertificatePin` | Использовать CHECK constraints и индексы ProcessingBackend. Default `creation_source` = `"system"`. |
| Все relationships | Включить в shared модели. Relationships — ORM-only метаданные, не влияют на схему БД. |

**Шаги реализации:**

1. Создать `shared/pyproject.toml` + структуру пакета
2. Определить merged модели (union колонок, constraints, indexes, relationships)
3. Обновить MenuBuilder:
   - Добавить зависимость в `pyproject.toml` (`[tool.uv.sources] etranprocessing_db = { path = "../../shared" }`)
   - Удалить `app/models.py`
   - Обновить `app/database.py` — импортировать `Base` из shared, оставить engine/session локально
   - Обновить все импорты в routers/services: `from app.models import ...` → `from etranprocessing_db.models import ...`
   - Убрать `Base.metadata.create_all()` из `main.py`
4. Обновить ProcessingBackend:
   - Добавить зависимость в `pyproject.toml`
   - Заменить `app/models.py` на ре-экспорт: `from etranprocessing_db.models import *`
   - Обновить `alembic/env.py` — импортировать `Base` и модели из shared
   - Переименовать `ServiceMenu` → `Service` во всём коде (или временный alias `Service as ServiceMenu`)
   - Обновить все импорты
5. Верификация:
   - `alembic revision --autogenerate` → пустая миграция (нет изменений схемы)
   - `pytest` в обоих бекендах
   - Запуск обоих сервисов, проверка работоспособности

---

### Вариант 2: ProcessingBackend как единственная модель-зависимость

Все модели живут в ProcessingBackend. MenuBuilder зависит от ProcessingBackend как от pip-пакета.

**Плюсы:**
- Не нужен третий пакет
- Миграции и модели гарантированно синхронизированы

**Минусы:**
- Жёсткая связь: MenuBuilder тянет за собой все 23 модели ProcessingBackend, lxml, cryptography и т.д.
- Deployment хрупкий: обновление ProcessingBackend может сломать MenuBuilder
- ProcessingBackend содержит бизнес-логику (payment_service, cert_discovery), которая не нужна MenuBuilder
- Противоречит принципу разделения ответственности

**Вердикт:** Не рекомендуется.

---

### Вариант 3: API-подход (MenuBuilder → ProcessingBackend API)

MenuBuilder обращается к данным ProcessingBackend через HTTP API, а не напрямую к БД.

**Плюсы:**
- Полная развязка бекендов
- Нет дублирования моделей вообще

**Минусы:**
- MenuBuilder использует 10+ сложных raw SQL запросов (`text()`) для мониторинга, инкассации, баланса, отчётов. Рефакторинг этих запросов в API — **огромная отдельная задача**.
- Производительность: N+1 запросы, сетевые задержки
- MenuBuilder использует `payments`, `tech_gate_records`, `gate_gauge_records`, `balance_terminal_tsp`, `api_tokens` — все эти таблицы пришлось бы экспонировать через API

**Вердикт:** Хорошая цель на будущее, но не для решения текущей проблемы дублирования моделей.

---

### Вариант 4: Дисциплина копирования

Просто следить, чтобы модели совпадали.

**Вердикт:** Не работает — уже разъехались. Человеческий фактор неизбежен.

---

## Рекомендация

**Вариант 1 (shared пакет)** — оптимальный баланс между чистотой архитектуры и минимальным disruption. Монорепо + одинаковый Python/SQLAlchemy делают его технически тривиальным. Основная работа — это merge моделей и обновление импортов (механическая замена `from app.models` → `from etranprocessing_db.models`).

## Файлы для создания

| Файл | Назначение |
|------|-----------|
| `shared/pyproject.toml` | Метаданные пакета |
| `shared/etranprocessing_db/__init__.py` | Инициализация пакета |
| `shared/etranprocessing_db/base.py` | `Base` (DeclarativeBase) |
| `shared/etranprocessing_db/models/__init__.py` | Ре-экспорт всех 23 моделей |
| `shared/etranprocessing_db/models/org.py` | Org, OrgBillingSettings, OrgStatus |
| `shared/etranprocessing_db/models/terminal.py` | TerminalType, Terminal, License, TerminalCertHistory, TerminalCertDiscovery |
| `shared/etranprocessing_db/models/menu.py` | MenuVariant, Group, Service, TerminalMenuBinding |
| `shared/etranprocessing_db/models/billing.py` | BillingOrder, BillingOrderItem, CertificatePin |
| `shared/etranprocessing_db/models/payment.py` | Tsp, TspParameterCode, Payment, PaymentParam, BalanceTerminalTsp |
| `shared/etranprocessing_db/models/telemetry.py` | GateGaugeRecord, TechGateRecord |
| `shared/etranprocessing_db/models/auth.py` | ApiToken |

## Файлы для изменения

| Файл | Изменение |
|------|----------|
| `MenuBuilder/backend/pyproject.toml` | Добавить `etranprocessing_db` зависимость |
| `MenuBuilder/backend/app/database.py` | Импортировать `Base` из shared, оставить engine/session |
| `MenuBuilder/backend/app/main.py` | Убрать `create_all()`, обновить импорты моделей |
| `MenuBuilder/backend/app/routers/*.py` | Обновить импорты |
| `MenuBuilder/backend/app/services/*.py` | Обновить импорты |
| `ProcessingBackend/backend/pyproject.toml` | Добавить `etranprocessing_db` зависимость |
| `ProcessingBackend/backend/app/database.py` | Импортировать `Base` из shared |
| `ProcessingBackend/backend/app/models.py` | Заменить на ре-экспорт из shared |
| `ProcessingBackend/backend/app/routers/*.py` | Обновить импорты |
| `ProcessingBackend/backend/app/services/*.py` | `ServiceMenu` → `Service` |
| `ProcessingBackend/backend/app/dependencies.py` | Обновить импорты |
| `ProcessingBackend/backend/alembic/env.py` | Импортировать `Base` и модели из shared |

## Файлы для удаления

| Файл | Причина |
|------|---------|
| `MenuBuilder/backend/app/models.py` | Заменён shared пакетом |
| `ProcessingBackend/backend/app/models.py` | Заменён ре-экспортом из shared |

---
