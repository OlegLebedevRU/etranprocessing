---
name: incident-triage-and-evidence
description: Разбор 404/409, ложного В эфире, MQTT-разрывов, recovery и deployment incidents через UTC timeline и проверяемое evidence, без догадок о причине.
---

# Incident triage and evidence

1. Сначала scope/разрешения, затем факты; не объявляй причину по одному симптому.
2. Собери UTC timeline: UI → HTTP → lease → MQTT → ACK/NACK → действие агента
   → stream_event → UI/кадры. Для console: tsk → req → rsp → out → res.
3. Для каждого звена заполни таблицу; отсутствие сообщения в одном логе не доказывает
   отсутствия публикации. Укажи окно наблюдения, retained flag и clock skew.
4. Связывай command_id (не путать с локальным cmd_id), lease_id, stream_instance_id,
   terminal SN; для console — correlationData/session_id/seq.
5. Отдели root cause от симптома, подтверждённое от гипотезы. Не публикуй JWT,
   PIN, ключи, credentials, connection profile или приватный shell/keyboard output.
6. Для серверной диагностики сначала [readiness](../../../.agent-context/operations/deployment-invariants.md);
   для MQTT — passive observe и [probe boundaries](../../../.agent-context/testing/mqtt-device-probe-terminal-773.md).
7. Если исправление разрешено: regression reproducer до/после и обновление карточки.
   Если задача read-only — рекомендации, без правок и незапрошенных запусков.

## Evidence
| UTC / звено | Ожидалось | Наблюдаемый факт | Источник / окно | Correlation | Вывод / неизвестное |
|---|---|---|---|---|---|
| | | | | | |

Результат: первая подтверждённая точка расхождения, альтернативные гипотезы,
минимальная следующая проверка и regression scenario, а не полный дамп логов.