# Агентский контекст: читать выборочно

Skills описывают **как работать**, карточки — **что знать**, handoff — **что передать**.
Это индекс, не замена протоколов в docs и не новые права доступа.

## Быстрый маршрут
1. [AGENTS.md](../AGENTS.md) → [intake](../.claude/skills/repo-intake-and-routing/SKILL.md).
2. При неизвестном контуре — [system overview](system-overview.md); иначе сразу
   одна компонентная карточка + контракты изменяемого flow из таблицы ниже.
3. Проверь источники и риски карточки; открой первичные файлы только в разрешённом scope.
4. Выбери дополнительные skills по триггеру. Не загружай каталог целиком.
5. Сложную задачу заверши [handoff](tasks/handoff-template.md), обнови затронутую карточку.

## Маршрутизация ролей
| Роль / задача | Минимальный контекст | Дополнительный skill | Результат |
|---|---|---|---|
| Triage / Router | [Overview](system-overview.md) | intake | scope, owner, риски |
| MenuBuilder Agent | [MenuBuilder](components/menubuilder.md), контракт UI/API задачи | frontend-remote-control-safety | UI/BFF + validation |
| Processing Agent | [ProcessingBackend](components/processing-backend.md) | api-and-data-ownership | API/миграция |
| Shared DB Agent | [Shared DB](components/shared-db.md) | api-and-data-ownership | ORM + совместимость |
| Terminal C Agent | [l4desk](components/l4desk.md), MQTT + lease | native-windows-tool-change | native change + build evidence |
| Console Agent | [l4con](components/l4con.md), [console](contracts/remote-console.md) | cross-stack-contract-audit | совместимость RPC/вывода |
| Contract Guardian | Только контракты flow, [app1](components/app1.md) при его участии | cross-stack-contract-audit | producer/consumer matrix |
| QA / Recovery Agent | [Validation](operations/validation-matrix.md), [lease](contracts/lease-lifecycle.md) | verification-matrix, incident-triage-and-evidence | проверенные сценарии |
| Docs Curator | [Регламент](../docs/etran_dev-agent-context-workflow.md) | documentation-and-context-curation | индекс + актуальные карточки |
| Release Agent | [Deployment](operations/deployment-invariants.md), handoff компонентов | verification-matrix | release order / rollback |
| MQTT probe | [Terminal 773](testing/mqtt-device-probe-terminal-773.md), MQTT matrix | mqtt-device-probe-testing | scoped probe result |

## Контракты
- [MQTT topic matrix](contracts/mqtt-topic-matrix.md) — ctl, console, presence; не весь MQTT платформы.
- [Lease lifecycle](contracts/lease-lifecycle.md) — fail-closed vs recovery.
- [Remote control](contracts/remote-control.md) — UI/BFF/app1/input.
- [Video streaming](contracts/video-streaming.md), [l4media](components/l4media.md) — медиатракт.
- [Remote console](contracts/remote-console.md) — RPC и stdout/stderr.

## Каталог skills
Каждый навык хранится как `.claude/skills/<name>/SKILL.md` с `name`/`description`.
В JetBrains открывай нужный файл по ссылке; автоматическая загрузка не предполагается.

- [repo-intake-and-routing](../.claude/skills/repo-intake-and-routing/SKILL.md)
- [cross-stack-contract-audit](../.claude/skills/cross-stack-contract-audit/SKILL.md)
- [lease-and-watchdog-safety](../.claude/skills/lease-and-watchdog-safety/SKILL.md)
- [verification-matrix](../.claude/skills/verification-matrix/SKILL.md)
- [native-windows-tool-change](../.claude/skills/native-windows-tool-change/SKILL.md)
- [frontend-remote-control-safety](../.claude/skills/frontend-remote-control-safety/SKILL.md)
- [api-and-data-ownership](../.claude/skills/api-and-data-ownership/SKILL.md)
- [incident-triage-and-evidence](../.claude/skills/incident-triage-and-evidence/SKILL.md)
- [documentation-and-context-curation](../.claude/skills/documentation-and-context-curation/SKILL.md)
- [mqtt-device-probe-testing](../.claude/skills/mqtt-device-probe-testing/SKILL.md)

Ранее существовавшие навыки сохраняются; они не отменяют scope, secrets policy,
SSH host verification или обязательный readiness. Legacy-навыки не загружать без разрешения.

## Актуальность и передача
- Формат новой карточки: [distillate template](templates/distillate-template.md).
- Метки: **код** = прочитан локальный источник; **документ** = заявлено в docs;
  **требование** = норма/цель; **runtime** = реально выполненная проверка с evidence.
- Базовая сверка: 2026-09-11, локальный HEAD `63ce6a7`, без runtime/deploy/MQTT.
  Дата чтения не означает дату внедрения; app1 извне не проверен.
- При изменении любого источника/контракта карточка требует повторной сверки.
  Устаревшую карточку пометь явно; безопасность не ослабляй по старому тексту.
- [Active packets](tasks/active/README.md) и [completed handoffs](tasks/completed/README.md)
  хранят краткие результаты; длинные исторические отчёты — в docs/history.