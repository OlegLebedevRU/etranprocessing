---
name: architecture-decision-record
description: Фиксация архитектурных решений (ADR) с контекстом, драйверами, альтернативами, последствиями и строгой проверкой соответствия контрактам платформы.
---

# Architecture decision record

1. **Контекст и драйверы**:
   Определи решаемую архитектурную проблему, статус (`Proposed`, `Accepted`, `Superseded`, `Deprecated`),
   бизнес- и технические драйверы, явные ограничения и требования к качеству. Отделяй проверенные
   факты от предположений.
2. **Проверка проектных контрактов**:
   Сопоставь решение с авторитетными контрактами [.agent-context/contracts/](../../../.agent-context/contracts/):
   - [Lease lifecycle](../../../.agent-context/contracts/lease-lifecycle.md): fail-closed при `lease_expired`,
     ограниченный recovery только при действующей lease, недопустимость renew без увеличения `expires_at_ms`.
   - [MQTT topic matrix](../../../.agent-context/contracts/mqtt-topic-matrix.md): строгое соблюдение иерархии
     топиков (`srv/{SN}/...`, `dev/{SN}/...`), запрет ad-hoc топиков (`srv/{SN}/cmd`, `dev/{SN}/ctrl`)
     и retained-сообщений для команд/ACK/NACK.
   - [Remote control](../../../.agent-context/contracts/remote-control.md) и [video streaming](../../../.agent-context/contracts/video-streaming.md):
     разделение ввода и видео, сохранение generation-трекинга сессий.
   - [Remote console](../../../.agent-context/contracts/remote-console.md): строгая типизация RPC (7001-7004),
     неинтерактивное выполнение, ограничение вывода/TTL.
3. **Модель данных и владение**:
   Сверь решение с [api-and-data-ownership](../api-and-data-ownership/SKILL.md) и [shared DB](../../../.agent-context/components/shared-db.md):
   - Тонкий слой ORM в `shared/etranprocessing_db` без бизнес-логики и FastAPI dependencies.
   - Единственный владелец миграций схемы — `ProcessingBackend/backend/alembic/versions/`.
   - Обязательная изоляция арендаторов (`org_id` нормализуется в `int` на auth boundary).
4. **Анализ альтернатив и компромиссов**:
   Опиши минимум 2 альтернативы (включая статус-кво). Явно укажи цену выбора, компромиссы (tradeoffs)
   и отброшенные варианты с причинами отказа. Избегай модных решений без обоснования пользы.
5. **Последствия и порядок миграции**:
   Зафиксируй положительные и отрицательные последствия, влияние на runtime-нагрузку и инварианты
   развертывания ([deployment-invariants](../../../.agent-context/operations/deployment-invariants.md)).
   При изменении схемы/API опиши двухфазную совместимость (expand → compatible readers/writers → switch → contract)
   и план отката (rollback).
6. **Верификация и handoff**:
   Свяжи критерии приемки с [матрицей верификации](../../../.agent-context/operations/validation-matrix.md).
   Обнови затронутые контракты и компонентные карточки в той же задаче.

## Обязательный результат
```markdown
# ADR-<NUMBER>: <Краткое название решения>

## Статус
[Proposed | Accepted | Superseded by ADR-X | Deprecated] — YYYY-MM-DD

## Контекст и проблема
<Описание решаемой проблемы, ограничений и технических драйверов>

## Соответствие контрактам и инвариантам
| Контракт / Правило | Влияние решения | Проверка инварианта |
|---|---|---|
| [lease-lifecycle](../../../.agent-context/contracts/lease-lifecycle.md) | | Fail-closed / recovery соблюдены |
| [mqtt-topic-matrix](../../../.agent-context/contracts/mqtt-topic-matrix.md) | | Топики по стандарту, retain=false |
| [shared-db](../../../.agent-context/components/shared-db.md) / Alembic | | Тонкий ORM, единый источник миграций |
| Изоляция тенантов | | org_id -> int, разделение данных |

## Рассмотренные альтернативы
1. **<Вариант 1 (Выбранный)>**: плюсы, минусы, компромиссы.
2. **<Вариант 2>**: почему отклонён.
3. **<Статус-кво>**: почему недостаточен.

## Решение и обоснование
<Суть принятого архитектурного решения и ключевые аргументы>

## Последствия и риски
- Положительные: <выигрыш в надёжности, скорости, поддерживаемости>
- Отрицательные / Ограничения: <технический долг, усложнение>
- План миграции и отката: <порядок релиза компонентов>

## Дальнейшие шаги (Follow-up)
- [ ] Обновить карточки в `.agent-context/contracts/`
- [ ] Добавить проверки в [validation-matrix](../../../.agent-context/operations/validation-matrix.md)
```

## Источники и контракты
- [Lease lifecycle](../../../.agent-context/contracts/lease-lifecycle.md), [MQTT matrix](../../../.agent-context/contracts/mqtt-topic-matrix.md).
- [Remote control](../../../.agent-context/contracts/remote-control.md), [Remote console](../../../.agent-context/contracts/remote-console.md), [Video streaming](../../../.agent-context/contracts/video-streaming.md).
- [Database ownership](../../../docs/etran_data-database-ownership.md), [Deployment invariants](../../../.agent-context/operations/deployment-invariants.md).
