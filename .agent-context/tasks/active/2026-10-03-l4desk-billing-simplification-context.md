# Контекст упрощения биллинга L4Desk

Подготовлено 2026-10-03 как историческое исходное состояние и карта затронутых компонентов **до реализации**. Ниже зафиксирован baseline, а не текущее состояние рабочей ветки. План «подписка на терминал», вариант A, ревизия 2, получен из основного checkout. Реализация с дополнениями пользователя: [план](../../../plans/menu_bill-terminal-subscription-implementation-plan.md), актуальное состояние и проверки: [handoff](2026-10-03-l4desk-terminal-subscriptions-handoff.md).

## Контекст задачи и границы

- Запрос: подготовить контекст переработки биллинга и коммерческого режима, без реализации.
- Исходный HEAD: `c6096f4a3a0841ab2b19f717a4dbe4d58de7f24c`.
- Прочитаны локальные источники MenuBuilder, shared и сохранённые отчёты L4D. Production в этой задаче не опрашивался.
- Последний использованный production snapshot: 2026-09-29. Его флаги не следует выдавать за повторно подтверждённое состояние 2026-10-03.
- Владелец L4Desk financial core и пользовательского API — MenuBuilder. ORM — shared, миграции — исключительно ProcessingBackend. IoT/app1 — внешний producer технических событий и исполнитель остановки сессий.
- Разрешённая валидация сейчас: чтение кода и документов, проверка документа. Изменение кода, БД, флагов, подключение к брокеру, запуск платежей и deployment не выполнялись.
- Исключённые каталоги не исследовались. Изменение MQTT-клиента потребует отдельного выбора типа клиента по AGENTS.md.

## Подтверждённое состояние каскада

Источник: [18F production followup](../../../l4desk-service/docs/handoffs/L4D-18F-DOCS-FIX-01-followup-2026-09-29.md), [18E итоговая проверка](../../../MenuBuilder/docs/l4desk/handoffs/L4D-18E-MB-FIX-01-report.md).

| Возможность | Snapshot 2026-09-29 | Следствие |
|---|---|---|
| `l4desk_registration_enabled` | false | Саморегистрация посетителя закрыта |
| `l4desk_terminal_onboarding_enabled` | true | Существующий авторизованный tenant может добавлять терминалы |
| `l4desk_financial_core_enabled` | true | Наличие ядра не означает коммерческую активацию всех flows |
| `l4desk_policy_enforcement_enabled` | false | Финансовый отказ не блокирует admission через L4Desk policy |
| `l4desk_policy_shadow_mode` | true | Заявлен shadow-режим; фактическую ветку определяет enforcement |
| `l4desk_billing_enabled` | false | Не считать коммерческий режим включённым |
| `l4desk_entitlement_worker_enabled` | false | Периодическая проверка grace/block этим worker выключена |
| `l4desk_metering_close_worker_enabled` | false | Worker завершения учёта выключен |
| `l4desk_enabled`, effective YooKassa | false | Общая активация и платёжный provider выключены |

Schema в snapshot — `028 (head)`. Выпуск принят в ограниченном режиме, общая коммерческая доступность не доказана. Тестовые allowlists квоты и entitlement в итоговом 18E были пусты. Consumer имеет отдельные флаги и finance allowlist: текущие runtime-значения здесь не установлены.

## Текущие правила в коде

Источники: [entitlement](../../../MenuBuilder/backend/app/services/financial_core/entitlement.py), [terminals](../../../MenuBuilder/backend/app/services/financial_core/terminals.py), [cycles](../../../MenuBuilder/backend/app/services/financial_core/cycles.py), [tariffs](../../../MenuBuilder/backend/app/services/financial_core/tariffs.py), [metering](../../../MenuBuilder/backend/app/services/financial_core/metering.py).

| Правило | Реализованное поведение |
|---|---|
| Создание терминалов | Onboarding не проверяет баланс, первую оплату, entitlement или максимальное число терминалов. Создание и допуск к сессии разделены |
| Бесплатный терминал | Первый неудалённый по неизменяемому `ordinal`; после удаления льгота переходит следующему |
| Бесплатный объём | 7200 секунд в локальные сутки tenant, общий объём видео и консоли на бесплатном терминале |
| До первой оплаты | `free`, дополнительные терминалы не допускаются финансовой политикой; grace отсутствует |
| Исчерпание free-квоты | При балансе <= 0 новые сессии запрещаются; при положительном балансе политика разрешает продолжение |
| Первая успешная оплата | Фиксирует anchor расчётного цикла. Признак первой оплаты в entitlement — `anchor_at is not None` |
| Последующие оплаты | Не переносят anchor; восстановление баланса >= 0 возвращает доступ |
| После первой оплаты | Баланс >= 0: `active`; баланс < 0 до grace boundary: `grace`; после boundary: `blocked` |
| Grace | Три календарных дня от начала текущего месячного цикла в timezone tenant, не от online, долга или создания терминала |
| Абонентская плата | Default 10000 коп. за дополнительный терминал в цикл, при первом подтверждённом `device_online`; один charge на `(terminal, cycle)` |
| Поздний online | Charge текущего цикла без нового grace; при долге после boundary блокировка |
| Использование | Default 100 коп. за оплачиваемый час. Округление вверх суммарных billable seconds за локальные сутки терминала, не каждого сеанса |
| Денежное округление | `posted = floor(calculated / 100) * 100`; discarded хранится отдельно и не переносится |

Это defaults и локальная реализация. Эффективный тариф хранится версионированно в БД; сегодняшние тарифные строки не считаны. Сохранение бесплатной квоты и отмена месячной платы для free-terminal не ограничены только стадией до первой оплаты.

## Цепочки и владельцы

1. Browser → MenuBuilder registration → reservation tenant ID в IoT → локальный tenant/owner. Регистрация имеет собственный gate.
2. Browser → settings onboarding → бизнес-терминал + L4DeskTerminal → provisioning IoT + certificate PIN в ProcessingBackend. Readiness записи, PIN, IoT и online — отдельные состояния.
3. Video/console → RemoteSessionUseCase → RemoteSessionPolicy → entitlement → app1. Для роли 5 или `is_l4desk` выбирается L4Desk policy; остальные получают PermissiveLegacyPolicy. Tenant/permission/lease-проверки остаются обязательными.
4. Подтверждённые интервалы сессий → remote_session_metering → FinUsageDaily → ledger/projection. Учёт использует целые UTC-секунды и cursor; дни делятся по timezone tenant.
5. IoT event feed → consumer/inbox/checkpoint → коммерческая обработка подтверждённого online при разрешённых настройках → monthly charge. Runtime producer/consumer заново не проверены.
6. Provider/manual payment → payment posting → balance projection + anchor/entitlement. Webhook и poll должны быть идемпотентны.
7. Entitlement worker → grace/block → stop outbox → app1 stop/release → завершение локального учёта. Нельзя считать стоп гарантированным только по наличию кода worker.

## Карта файлов для следующего шага

| Область | Источники от корня репозитория |
|---|---|
| Флаги и запуск workers | `MenuBuilder/backend/app/config.py`, `app/main.py` |
| Регистрация и создание | `app/services/registration_service.py`, `terminal_onboarding_service.py`, `terminal_creation_service.py`; `app/routers/settings.py` |
| Admission | `app/services/remote_session_policy.py`, `remote_session_use_case.py` |
| Сессии и usage | `app/services/remote_session_metering.py`; `financial_core/metering.py`, `metering_close_worker.py` |
| Циклы и правила | `financial_core/entitlement.py`, `cycles.py`, `terminals.py`, `tariffs.py` |
| Деньги | `financial_core/accounts.py`, `posting.py`, `projection.py`, `reversal.py` |
| Платежи | `financial_core/payments.py`, `manual_payments.py`, `yookassa.py` |
| Эксплуатация | `financial_core/worker.py`, `stop_outbox.py`, `notifications.py`, `reconciliation.py`, `archive_service.py` |
| API | `app/routers/finance.py` — `/api/v1/finance/*`; Hub и schemas financial core |
| Consumer | `app/services/iot_event_consumer.py`, `iot_consumer_storage.py` — точки дальнейшей проверки gates |
| Shared ORM | `shared/etranprocessing_db/models/finance.py`, `models/l4desk.py`; MenuBuilder `models_l4desk.py` только re-export |
| UI | `MenuBuilder/frontend/src/api/finance.ts`, `api/hub.ts`, `pages/AdminHubPage.tsx`, `routes/l4desk/L4DeskTerminalsPage.tsx` — точки дальнейшего аудита |
| Отдельный billing flow | `MenuBuilder/frontend/src/routes/billing.tsx` и `/api/billing` не считать автоматически тем же L4Desk financial core; границу уточнить по плану |

Все `app/...` и `financial_core/...` в таблице относятся к `MenuBuilder/backend/app/` и его `services/financial_core/` соответственно. Физические миграции находятся в `ProcessingBackend/backend/alembic/`.

## Сложности и риски для упрощения

- Потенциальный обход квоты: usage читается по terminal_id, удаление free-terminal не переносит расход дня следующему. Статический вывод: следующий может получить дополнительную квоту в те же сутки. E2E не выполнен.
- Нет платёжного gate или лимита количества при создании терминалов. Включить enforcement недостаточно для ограничения числа созданных записей/PIN/provisioning.
- В `L4DeskEntitlementPolicy` отключённый enforcement возвращает `allowed=True`. Поле shadow сохраняется, но само по себе не определяет возврат решения; сочетания флагов требуют проверки по коду.
- `l4desk_enabled` через OR включает effective YooKassa, onboarding и financial core. Нельзя использовать его как универсальный безопасный переключатель запуска без проверки зависимостей.
- UI policy tenant и скрытие страниц лицензий не заменяют финансовую авторизацию backend.
- Проверка новой сессии, остановка текущей, запрет новой console-команды, начисление и получение оплаты — разные точки применения правил. Включение одной не доказывает остальные.
- Величина долга, даты cycle и timezone связаны с grace; поздняя оплата и поздний online не дают новый период. Упрощение должно явно решить судьбу этих связей.
- Исторические тестовые находки и ещё открытые ограничения описаны в 18E/18F отчётах. Этот документ не закрывает их и не переносит исторический E2E на новые правила.

## Что нужно сохранить или явно мигрировать

- Tenant isolation, server-owned terminal identity, проверку роли и права на устройство; не ослаблять mTLS, certificate serial и input lease.
- Согласованность immutable double-entry ledger и balance projection, блокировку/версию projection и идемпотентность operation/source IDs. Если план меняет финансовую модель, требуется отдельное решение миграции истории.
- Идемпотентность payment, monthly charge и event replay; отсутствие повторных списаний при retry.
- Подтверждённые интервалы usage, cursor и waiver недоказанного хвоста. Нельзя тарифицировать недоказанное время ради упрощения.
- Snapshot тарифов и воспроизводимость начислений, reconciliation и audit.
- Совместимость всех потребителей shared ORM; миграции выполняет ProcessingBackend.
- Отсутствие секретов, приватных URL, PIN/JWT и тестовых credentials в tracked документах.

## Решения после получения плана

1. Определить единицу продажи: tenant, терминал, подписка, время или сочетание; судьбу платы за online и почасового usage.
2. Установить лимиты создания и подключения, бесплатную квоту, её владельца и перенос при удалении; правила для новых и существующих tenants.
3. Определить предоплату/долг, сохранение или отмену grace, поведение при нулевом балансе и остановку действующей сессии.
4. Выбрать роль первой оплаты, anchor, timezone и платежных методов; судьбу текущих долгов, остатков и циклов.
5. Разделить коммерческие gates регистрации, onboarding, начислений, admission, stop и provider; определить необходимое поведение Classic.
6. Сопоставить изменения API/UI, событий IoT и shared schema; запросить актуальный контракт app1 при его изменении.
7. Составить миграцию, rollout/rollback и адресный набор тестов. Не включать production-флаги до согласованного плана реализации и проверок.

## Проверки и передача

- [x] Прочитаны intake skill, индекс контекста, компонентная карточка MenuBuilder и handoff template.
- [x] Статически проверены onboarding, free-terminal, entitlement, cycles, tariff defaults, daily rounding, policy routing, config и запуск workers.
- [x] Сверены сохранённые 18E/18F production flags и границы evidence.
- [ ] Live production flags, тарифы и финансовые остатки — не проверены в этой задаче.
- [ ] E2E переноса квоты, массового onboarding, реальных оплат и новых коммерческих правил — не выполнены.
- Тесты/линтеры/build не запускались: изменения только в документации, это соответствует AGENTS.md.
- Код, данные, контрактные инварианты и deployment не изменены; временные credentials, процессы и сессии не создавались.
- Следующий шаг: рассмотреть предложенный план реализации, особенно минимум изменений схемы и transport policy. По дополнению пользователя каскад L4D принимается завершённым для совместимых механизмов; исторические gates этого контекста не блокируют подготовку новой модели. Компонентная карточка не переписана: новых фактов runtime или реализованных изменений контракта нет.
