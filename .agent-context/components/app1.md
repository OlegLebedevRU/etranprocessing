# app1 / iot-rpc-rest-app: внешняя граница

## Назначение
Оркестратор remote-input lease, MQTT/RPC и событий согласно локальной архитектуре.

## Границы ответственности
Server lease/owner/scope/TTL, ctl publisher, terminal event consumer, console RPC.
Исходники app1 вне текущего дерева: до реализации запросить разрешённый checkout и ревизию.
Владелец его БД/миграций должен быть подтверждён в том репозитории, не назначен по аналогии.

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| BFF → app1 | REST/WS | /api/internal/v1/remote-input | lease, scope, input, trusted headers | внутренний API, owner/tenant checks требуются |
| app1 → l4desk → app1 | MQTT | srv/dev {SN}/ctl | ctl v1 / events | no retained actions, command_id/lease_id |
| app1 ↔ l4con | RPC/MQTT | tsk/rsp/cmt ↔ req/res/out | method/session/task | не заменять ctl-контрактом |

## Инварианты
Keepalive сервера должен обновлять локальную lease через terminal command.
Terminal event должен синхронизировать lease/UI; HTTP/PUBACK недостаточно.
Совместимость нельзя подтвердить только чтением BFF.

## State machine
Lease и stream state разделены; нормативная [lease lifecycle](../contracts/lease-lifecycle.md).
Локальные docs заявляют очистку server stream по stopped/failed; это не проверка runtime.

## Ключевые исходники
- [iot_client.py](../../MenuBuilder/backend/app/services/iot_client.py) — локальный consumer API app1.
- [video_control.py](../../MenuBuilder/backend/app/routers/video_control.py) — публичная BFF граница.
- Внешние DTO/publisher/consumer/tests — запросить у владельца app1 перед изменением.

## Проверка
Producer/consumer fixtures обеих ревизий, REST и WS keepalive → MQTT → ACK → local expiry;
тесты `test_step4_reproduce_and_contracts.py`, полный набор pytest (101 тест), ruff check/format.

## Актуальный статус реализации (Шаг 4)
- Поле `CtlLeaseRenew` сериализует каноническое wire-поле `command_id` UUID.
- Поддержаны семантика `renew_status` (`server_accepted`, `terminal_applied`, etc.) и `applied_deadline_ms`.
- Защищен режим камеры от сброса в desktop при re-acquire и scope upgrade.
- Безопасный идемпотентный release для владельца, изоляция эпох стримов.
- Сервис протестирован и задеплоен на хост 87.242.100.34 (200 OK на /docs).

## Источники и актуальность
- Authoritative docs: [E2E](../../docs/etran_arch-video-remote-desktop-e2e.md),
  [console](../../docs/ops_run-remote-console-diagnostics.md),
  [protocol specification](../../docs/ingress_iot/remote-input-protocol.md).
- Code references: BFF keepalive и schemas/service app1 актуализированы.
- Актуализировано: 2026-09-11, реализация Шага 4, деплой на 87.242.100.34.
- Обновить при: стендовой E2E-верификации и ревизии provisioning топиков.