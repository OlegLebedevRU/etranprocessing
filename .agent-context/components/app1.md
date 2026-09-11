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
duplicate/late/offline/restart. Шаблон [release handoff](../tasks/release-handoff-template.md).

## Известные риски и незавершённые вопросы
Нет доступа к исходникам/deployed revision в рамках этой задачи. Все сведения о внутренней
реализации app1 — документальные, не гарантия текущего сервиса.

## Источники и актуальность
- Authoritative docs: [E2E](../../docs/etran_arch-video-remote-desktop-e2e.md),
  [console](../../docs/ops_run-remote-console-diagnostics.md).
- Code references: BFF keepalive просмотрен; app1 — не проверен.
- Проверено: 2026-09-11, HEAD `63ce6a7` локального репозитория, без runtime.
- Обновить при: app1 revision, DTO, publish/consume, state/TTL или совместном релизе.