---
name: api-and-data-ownership
description: API, поля/таблицы и статусные модели — единый владелец данных, shared ORM, Alembic ownership и совместимый порядок миграции/релиза.
---

# API and data ownership

1. По [ownership matrix](../../../docs/etran_data-database-ownership.md) определи
   владельца таблицы и точные разрешённые записи. Импорт ORM не даёт права write.
2. Общие models/constraints/relationships — только shared/etranprocessing_db;
   без бизнес-логики, auth/crypto и FastAPI dependencies.
3. Все Alembic общей схемы — ProcessingBackend/backend/alembic/versions;
   proposal готовит доменный владелец, в MenuBuilder не создавать вторую цепочку.
   Внешняя схема app1 не становится общей по аналогии — уточни её владельца.
4. Tenant scope обязателен; org_id из JWT нормализуется в int на auth boundary.
   Pydantic settings — extra=ignore, defaults без секретов/credentials.
5. Опиши state machine: состояния, переходы, инициаторы, события, повтор и ошибка.
6. Планируй expand → compatible readers/writers → backfill → switch → contract.
   Старый consumer должен пережить новый producer; destructive cleanup отдельно.
7. При shared changes — тесты/quality обоих backend и downstream, migration upgrade
   с предыдущего head и согласованный rollback/roll-forward. Не выдавай read-only
   анализ за фактически выполненную миграцию.

## Результат
Владелец/исключения column-scoped, схема и API compatibility, migration/release order,
проверки обоих backend, [shared card](../../../.agent-context/components/shared-db.md).