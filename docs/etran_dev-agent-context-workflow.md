# Агентская рабочая память: skills, context, handoff

## Назначение и область
Документальное внедрение обоих разделов агентского набора: короткие навыки,
контекстные карточки и передача результата. Исполняемая автоматизация этапа 3
отложена по согласованию; этот документ не запускает тесты, MQTT или деплой.

Главное правило: **не начинать реализацию без владельца изменения, контракта,
инвариантов безопасности и способа проверки результата**.

## Три независимых слоя
| Слой | Где | Что хранить | Чего не хранить |
|---|---|---|---|
| Правила | [AGENTS.md](../AGENTS.md) | scope, security, короткий маршрут | весь контекст всех компонентов |
| Skills: как работать | [.claude/skills](../.claude/skills) | trigger, шаги, обязательный результат | исторические логи/секреты |
| Distillates: что знать | [.agent-context](../.agent-context/README.md) | owner, contract, invariants, sources, risks | копии больших спецификаций |
| Handoff: что передать | [tasks](../.agent-context/tasks/handoff-template.md) | delta, evidence, unresolved, следующий шаг | неподтверждённые «всё работает» |

Навыки — обычные Markdown с YAML name/description, пригодные для чтения в JetBrains.
Автоматическое discovery конкретным агентом не предполагается: AGENTS даёт явный маршрут.
Существующие специализированные skills не отменяют более строгие правила доступа/секретов.

## Правила минимального контекста
1. Intake → один компонент → только контракты его задачи; overview нужен при неизвестном scope.
2. Ролевую маршрутизацию брать из [индекса](../.agent-context/README.md), а не читать все карточки.
3. Ориентир: skill до 60 строк, карточка до 100; длинную таблицу выделять ссылкой.
4. До изменения протокола открыть актуальные producer/consumer sources. Недоступная
   сторона остаётся «не проверено», а не восстанавливается из старого промпта.
5. Источники ограничивают утверждения: **документ**, **код**, **требование**, **runtime**.
   Более свежая дата карточки не отменяет протокол и не доказывает deployment.
6. Найденный конфликт сохранить как risk и уточнить нужное поведение; не менять
   wire поля, MQTT topic/client type, ACL или packaging по собственной инициативе.

## Формат и жизненный цикл
- [Distillate template](../.agent-context/templates/distillate-template.md): все разделы
  обязательны; неприменимое обозначать N/A с причиной, не выдумывать state machine.
- [Handoff template](../.agent-context/tasks/handoff-template.md): scope/revision, contracts,
  invariants, выполненные/невыполненные проверки, evidence и следующий шаг.
- [Release handoff](../.agent-context/tasks/release-handoff-template.md): обе ревизии,
  old/new compatibility, migration/deploy order, gates и rollback conditions.
- Активный packet — в tasks/active; после завершения краткий итог — tasks/completed
  и ссылка в индексе. Не поддерживать две расходящиеся копии активного результата.
- Длинные исторические отчёты/логи — docs/history по его правилам, без секретов.

## Ревизия и ответственность
Владелец компонента актуализирует карточку в той же задаче, что меняет контракт;
Contract Guardian сверяет cross-stack, Docs Curator — формат/ссылки/индексы.
Перед использованием и каждым затрагивающим релизом сверять источники/ревизии;
раз в месяц куратор проводит короткую ревизию ссылок и открытых рисков.
Это регламент, не настроенное расписание автоматического задания.

Карточка устарела при изменении topic/payload/DTO/state/owner/TTL/build/deploy,
удалении первоисточника или появлении противоречащего evidence. Пометить
«требует сверки», указать причину; не просто обновлять дату без просмотра источника.
Активные документы docs используют naming convention и регистрируются в docs/README.md;
служебные имена skills/context/templates не подчиняются префиксам docs.

## Этап 3: дальнейшая автоматизация (не реализована)
| Работа | Владелец / prerequisites | Критерий будущей готовности |
|---|---|---|
| Проверка docs links/naming/sections/index | Docs Curator; утверждённые исключения history | CI отклоняет broken link, bad active name, missing source/date/index; negative fixtures |
| MQTT contract fixtures app1 ↔ l4desk | обе source revisions, утверждённые schemas | valid/invalid/expired/duplicate совместимы с обеими сторонами |
| Воспроизводимые E2E recovery сценарии | isolated terminal, ownership PID, broker ACL | renew дольше исходного окна, expiry без restart, unexpected_exit bounded, reconcile/UI |
| Release compatibility gate | release handoff обеих сторон | old/new matrix, expand-first migrations, проверенный rollback/roll-forward |
| Регулярная ревизия distillates | component owners + curator | stale карточки выявляются до реализации, evidence/date обновлены осмысленно |

## Границы MQTT probe
[Навык](../.claude/skills/mqtt-device-probe-testing/SKILL.md) и
[карточка terminal 773](../.agent-context/testing/mqtt-device-probe-terminal-773.md)
разрешают только согласованные сценарии существующих протоколов. Сначала passive observe;
localhost не доказывает изоляцию при наличии bridge. Device identity не даёт server ACL.
Probe event проверяет consumer, не заменяет реальный FFmpeg или E2E.

## Известные ограничения начального набора
- app1 не проверен по внешним исходникам; runtime/MQTT/E2E/deploy не выполнялись.
- В C выявлены [gaps renew/expiry/dedup](../.agent-context/contracts/lease-lifecycle.md);
  исправление требует отдельной code-задачи, не изменения формулировки гарантии.
- Устаревшая ссылка docs/remote-input-protocol.md не используется как источник;
  существующие remote-input/E2E docs и код сверяются совместно.

## Проверка документальной задачи
Проверить ссылки, naming, frontmatter skills, обязательные разделы карточек,
индексы и отсутствие секретов в diff. Прикладные pytest/linters/build не нужны.
Результат проверки записывается в completed handoff, отдельно от будущих runtime тестов.