# Shared DB: etranprocessing_db

## Контракт031 (2026-10-05, локально проверен)

Terminal.l4desk_subscription_enabled Boolean NOT NULL DEFAULT false; migration owner PB, нет backfill. v031 snapshots/package provenance опубликованы рядом с сохранёнными v030. Pure subscription evaluator — отдельный zero-dependency etranprocessing_access, вне ORM. Shared65tests, оба backend полные проверки, l4mcp downstream21tests, реальная030→031 migration прошли. Production ещё030; старый MB bridge030/031 выложен перед финальной миграцией. [Handoff](../tasks/active/2026-10-05-licensing-implementation.md).

## 2026-10-03: production schema029

Аддитивная migration029 добавила nullable timestamptz `L4DeskTerminal.paid_until`.
Новых таблиц/drop/конвертации старых финансов нет. PB и MB обновлены из 8bb0359;
MB проверяет revision и колонку. Schema/provenance artifacts v029 — текущий
контракт source package. [Release evidence](../tasks/completed/2026-10-03-l4desk-terminal-subscriptions-release.md).

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

Пакет `093f985` (2026-09-28): additive migration `028` расширяет `orgs`
политикой навигации и видимости двух страниц лицензий. В production `028`
применена; MenuBuilder требует `028` в startup guard (`bacea78`). Приёмка
контроллером остаётся открытой; evidence — handoff MenuBuilder после 18E.
Временные прямые чтения не-владельца — compatibility paths, не шаблон расширения.
Схема внешнего app1 не покрывается этой ownership matrix.

## Источники и актуальность
- Authoritative docs: [ownership](../../docs/etran_data-database-ownership.md), [AGENTS](../../AGENTS.md).
- Code references: точки входа выше; ORM не изменялась и не проверялась исполнением.
- Проверено: 2026-09-11, HEAD `63ce6a7`, ownership document.
- Обновить при: fields/constraints, writer transfer, таблицах и schema rollout.
