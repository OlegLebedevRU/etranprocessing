# Смена идентичности устройства — source handoff, 2026-11-16

## Intake и владельцы
Сквозная реализация MB UI/API → PB check/setup → IoT dedicated transfer.
PB владеет issuance, cert_serial и Alembic036; shared содержит только ORM.
IoT владеет binding, control teardown и миграцией0014_task_tenant_history.
Изменения исходников, без коммита, публикации и деплоя.

## Согласованный контракт
- Только суперюзер; Classic → Classic; неизменные SN/device_id.
- Подготовка A→B оставляет устройство в A до успешной выдачи PB setup.
- Прежние pending setup-PIN аннулируются. Любой новый setup-PIN использует B.
  Истечение/потеря PIN не отменяет intent; есть явная отмена до issuance.
- Renew отдельный; renewal-PIN не становится transfer-PIN.
- Issuance сохраняет ответ/CSR для same-CSR recovery и переключает PB.
  IoT подтверждается отдельным идемпотентным HTTP API; сбой сохраняет sync_pending
  и событие в той же транзакции issuance. Продолжение — явный retry SU.
- Не ждём первого mTLS/MQTT подключения, не опрашиваем устройства.
- Оплаченный срок License сохраняется; баланс/индивидуальная цена обнуляются,
  прежнее меню отвязывается. Старая финансовая история остаётся A.
- Для composite FK техническая история сессий/финансов привязана к архивному
  техническому терминалу A; денежные поля и org не изменяются.
- Balance expression UNIQUE INDEX включает org, сохраняя COALESCE версии из022.
- IoT отзывает lease и останавливает console/video до смены org. Новые операции
  захватывают тот же binding lock. Block/collision flags не сбрасываются.
- Task.org_id фиксируется при создании. На передаче legacy NULL фиксируются
  только для данного устройства в A; незавершённые старые задачи истекают.
  A видит свою историю payload/results/events; B видит только свою.
  Запоздалые результаты старых задач отклоняются. DevEvent.org_id не меняется.
- PB создаёт дедуплицированное событие старого сертификата только после отказа
  auth. Успешный auth не имеет дополнительных запросов. Failure cache ограничен.
- Раздел «События устройств»: new/in_progress/closed, actor/comment, фильтры,
  переход к терминалу, PIN, детали операции/retry. Строгий SU на сервере.
- Бейдж на «Администрирование» и разделе событий: один общий browser poll60s,
  пауза в скрытой вкладке, просмотр не помечает событие обработанным.

## Исходники IoT
Рабочая копия: `.worktrees/identity-iot`, база
`879a9e654fbad9269cc9ee03443e2d7926ba437b`.
Оригинальный `D:/work/iot.leo4.ru/iot-rpc-rest-app` не изменён.
IoT diff и новые файлы остаются в указанной рабочей копии для совместного выпуска.
Не считать это deployed revision. Изменений MQTT-клиента/топиков нет.

## Проверки
- PB полный pytest: 411 passed, 3 skipped.
- MB полный pytest: 1235 passed,25 skipped,2 падения ожиданий списка ревизий;
  ожидания исправлены под036, повтор всего test_schema_compatibility:15 passed.
- Shared полный pytest:65 passed; Pyright:0 errors.
- PB и MB: Ruff check/format, Pyright app — успешно.
- IoT полный pytest:1225 passed,13 skipped; после Black targeted:7 passed.
  Ruff E4/E7/E9/F изменённого production-кода — успешно.
- Frontend build успешно; unit161 passed; Playwright3 passed на5297.
- Реальный SQLite DDL и FK: archive/history ownership, migration036 upgrade/
  downgrade fences, balance expression index, active-operation uniqueness.
- История IoT проверена настоящими SQL select/update на SQLite с PG-совместимыми
  тестовыми компиляторами. HTTP и teardown проверены через test doubles.
- Не выполнены: real PostgreSQL migration integration, broker/physical device E2E,
  production rollout. Локальный Docker daemon недоступен; production не использован.
- Пользовательский certificate_renewal.py сохранён: исходный diff37 вставок.

## Порядок выпуска
1. IoT migration0014 и совместимый app1 с dedicated transfer API.
2. PB Alembic036; совместимый shared package и оба backend.
3. Настроить PB identity_change_iot_url/key из окружения; IoT service key должен
   совпадать. Для MB→PB retry требуется штатный internal service key.
4. UI; smoke на тестовом устройстве: pending/check/setup, потерянный PIN,
   IoT timeout/retry, старый cert, история A/B, повторная передача.
Downgrade после передачи блокируется, если теряется владение историей/деньгами;
использовать roll forward. Секреты/окружение production не изменялись.

## Доступ
Ранее запись обычным sandbox identity давала Access denied из-за NTFS ACL.
Точечная elevated запись исходников разрешена и использована. ACL не менялись.
Отклонённая auto-review попытка общего ACL grant заменена точечными изменениями;
блокировок текущей реализации на этом основании нет.
