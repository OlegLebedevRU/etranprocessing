# Handoff: исследование RPC 7xxx

- Scope: MenuBuilder UI → IoT diagnostics → l4con; исследование без внедрения.
- Revisions: etranprocessing92f9340, внешний IoT987c5bad; production revision неизвестна.
- Результат: [аудит и план](../../../docs/term_arch-rpc-7xxx-contract-audit.md).
- Владельцы: IoT task/session/schema, MenuBuilder browser lifecycle, l4con consumer/Job cleanup.
- Инварианты не изменены. No deploy, credentials, broker sessions или test processes.
- Проверено: static producer/consumer, DTO isolated probe (exit0), dt method mismatch.
- Не проверено: production журналы и E2E конкретной отмены; ingress mapping; другие агенты.
- Главные риски: cancel по tsk без тела, отсутствие target session comparison,
  ACK до cleanup, global key/keyword parsing, фиктивный 7004 renewal.
- Следующий шаг: согласовать методные схемы/cardinality и безопасную миграцию;
  перед изменением MQTT-клиента выполнить обязательное уточнение типа клиента.
- Карточка remote-console требует актуализации при согласовании/внедрении контракта.

## Follow-up: classic tasks, 17:18–17:20 UTC

- Read-only production DB audit via SSH/app1, SET TRANSACTION READ ONLY,
  timeout8s/rollback; без raw command, env, новых токенов/задач или файлов в контейнере.
- У773100 старых7002 (последняя09-14), у4624 три (последняя08-12).
  Последние25 задач773 все7001; фильтр списка не исключает7002.
- Production detail DTO возвращает header.method_code; UI читает flat field.
  Обе заданные задачи подтверждают причину «Метод #undefined».
- Detail DTO не возвращает payload, хотя dt есть в DB.
- Задача773 b5b6b450...: result DB501 vs body200/exit0. RES properties отсутствуют
  в l4con producer; IoT сохраняет header status с default501.
- Задача4624 df6be987...: method50/GET_NVS_RECORD, result404/ERROR; DONE не успех действия.
- Production hashes трёх diagnostics modules совпали с local; это не полный image audit.
- Ещё не проверено: HTTP browser tenant/pagination, controlled cancel E2E, схема7010.
