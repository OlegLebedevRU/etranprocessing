# Agent handoff: начальный набор рабочей памяти

## Контекст задачи
- Scope: согласованный первый вариант — документальный набор chapter 1/2,
  без реализации автоматизации этапа 3 и без изменения приложений/MQTT-клиентов.
- Репозиторий: etranprocessing, исходный HEAD `63ce6a7`; итог — working tree, без commit.
- Дата: 2026-09-11; runtime/UTC timeline терминала отсутствует — сервисы не запускались.
- Владелец контракта этой задачи: регламент работы агентов; остальные владельцы — в карточках.

## Выполнено
- 10 новых skills, 16 дистиллятов, выборочный индекс/роли и вход через AGENTS.md.
- Шаблоны distillate, task/release handoff и MQTT probe result; регламент ревизии/roadmap.
- Исправлена битая ссылка AGENTS; RabbitMQ skill больше не содержит встроенной
  авторизации и отключения SSH host verification, ссылается на readiness.

## Затронутые контракты
| Contract | Producer | Consumer | Compatibility |
|---|---|---|---|
| Агентская передача | текущий агент | следующий агент | Markdown; явные scope/evidence/risks |
| ctl / console / API | существующие приложения | существующие приложения | wire-контракты не изменены; совместимость runtime не проверена |

## Изменённые инварианты
- Runtime-инварианты не менялись. Закреплено: owner/contract/safety/verification до реализации.
- Требования безопасности отделены от реализации и документальных заявлений.

## Проверено
- [x] Локальная проверка новых документов и новых ссылок в изменённых документах:
  relative targets, обязательные разделы, frontmatter, indexes/naming, fences и whitespace.
  38 документов, 203 локальные ссылки. Временный read-only verifier:
  `uv run --no-project python verify_agent_docs_tmp.py`, exit 0; после проверки удалён.
- [x] Все 10 новых skills ≤60 строк, 16 карточек ≤100; targeted safety scan без находок.
- [x] `git diff HEAD --check`, exit 0; статическая сверка ctl renew/dedup,
  supervisor watchdog/recovery, build.cmd, BFF keepalive и ownership docs.
- [ ] MQTT/device binding/ACL/bridge, app1 sources, E2E/recovery/deploy — не выполнялись,
  вне согласованного scope. Утверждения внешнего сервиса остаются документальными.
- N/A: pytest/ruff/pyright/npm/native build — изменения только Markdown.

## Наблюдаемое evidence
- [Lease gaps и code references](../../contracts/lease-lifecycle.md): ACK без expiry,
  expiration до dedup, grace +5 с/expiry >0; это статические наблюдения, не runtime repro.
- [Регламент](../../../docs/etran_dev-agent-context-workflow.md), [индекс](../../README.md).
- Correlation identifiers: N/A — ни MQTT-команды, ни lease, ни stream не создавались.
- Уровень: документы + выборочное чтение кода + локальная проверка Markdown.

## Риски и следующие действия
- Terminal/App1 owner: отдельная code-задача для reproducer/fix найденных lease gaps;
  не считать их устранёнными текстом. Нужны обе source revisions и изолированный стенд.
- Docs/QA owners: автоматизация links/naming/fixtures/E2E оставлена следующей итерации;
  текущая разовая проверка не является добавленным CI gate.
- Владелец инфраструктуры: оценить, использовались ли credentials из старого примера
  RabbitMQ skill; если да — ротация и оценка истории отдельно. Ротация здесь не выполнялась.
- Cleanup: временный verifier удалён; test sessions,
  процессы FFmpeg, retained сообщения и remote ресурсы не создавались.
- В конце в рабочем дереве обнаружены посторонние untracked `.output.txt` и
  `incident_contract_audit_20260911/`; не создавались этой задачей, не открывались и не изменялись.

## Обновить context distillates
- [MQTT](../../contracts/mqtt-topic-matrix.md), [lease](../../contracts/lease-lifecycle.md),
  [l4desk](../../components/l4desk.md) — созданы, обновить после code/runtime проверки.
- [Probe 773](../../testing/mqtt-device-probe-terminal-773.md) — создан; фактические identity/ACL
  подтверждать перед запуском, не переносить секреты в карточку.