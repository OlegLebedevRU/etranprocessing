# Shared DB: etranprocessing_db

## Назначение
Единый тонкий декларативный ORM-контракт для ProcessingBackend и MenuBuilder.

## Границы ответственности
Models, constraints, indexes, relationships; без auth/crypto/framework/business logic.
Наличие общей модели не даёт права записи. Доменные владельцы определены
[матрицей](../../docs/etran_data-database-ownership.md), Alembic только в ProcessingBackend.

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| PB/MB → models → PostgreSQL | SQLAlchemy 2 / asyncpg | etranprocessing_db | Mapped/mapped_column | единая схема, не общие полномочия |
| Migration → оба backend | Alembic | ProcessingBackend/backend/alembic | schema revision/backfill | expand до нового writer, contract отдельно |

## Инварианты
- MenuBuilder владеет org/users/menu/billing/terminals; PB — payment/balances/cert audit.
- Исключения только column/use-case scoped: PB certificate discovery полей terminal;
  PB loaded_version/loaded_at при выдаче menu binding. Полный список — в ownership doc.
- Любое расширение co-writing требует явной правки матрицы, не случайного импорта.

## State machine
Schema rollout: expand → compatible readers/writers → backfill → switch → contract.
Состояния доменных объектов определяет владелец use case, не shared package.

## Ключевые исходники
- [models](../../shared/etranprocessing_db/models) — декларативные модели.
- [terminal.py](../../shared/etranprocessing_db/models/terminal.py) — terminal identity/model.
- [versions](../../ProcessingBackend/backend/alembic/versions) — миграции общей схемы.

## Проверка
При изменении shared: собственные релевантные проверки + полные pytest и quality обоих
backend, downstream импорты, upgrade с предыдущего head и утверждённый rollback/roll-forward.
Валидация backfill: count/NULL/constraints; tenant scope и старый consumer с новой схемой.

## Известные риски и незавершённые вопросы
Временные прямые чтения не-владельца — compatibility paths, не шаблон расширения.
Схема внешнего app1 не покрывается этой ownership matrix.

## Источники и актуальность
- Authoritative docs: [ownership](../../docs/etran_data-database-ownership.md), [AGENTS](../../AGENTS.md).
- Code references: точки входа выше; ORM не изменялась и не проверялась исполнением.
- Проверено: 2026-09-11, HEAD `63ce6a7`, ownership document.
- Обновить при: fields/constraints, writer transfer, таблицах и schema rollout.