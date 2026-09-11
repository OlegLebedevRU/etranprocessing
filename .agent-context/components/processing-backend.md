# ProcessingBackend

## Назначение
Терминальный payment/mTLS gateway и единая цепочка Alembic общей PostgreSQL-схемы.

## Границы ответственности
Payment ledger/balances, terminal requests, certificate audit/discovery и выдача меню.
Не размещать здесь пользовательский JWT portal/billing API MenuBuilder.
Автор доменной миграции — владелец таблицы; физическая цепочка — в ProcessingBackend.

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| Terminal → backend | mTLS HTTPS через Nginx | payment, techgate, gategauge, licensebilling, certificates | request + X-Client-Cert-DN/Serial | terminal auth/license check |
| Terminal → backend | HTTPS | GET /api/ListMenuFile | terminal menu request | опубликованная версия/аудит выдачи |
| Backend → shared DB | async SQLAlchemy | payment/ledger/audit | declarative models | owner-scoped запись |

## Инварианты
- Nginx валидирует certificate; доверие к forwarded identity требует защищённой границы proxy.
- ORM не дублировать и не добавлять в shared бизнес-логику.
- MenuBuilder читает payment reporting; это не разрешение на запись ledger.

## State machine
Зависит от payment/certificate/menu use case. Перед изменением статуса выписать
текущие допустимые переходы, повтор запроса и transaction boundary; не выдумывать общую enum.

## Ключевые исходники
- [app/main.py](../../ProcessingBackend/backend/app/main.py) — маршрутизация приложения.
- [app](../../ProcessingBackend/backend/app) — найти конкретный handler по endpoint задачи.
- [alembic/versions](../../ProcessingBackend/backend/alembic/versions) — единственные общие migrations.
- [shared models](../../shared/etranprocessing_db/models) — общая схема.

## Проверка
Из ProcessingBackend\backend: `uv run pytest`, quality по [матрице](../operations/validation-matrix.md).
Auth tests используют simulated certificate headers; payment повторы/ошибки/tenant isolation
проверять в профильных тестах. Shared changes требуют также MenuBuilder и migration checks.

## Известные риски и незавершённые вопросы
Это ownership-карточка, не runtime-аудит gateway. Legacy-совместимость не разрешает
исследовать исключённые корневые каталоги без явного scope пользователя.

## Источники и актуальность
- Authoritative docs: [backend guidelines](../../ProcessingBackend/GUIDELINES.md),
  [ownership](../../docs/etran_data-database-ownership.md), [AGENTS](../../AGENTS.md).
- Code references: entry points выше; обработчики здесь не проверялись.
- Проверено: 2026-09-11, HEAD `63ce6a7`, документальная сверка ролей, не runtime.
- Обновить при: terminal endpoints/auth, ownership, migration policy.