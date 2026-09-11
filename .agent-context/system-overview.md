# Система etranprocessing

## Назначение
Карта владельцев для выбора минимального контекста payment, portal и remote operations.

## Границы ответственности
| Компонент | Владеет | Не владеет |
|---|---|---|
| ProcessingBackend | payment/mTLS gateway, Alembic общей схемы | пользовательский JWT portal |
| MenuBuilder | UI, JWT/tenant/admin, billing API, BFF | terminal execution, server lease app1 |
| shared/etranprocessing_db | декларативные ORM models/constraints/relations | бизнес-логика, auth, миграции |
| app1 / iot-rpc-rest-app | оркестрация lease, MQTT/RPC по локальным docs | код отсутствует в текущем дереве |
| tools/l4desk | remote input, FFmpeg lifecycle/watchdog | серверная авторизация, media routing |
| tools/l4con | console RPC, дочерний cmd/powershell, output | desktop ctl |
| l4media | ingress, media routing/WebRTC через Janus | lease authority |

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| Terminal → ProcessingBackend | HTTPS/mTLS через Nginx | payment, tech, ListMenuFile | terminal identity/request | certificate + terminal auth |
| Browser → MenuBuilder | HTTPS/WS | API портала / video / console | JWT context, device ID | tenant/permission boundary |
| MenuBuilder → app1 → terminal | REST/WS → MQTT | remote-input ctl / console RPC | lease/command/session | см. карточку соответствующего flow |
| FFmpeg → media → browser | RTP → L4RTP → WebRTC | media plane | encoded frames | не доказательство lease или permissions |

## Инварианты
- Один владелец общей ORM-схемы; Alembic только в ProcessingBackend.
- SN, device_id, lease_id, stream_instance_id и command_id не взаимозаменяемы.
- PUBACK ≠ ACK агента ≠ свежие кадры в браузере.
- Корневые запреты доступа остаются в силе; карточка tools не разрешает открывать tools.

## State machine
Единой машины состояний нет: [lease](contracts/lease-lifecycle.md),
[video](contracts/video-streaming.md) и [console](contracts/remote-console.md) независимы.

## Ключевые исходники
- [video_control.py](../MenuBuilder/backend/app/routers/video_control.py) — BFF control.
- [iot_client.py](../MenuBuilder/backend/app/services/iot_client.py) — граница app1.
- [shared models](../shared/etranprocessing_db/models) — общая схема.
- [ctl_protocol.c](../tools/l4desk/src/ctl_protocol.c) — terminal contract (только разрешённый scope).

## Проверка
Выбирать по [validation matrix](operations/validation-matrix.md); для сквозного flow
нужны обе стороны контрактов и runtime evidence, не только сборка.

## Известные риски и незавершённые вопросы
app1 — внешний репозиторий; локальные документы не доказывают его актуальную реализацию.
Ранее записанное «реализовано/задеплоено» не означает новую E2E-проверку.

## Источники и актуальность
- Authoritative docs: [AGENTS](../AGENTS.md), [E2E architecture](../docs/etran_arch-video-remote-desktop-e2e.md).
- Code references: ссылки выше; обзор ролей — документ, не полный аудит кода.
- Проверено: 2026-09-11, HEAD `63ce6a7`, docs + выборочная сверка кода, без runtime.
- Обновить при: изменении владельцев, входных API, схемы или границ доверия.