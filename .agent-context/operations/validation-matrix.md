# Матрица проверки изменений

## Назначение
Выбирать проверки по scope и отмечать фактически выполненное, не объявлять сборку E2E.

## Границы ответственности
Автор проверяет локальный слой; Contract Guardian — обе стороны;
QA/Release — согласованный end-to-end workload. Миграции общей схемы — ProcessingBackend.

## Внешние контракты
| Direction | Transport | Endpoint/topic | Main payload | Guarantees |
|---|---|---|---|---|
| Автор → следующий агент | handoff | verification table | команда, exit code, UTC/evidence | passed/failed/not run/N/A явно |

## Инварианты
Нет execution evidence — нет passed. PUBACK ≠ terminal ACK ≠ свежий кадр.
Не ослаблять тесты ради pass. Документальные требования ниже не являются результатами тестов.

## State machine
planned → executed → passed/failed; unavailable → not run + причина/риск;
повтор после исправления фиксируется отдельно, исходный отказ не скрывается.

## Ключевые исходники
- [backend tests PB](../../ProcessingBackend/backend/tests), [backend tests MB](../../MenuBuilder/backend/tests).
- [l4desk build](../../tools/l4desk/build.cmd), [ctl](../../tools/l4desk/src/ctl_protocol.c).
- [frontend](../../MenuBuilder/frontend), [handoff](../tasks/handoff-template.md).

## Проверка
| Изменение | Обязательный набор после разрешённых code changes |
|---|---|
| Только Markdown | ссылки/naming/обязательные разделы/индексы/secrets review; без pytest/ruff/pyright/npm/native build |
| PB backend | из ProcessingBackend\backend: uv run pytest; ruff check --fix app, ruff format app, pyright app через uv run; также изменённый test/src scope |
| MB backend | аналогично из MenuBuilder\backend; все релевантные downstream tests |
| shared | собственные релевантные tests/quality + полные pytest/quality обоих backend; upgrade/rollback совместимость |
| MB frontend | npm run build из MenuBuilder\frontend + существующие релевантные UI tests и browser сценарии |
| native tools | build.cmd конкретной утилиты: x86/x64/default + локальные protocol/process tests; не pytest backend только из-за tools |
| cross-stack | сначала локальные слои, затем обе стороны и полная цепочка с correlation |

Auth tests: short-lived create_access_token для JWT; simulated certificate headers
X-Client-Cert-DN/Serial для terminal routes. Никогда не production tokens.

| Lease/recovery сценарий | Требуемый результат / evidence |
|---|---|
| running + valid renew | ACK, expiry вырос, FFmpeg жив дольше исходного watchdog окна |
| другой lease_id | NACK lease_mismatch, состояние неизменно |
| нет active stream | NACK stream_not_running |
| missing/zero/past expiry | отказ/без unsafe recovery; текущий gap ACK без renew отмечен в lease card |
| duplicate command_id до TTL | исходный response, без повторного побочного эффекта |
| duplicate после TTL/cache eviction | исход по документированной policy; не обещать cache replay |
| прекращён keepalive | stopped/lease_expired, input released, автоматического restart нет |
| unexpected FFmpeg exit с valid lease | restarting → bounded retry → running/recovered |
| lease истекла во время backoff | fail-closed, следующая попытка не стартует |
| превышен retry budget | failed/restart_limit, нет бесконечного цикла |
| restart агента со своим orphan | orphan завершён, stopped/agent_restart_reconcile |
| terminal stopped/failed дошёл до UI | исчез ложный running, видна reason |
| restart producer/consumer, late/out-of-order event | нет обновления чужой lease/эпохи, UI reconcile |
| UI stop/error/unmount/потеря прав | таймеры и ввод прекращены; камера/view-only без pointer |

Console: ping/preset, seq/eof/res, TTL/cancel, duplicate, malformed/unknown method,
output cap, reconnect. Probe: [terminal 773](../testing/mqtt-device-probe-terminal-773.md).

## Известные риски и незавершённые вопросы
E2E automation/contract fixtures не добавляются этой doc-only итерацией.
Известные [lease gaps](../contracts/lease-lifecycle.md) требуют отдельного bug-fix scope.
MQTT-emulated event подтверждает consumer parsing, не реальный FFmpeg recovery.

## Источники и актуальность
- Authoritative docs: [AGENTS](../../AGENTS.md), [ownership](../../docs/etran_data-database-ownership.md),
  [E2E](../../docs/etran_arch-video-remote-desktop-e2e.md).
- Code references: ctl/supervisor статически просмотрены; test suites здесь не запускались.
- Проверено: 2026-09-11, HEAD `63ce6a7`, требования к будущей валидации, не runtime report.
- Обновить при: scope/testing rules, контракте, regression incident, new consumer.