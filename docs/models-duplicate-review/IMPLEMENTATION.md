# Отчет о внедрении единого пакета моделей `etranprocessing_db`

## 1. Контекст и цели
Исторически сервисы `ProcessingBackend` и `MenuBuilder` работали с общей базой данных PostgreSQL, но поддерживали независимые копии SQLAlchemy моделей. Это привело к рассинхронизации схемы (13 расхождений из 23 моделей) и дублированию кода.

Цель внедрения — консолидация всех 23 ORM-моделей в единый тонкий shared-пакет `etranprocessing_db` с сохранением архитектурных границ и регламентов.

---

## 2. Архитектура и структура решения

### 2.1. Тонкий shared-слой (`shared/etranprocessing_db`)
Shared-пакет размещен в корневом каталоге `shared/` (`etranprocessing-db` v0.1.0) и содержит исключительно декларативные модели SQLAlchemy 2.0 (`Mapped`, `mapped_column`, `relationship`, `CheckConstraint`, `UniqueConstraint`, `Index`).

Структура пакета:
```
shared/
├── pyproject.toml
└── etranprocessing_db/
    ├── __init__.py          # Единый экспорт всех 23 моделей и DeclarativeBase
    ├── base.py              # Base(DeclarativeBase)
    └── models/
        ├── __init__.py
        ├── auth.py          # ApiToken
        ├── billing.py       # BillingOrder, BillingOrderItem, CertificatePin
        ├── menu.py          # MenuVariant, Group, Service, TerminalMenuBinding
        ├── org.py           # Org, OrgBillingSettings, OrgStatus
        ├── payment.py       # Tsp, TspParameterCode, Payment, PaymentParam,
        │                    # BalanceTerminalTsp, BalanceHistory
        ├── telemetry.py     # GateGaugeRecord, TechGateRecord
        └── terminal.py      # TerminalType, Terminal, License,
                             # TerminalCertHistory, TerminalCertDiscovery
```

### 2.2. Принцип Thin DB Layer
1. **Zero Business Logic**: Пакет `etranprocessing_db` не содержит бизнес-логики, утилит авторизации/хэширования, парсинга XML или внешних HTTP-клиентов.
2. **Minimal Dependencies**: Зависимости ограничены только `sqlalchemy[asyncio]>=2.0.52` и `asyncpg>=0.30.0`.
3. **Совместимость**: Оба сервиса (`ProcessingBackend` и `MenuBuilder`) подключают пакет как editable path dependency (`tool.uv.sources`), а их локальные `app/models.py` ре-экспортируют модели из `etranprocessing_db` для обратной совместимости.

---

## 3. Регламент проведения миграций и владение схемой

1. **Единый источник миграций (Alembic Authority)**:
   - Все миграции базы данных централизованы и генерируются/выполняются **исключительно** через `ProcessingBackend` (`ProcessingBackend/backend/alembic/`).
   - `alembic/env.py` импортирует `Base.metadata` из `etranprocessing_db.base`, видя полную схему всех 23 моделей.
2. **Запрет DDL в MenuBuilder**:
   - `MenuBuilder` не содержит скриптов миграций и **никогда не выполняет** `Base.metadata.create_all()`.
3. **Порядок применения миграций**:
   1. Изменение моделей в `shared/etranprocessing_db/models/`.
   2. Генерация ревизии: `cd ProcessingBackend/backend && uv run alembic revision --autogenerate -m "..."`.
   3. Проверка ревизии и применение в dev/test.
   4. В проде: применение через контейнер `processing-backend` (`sudo docker exec processing-backend alembic upgrade head`).
   5. При добавлении новых полей/таблиц, используемых в `MenuBuilder`, перезапуск `menubuilder-backend` (`sudo docker restart menubuilder-backend`).

---

## 4. Сборка и Docker-контекст

1. **Docker Build Context**:
   - В `ProcessingBackend/docker-compose.yaml` и `MenuBuilder/docker-compose.yaml` контекст сборки поднят до родительского каталога (`context: ..`).
   - `Dockerfile` каждого сервиса сначала копирует `shared/` и устанавливает его (`pip install --no-cache-dir /shared`), после чего устанавливает зависимости сервиса.
2. **Скрипты деплоя (`deploy-processing`, `deploy-menubuilder`)**:
   - Актуализированы для синхронизации каталога `shared/` на сервер `user1@176.108.247.249:/home/user1/`.

---

## 5. Верификация и тестирование

- **Модульные и интеграционные тесты**:
  - `ProcessingBackend/backend`: 51/51 тестов успешно пройдены (`pytest`).
  - `MenuBuilder/backend`: 129/129 тестов успешно пройдены (`pytest`).
- **Статический анализ и линтеры**:
  - `ruff check`, `ruff format --check`, `pyright` пройдены с 0 ошибок по всему коду (`shared/`, `ProcessingBackend/`, `MenuBuilder/`).
- **Production Verification**:
  - Все контейнеры (`processing-backend`, `menubuilder-backend`, `mcp-pin-server`) пересобраны и запущены в проде.
  - Живой трафик от платежных терминалов обрабатывается с кодами `200 OK`.
