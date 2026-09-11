# Release handoff

## Область и разрешение
- Release ID / ответственный / согласованный контур:
- Компоненты, source revisions, build artifact/hash:
- Согласование деплоя / прямых server edits (если вообще требуются):
- [Deployment invariants](../operations/deployment-invariants.md) / readiness:

## Совместимость producer/consumer
| Contract/schema | Producer old/new | Consumer old/new | Поведение old↔new | Evidence / ограничение |
|---|---|---|---|---|
| | | | | |

## Порядок релиза
1. <Preflight/resources, сохранение согласованного rollback artifact.>
2. <Expand migration общей схемы через ProcessingBackend, если нужна.>
3. <Совместимые consumer/producer в доказанном порядке; не универсальный догмат.>
4. <Backfill/switch отдельно с метриками; contract cleanup отдельным релизом.>
5. <Endpoint/workload verification и handoff.>

## Gates и rollback conditions
- До релиза: <tests/build, обе стороны, permissions/TTL/retain, недоступные проверки>.
- Остановить rollout при: <наблюдаемый критерий ошибки / threshold / owner решения>.
- Rollback: <точные старые артефакты и совместимость с уже применённой схемой>.
- Данные: <backup/backfill recovery; где downgrade недопустим и нужен roll-forward>.
- После: <endpoint + реальный workload; compile/container running недостаточно>.

## Результат
- [ ] Выполнено: <команды, exit codes, версии, evidence UTC>.
- [ ] Не выполнено: <причина / риск / владелец>.
- Cleanup / remaining resources:
- Обновлённые карточки / следующий агент:

Шаблон не даёт разрешение на релиз и не свидетельствует о проведённой проверке.