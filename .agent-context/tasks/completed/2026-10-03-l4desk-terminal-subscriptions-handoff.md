# Подписки L4Desk — результат реализации

## Контекст задачи

- Scope: упрощение модели подписок и UI по варианту A ревизии 2 исходного плана и дополнениям пользователя. Владелец подписок/API — MenuBuilder; ORM — shared; migration — ProcessingBackend. Сессии/epoch/duration остаются техническим ядром.
- Реализация опубликована в main: `8bb035944e709ace5b53df8d76586a4d596cfe89`, base `9cb98b8baa4344645c2bb33ffaa0886d601b3cf0`. [Production release](2026-10-03-l4desk-terminal-subscriptions-release.md) завершён после перечисленных ниже изолированных проверок.
- Проверки реализации: 2026-10-03, Windows/Python 3.14; разрешённый 176 builder, изолированная PostgreSQL 18. Локальный Docker недоступен. Реальная ЮKassa не использовалась; последующий production smoke описан отдельно.
- Применены repo-intake-and-routing, api-and-data-ownership, verification-matrix; карточки компонентов и session/media contracts. Исключённые каталоги не исследовались; MQTT-клиенты не менялись.
- `[MCP Ops Readiness: UNAVAILABLE]`: ops tools в сессии отсутствуют. Для разрешённых проверок использован SSH. Начальный builder preflight: свободная RAM 2798 MiB, диск 57%, load 0.08; production только read-only preflight.

## Выполнено по стекам

1. **shared / схема:** одно nullable поле `paid_until`; аддитивная migration 029; актуальные schema/provenance artifacts и baseline 028 для контракта. Нет новых таблиц, drop, конвертации старых денег или переписывания административного состояния.
2. **MenuBuilder backend:** вычисляемые статусы терминалов, создание дополнительных, серверные admission checks; подписочная корзина и immutable snapshot; проверка provider amount/metadata/capture; сериализация выдачи сроков; адресный session stop/outbox; техническая длительность без проводок; reminders с ограниченным retry; аудируемая ручная корректировка суперпользователем.
3. **ProcessingBackend:** MQTT/RTP allowance = административное разрешение AND подписочная policy. Classic без L4Desk profile автоматически в новую модель не включается. Grace boundary читается из того же audit snapshot.
4. **MenuBuilder frontend:** статусы/причины/следующие действия, серверный просмотр корзины, история/проверка/продолжение оплаты, статистика нагрузки отдельно, статусы в списке терминалов и административная корректировка. Корзина сбрасывается при смене tenant.
5. **Уборка:** прежний финансовый router заменён compatibility auth helper; старые начисления/сторно/reconciliation отключены; старый finance frontend API и UI удалены; startup и presence не запускают деньги. Модели и исторические сервисы заморожены, см. README в financial_core.

## Контракты

| Contract | Producer | Consumer | Compatibility |
|---|---|---|---|
| shared schema 029 | ProcessingBackend migration | PB / MB | Оба consumer должны обновиться вместе; MB проверяет revision и колонку |
| `/api/subscriptions`, `/api/usage` | MenuBuilder | React | Новые payload; `/api/v1/subscriptions` alias; старые money mutation 410/404 |
| payment snapshot | MB + provider verification | `paid_until`, audit terms | Только purpose=subscription; replay не выдаёт второй срок |
| `subscription.terms` | MB verified grant/correction | MB/PB policy | Единые frozen grace/timezone; административное disable имеет приоритет |
| IoT/media sessions | Существующий app1 contract | MB orchestration/metering/stop | Wire protocol/epoch сохранены; целевые устройства не проверены hardware E2E |

## Инварианты

- Один неудалённый терминал по ordinal бесплатен; второй можно подготовить; третий и далее требуют действующую платную подписку дополнительного терминала. Grace не даёт разрешение увеличивать парк.
- `YOOKASSA_ENABLED` — единственный коммерческий gate. OFF блокирует покупку и дополнительные платные возможности. ON открывает их по имеющимся срокам; не выдаёт оплату и не запускает рекуррентные списания. Окружение обоих backend согласуется при штатном restart/release.
- Оплата не отменяет admin disable; деньги не зависят от длительности сессий. Старые posted строки неизменяемы; поздние секунды добавляются audit и видны в статистике.
- FinPayment / audit переиспользованы, но old-purpose payments не конвертируются в подписки. Незавершённый заказ блокирует удаление терминалов tenant.

## Проверено

- [x] MenuBuilder: `uv run pytest -q --tb=short` — **604 passed, 20 skipped**, exit 0. Все skips — отдельные PostgreSQL fixtures без локального test DB; они выполнены на сервере ниже.
- [x] На PostgreSQL 18: `tests/test_subscription_postgres.py` — **20 passed**, exit 0. Duration replay, concurrent verification одной и разных оплат, отсутствие новых ledger transactions, gate OFF, immutable request/receipt, tampered provider response, delete guard/free transfer, HTTP tenant boundary, корректировка/аудит, timezone/grace, reminder retries, адресный stop и старые stop requests, поздние секунды в posted history, additive migration и изоляция подписочных платежей от архивной истории/ложных posting alerts.
- [x] Расширенный серверный прогон: PostgreSQL + pure subscription + metering — **35 passed**, exit 0 (до дополнительного bounded reminder assertion; затем PostgreSQL subset повторён целиком).
- [x] ProcessingBackend: **145 passed**, exit 0; shared: **65 passed**, exit 0.
- [x] `ruff check --fix`, `ruff format`, `pyright` — все три изменённых Python subproject, exit 0; pyright без ошибок. Ruff дополнительно проверил новые тесты и migration.
- [x] Frontend `npm run build` — exit 0; Vitest **70 passed**. У Vite остаётся предупреждение о размере общего vendor chunk.
- [x] Итоговый Playwright после блокировки повторной оплаты: `npm run test:e2e -- --repeat-each=2` — **4 passed**, exit 0; это browser integration с mock API, не hardware/provider E2E.
- [x] Builder собрал runtime-образ MenuBuilder (`--target runtime`) и образ ProcessingBackend из текущих исходников, exit 0. Начальная попытка `--target release` с сокращённым test archive не нашла `MenuBuilder/nginx.conf`; для runtime использована корректная target без verification stage. Production release image flow ещё не выполнялся.
- [x] Runtime MenuBuilder на выделенной PostgreSQL со schema revision 029 и YooKassa OFF: lifespan startup/schema check/shutdown — exit 0, `LIFESPAN_OK_SCHEMA029_PAYMENTS_OFF`; 23 обязательные таблицы найдены. Test schema stamp задавался только в disposable DB, не в production.
- [x] Read-only secret scan изменённых/новых текстовых файлов: 4 совпадения, все проверены — явно тестовые literals (`hash`, `test-only`, test internal key). Реальные credentials не обнаружены.
- [x] Последующий production rollout применён, см. [release evidence](2026-10-03-l4desk-terminal-subscriptions-release.md).
- [ ] Actual provider purchase, hardware MQTT/RTP и терминальные stop E2E — не выполнялись. Их результаты не подменены unit/browser тестами или DB policy smoke.

## Evidence и ограничения

Полный локальный прогон сначала обнаружил отсутствие нового `db.get` в старом тестовом адаптере остановки. Адаптер дополнен deny policy, полный прогон повторён успешно. Браузерный прогон выявил нестабильное имя кнопки при loading; итоговый UI имеет стабильное accessible name и явный disabled во время операции. Дополнительная фикстура архивного платежа сначала нарушала actor/verified constraints и не содержала capture time; исправлена по реальной схеме/контракту без ослабления ограничений, все 20 DB tests повторены успешно.

Временные логи не содержат JWT, PIN, приватного ввода или provider keys. Поведение БД проверялось на выделенной базе `subscription_test`; fixture отказывается работать с иным именем. Проверка migration выполнена реальным PostgreSQL ALTER через Alembic Operations против схемы без новой колонки.

## Остаток и переход

- Реализация кода, schema029 и согласованный rollout завершены по [порядку перехода](../../../docs/menu_bill-terminal-subscription.md). Текущие проверенные production значения и digest записаны в release handoff.
- Коммерческое включение требует env credentials/return URL/чека и одного согласованного `YOOKASSA_ENABLED`, затем отдельной проверки настоящего разрешённого тестового платежа. Архивные billing flags больше не являются коммерческим gate.
- Email outbox best effort: crash после отправки и до commit может повторить письмо. Сроки выдаются в DB атомарно и от этой доставки не зависят.
- Cleanup выполнен: собственные test containers (включая PostgreSQL и его anonymous volume), network, четыре image tag, удалённые archive/source directory и локальные временные archive/logs/test output удалены. Проверка на builder вернула `TASK_RESOURCES_REMOVED`, без оставшихся `l4desk-subscription*` ресурсов. Чужие сервисы/данные не изменены.

## Context distillates

- [Исторический baseline](2026-10-03-l4desk-billing-simplification-context.md) помечен как состояние до реализации.
- [План по стекам](../../../plans/menu_bill-terminal-subscription-implementation-plan.md) обновлён статусом реализации.
- [Правила и переход](../../../docs/menu_bill-terminal-subscription.md), docs index, MenuBuilder CHANGELOG обновлены. После rollout packet перенесён в completed; границы provider/hardware acceptance сохранены.
