# Владение общей PostgreSQL-схемой

**Владелец документа:** архитектура backend-платформы
**Проверено:** 2026-10-04 (L4Desk/fin ledger — миграции 027/029; подписки L4Desk — логика MB, колонка `l4desk_terminals.paid_until`)
**Область:** `shared/etranprocessing_db`, `ProcessingBackend`, `MenuBuilder`

## Правила владения

- Владелец таблицы определяет её инварианты и единственный создаёт Alembic migration proposal. Физически все миграции остаются в `ProcessingBackend/backend/alembic`.
- Наличие ORM-модели в `etranprocessing_db` не даёт сервису права записи. Пакет остаётся только декларативным контрактом схемы.
- Запись не-владельца запрещена, кроме явно указанных ниже use-case/column scoped исключений.
- Новые междоменные записи реализуются через API/use-case владельца. Существующие прямые чтения допустимы как временные compatibility paths и не расширяются.
- Запрос всегда ограничивается tenant scope, когда таблица содержит `org_id` либо связана с организацией через terminal/menu/billing aggregate.
- **Подписки L4Desk** не имеют отдельной таблицы: владелец логики — **MB** (`app/services/subscriptions.py`, `subscription_payments.py`, `subscription_worker.py`, `subscription_notifications.py`, `app/routers/subscriptions.py`); состояние хранится в `l4desk_terminals` (включая `paid_until`). Миграцию `029_terminal_subscription` физически создаёт PB как sole Alembic owner.
- **Transport policy (leo4proxy)** — вычисляемая логика **PB** (`app/services/leo4proxy_policy.py`) поверх чтения `l4desk_terminals` / terminal context; отдельной таблицы политики нет.

## Матрица таблиц

Обозначения: `PB` — ProcessingBackend, `MB` — MenuBuilder.

| Таблица | Домен | Владелец | Разрешённая запись | Разрешённое чтение и ограничения |
|---|---|---|---|---|
| `api_tokens` | auth | MB | MB | MB; MCP использует отдельный прикладной контракт |
| `users` | auth | MB | MB (включая `last_org_id`) | MB |
| `user_sessions` | auth | MB | MB (включая `active_org_id`) | MB |
| `orgs` | organization | MB | MB | MB; PB читает статус при terminal auth |
| `org_billing_settings` | organization/billing | MB | MB | MB |
| `org_statuses` | organization | MB | MB | MB; PB может читать справочное состояние |
| `terminal_types` | terminal | MB | MB | MB и PB |
| `terminals` | terminal | MB | MB; PB — только certificate identity/discovery поля в legacy enrollment flow | MB; PB читает auth/license context |
| `licenses` | terminal/billing | MB | MB | MB; PB читает `expires_at` при terminal auth |
| `terminal_cert_history` | certificate audit | PB | PB | PB; MB — административное read-only представление |
| `terminal_cert_discovery` | certificate discovery | PB | PB | PB; MB — административное read-only представление |
| `menu_variants` | menu | MB | MB | MB; PB читает опубликованное terminal menu |
| `groups` | menu | MB | MB | MB; PB читает terminal menu |
| `services` | menu | MB | MB | MB; PB читает terminal menu/payment metadata |
| `menu_variant_snapshots` | menu delivery audit | PB | PB создаёт immutable snapshot фактически выданной версии | PB; MB read-only для аудита |
| `terminal_menu_bindings` | menu assignment/delivery | MB | MB меняет assignment; PB меняет только `loaded_version` и `loaded_at` после выдачи | MB и PB |
| `catalog_categories` | catalog | MB | MB | MB |
| `catalog_items` | catalog | MB | MB | MB |
| `billing_orders` | billing | MB | MB | MB |
| `billing_order_items` | billing | MB | MB | MB |
| `certificate_pins` | billing/certificate | MB | MB | MB; PIN operations доступны через выделенный прикладной API |
| `tsp` | payment reference | PB | PB/import pipeline | PB; MB read-only reporting/reference |
| `tsp_parameter_codes` | payment reference | PB | PB/import pipeline | PB; MB read-only reporting/reference |
| `payments` | payment ledger | PB | PB | PB; MB временно читает напрямую для reporting |
| `payment_params` | payment ledger | PB | PB | PB; MB временно читает напрямую для reporting |
| `balance_terminal_tsp` | balance ledger | PB | PB | PB; MB read-only monitoring/reporting |
| `gate_gauge_records` | telemetry | PB | PB | PB; MB получает оперативное состояние через event bus, DB — только read-only fallback |
| `tech_gate_records` | telemetry | PB | PB | PB; MB read-only monitoring/reporting |
| `l4desk_registrations` | l4desk | MB | MB | MB |
| `l4desk_tenant_profiles` | l4desk | MB | MB | MB |
| `l4desk_memberships` | l4desk | MB | MB | MB |
| `l4desk_terminals` | l4desk | MB | MB (включая `paid_until`, миграция 029) | MB; PB читает для certificate identity и вычисляемой transport policy (leo4proxy) |
| `l4desk_audit_events` | l4desk audit | MB | MB; PB пишет события `certificate_pin_issued` из certificates router | MB |
| `l4desk_remote_sessions` | l4desk | MB | MB | MB |
| `fin_tariff_versions` | fin ledger | MB | MB | MB |
| `fin_accounts` | fin ledger | MB | MB | MB |
| `fin_ledger_transactions` | fin ledger | MB | MB | MB |
| `fin_ledger_entries` | fin ledger | MB | MB | MB |
| `fin_billing_profiles` | fin ledger | MB | MB | MB |
| `fin_billing_cycles` | fin ledger | MB | MB | MB |
| `fin_usage_daily` | fin ledger | MB | MB | MB |
| `fin_terminal_monthly_charges` | fin ledger | MB | MB | MB |
| `fin_payments` | fin ledger | MB | MB | MB |
| `fin_manual_payments` | fin ledger | MB | MB | MB |
| `fin_balance_projections` | fin ledger | MB | MB | MB |
| `fin_notification_deliveries` | fin ledger | MB | MB | MB |
| `fin_reconciliation_runs` | fin ledger | MB | MB | MB |
| `fin_archive_batches` | fin ledger | MB | MB | MB |

## Совместимость shared schema

Изменения общей схемы выполняются только в порядке expand → migrate → contract:

1. **Expand:** добавить nullable/defaulted columns, новые tables/indexes или совместимые constraints. Старые версии обоих backend продолжают работать.
2. **Deploy readers/writers:** выпустить совместимые версии PB и MB; новый writer не прекращает заполнять старый контракт, пока старый reader не выведен.
3. **Migrate:** выполнить backfill отдельной наблюдаемой операцией; проверить количество строк, `NULL`, ограничения и rollback strategy.
4. **Switch:** переключить чтение на новый контракт и подтвердить метриками/логами обеих приложений.
5. **Contract:** удалять/переименовывать columns и ужесточать constraints только отдельной cleanup migration после подтверждения, что ни один развернутый consumer не использует старую схему.

## Обязательные проверки изменения схемы

- Обновить модели и Alembic migration в одном change set; не создавать migration chain в MenuBuilder.
- Запустить полный `pytest`, `ruff` и `pyright` в обоих backend при изменении `shared/`.
- Проверить upgrade с предыдущего production head и согласованный downgrade/roll-forward сценарий.
- Для writer transfer сначала добавить owner API и перевести consumer, затем удалить прямую запись.
- Любое новое исключение co-writing требует обновления этой матрицы с точными columns/use case и датой проверки.