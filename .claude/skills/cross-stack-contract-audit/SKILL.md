---
name: cross-stack-contract-audit
description: Аудит границ REST, MQTT, WebSocket, очередей и native клиентов при изменении endpoint, payload, topic, TTL, lease, роли или lifecycle event.
---

# Cross-stack contract audit

1. После intake определи producer → transport → consumer каждого сообщения.
   Не путай транспортный PUBACK с прикладным ACK или наблюдаемым действием.
2. Заполни единую таблицу; неприменимые поля обозначь N/A, неизвестные — «не проверено».
3. Для каждого поля укажи обязательность, default, единицы, версию, поведение старого
   клиента и обратную совместимость. Не подменяй реальные имена общим envelope:
   ctl использует `command_id`, console RPC — `correlationData`/`session_id`.
4. Проверь все необходимые стороны, tenant/owner/scope и порядок релиза.
   Недоступный producer/consumer означает ограниченную проверку, а не совместимость.
5. Проверь stale/expired lease, wrong lease_id, duplicate, offline, reconnect,
   restart producer/consumer, late/out-of-order event, потерю прав.
6. Обнови авторитетный протокол и карточку в той же задаче, если контракт меняется.

## Обязательный результат
| Направление / владельцы | Канал / topic или path | Schema / версия | QoS / retain | Idempotency / duplicate | Timeout / TTL | ACK/NACK / ошибка | Compatibility |
|---|---|---|---|---|---|---|---|
| producer → consumer | | | | | | | |

Дополнительно: список сторон изменения, evidence, непроверенные звенья и release order.

## Источники по выбору
- [MQTT matrix](../../../.agent-context/contracts/mqtt-topic-matrix.md).
- [Remote control](../../../.agent-context/contracts/remote-control.md),
  [lease](../../../.agent-context/contracts/lease-lifecycle.md),
  [console](../../../.agent-context/contracts/remote-console.md).

Запрещены ad-hoc `srv/{SN}/cmd`, `dev/{SN}/ctrl` и retained команды/ACK/NACK.
Новый diagnostic topic требует отдельного утверждения schema, ACL, TTL и тестового режима.