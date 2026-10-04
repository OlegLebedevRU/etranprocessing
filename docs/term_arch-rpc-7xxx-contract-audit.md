# RPC 7xxx: аудит MenuBuilder → IoT → l4con

Дата: 2026-10-04. Статус: исследование и предложения, без изменения протокола,
кода, установленного клиента или production. Evidence: статический код,
изолированная проверка DTO и последующая read-only сверка production-БД.
Runtime-журналы конкретной отмены не исследованы.

## Intake и источники

- Producer: MenuBuilder UI; IoT diagnostics владеет task/session orchestration;
  l4con владеет исполнением и завершением дерева процессов.
- Локальный etranprocessing: `92f93402c6fd183938060f125d4ee5c5acd916a9`.
- Внешний IoT checkout: `D:/work/iot.leo4.ru/iot-rpc-rest-app`,
  HEAD `987c5badbd810b55642be6c21870f5c9f0027640`. Имеющиеся сторонние изменения
  четырёх JSON-схем не затрагивались. Соответствие production этому HEAD неизвестно.
- Канон: внешний `docs/mqtt-rpc-protocol.md`, `method-codes-reference.md`,
  `remote-diagnostics-protocol.md`; требование пользователя — обязательный
  массив `payload.dt`, допускающий пустой список и однотипные параметры.
- Инварианты: tenant/owner/lease, точный SN/topic, отсутствие retain у команд,
  отдельные task UUID и session UUID, bounded execution, запрет отмены чужой сессии.
- Проверка реализации в будущем: producer/consumer fixtures, x86/x64 native tests,
  контролируемый E2E с test-owned process. В этом аудите сеть/брокер не использовались.

Основные локальные источники:
[UI](../MenuBuilder/frontend/src/routes/devices/DeviceConsoleTab.tsx),
[URL WebSocket](../MenuBuilder/frontend/src/api/devices.ts),
[l4con consumer](../tools/l4con/src/mqtt_client.c),
[MQTT/JSON parser](../tools/l4con/src/mqtt_protocol.c),
[executor](../tools/l4con/src/command_runner.c),
[существующая спецификация](ops_run-remote-console-diagnostics.md).
В IoT прочитаны `api/internal_v1/diagnostics.py`, `core/diagnostics/*`,
`core/services/device_tasks.py`, `device_task_processing.py`,
`core/schemas/device_tasks.py`, `core/adapters/agent_contract_v1.py`,
`core/logging_config.py`.

## Фактический путь и матрица

UI открывает same-origin `/api/internal/v1/diagnostics/ws/devices/{SN}`;
это не вызов `iot_client.py` на каждую команду. IoT проверяет tenant/console lease,
валидирует browser DTO и создаёт обычную device task через diagnostics sender.
Ingress/gateway wiring в этом аудите не проверено.

| Направление | Канал | Формат | Доставка / корреляция | Срок / результат |
|---|---|---|---|---|
| UI → IoT | WebSocket | `type=exec/cancel/start_log/stop_log` | session UUID; browser DTO | ACK исполнения отдельно не гарантирован |
| IoT → l4con | `srv/{SN}/tsk` | TaskNotify: `id`, `created_at`, `header` | task UUID + MQTT method/correlation properties | только анонс, не исполнение |
| l4con → IoT | `dev/{SN}/req` | body correlation aliases | текущий клиент QoS 0, retain 0 | запрос тела |
| IoT → l4con | `srv/{SN}/rsp` | task response с `payload:{dt:[...]}` | MQTT properties + task body | task TTL в минутах; exec TTL в секундах |
| l4con → IoT | `dev/{SN}/out` | v1 stream envelope, session/seq/eof | QoS 1, retain 0; seq dedup у IoT | volatile output, отдельный контракт |
| l4con → IoT → l4con | `res` → `cmt` | result + correlation | res QoS 1, retain 0; текущий consumer не ведёт полноценный CMT lifecycle | ACK отмены сейчас раньше завершения процесса |

MQTT publisher IoT использует AMQP; фактический broker mapping/QoS не проверялся.
Exactly-once execution и надёжный replay результатов этим аудитом не подтверждены.

| Метод | Фактическое состояние |
|---|---|
| 7000 | IoT строит dt для start/stop logs. У l4con нет dedicated handler 7000; ESP32 live-log UI не доказывает поддержку Windows. |
| 7001 | IoT формирует dt с одним exec object; l4con извлекает ключи поиском по всему JSON. |
| 7002 | IoT формирует dt с одним cancel object; l4con отменяет любую текущую команду без сравнения session. |
| 7003 | Есть consumer l4con; browser DTO/WS handler этого маршрута ping не поддерживает, UI onopen ping не отправляет. |
| 7004 | Есть consumer ACK, но renewal deadline не происходит. UI продлевает server console lease через REST, это другой flow. |
| 7005 | Описан локальным документом, dedicated handler l4con не найден. |
| 7006–7099 / прочие 7xxx | Поддержка не установлена. IoT diagnostics резервирует 7000–7099; нельзя считать весь 7000–7999 реализованным. |

## Почему 7002 может отсутствовать в логах

1. `log_rpc_debug()` IoT возвращается без записи для всех SN, кроме двух
   фиксированных SN в `RPC_DEBUG_SNS`. Терминал 773 туда не включён.
   События `rpc.tsk.publish` / `rpc.rsp.publish` поэтому не дают его method trace.
2. INFO `Created task` содержит response UUID/time, без method code.
   `DiagnosticService.cancel` не пишет отдельный structured audit.
   Module logger пишет отдельный rotating file и `propagate=False`;
   один docker stdout не является полным журналом RPC.
3. l4con пишет `[CANCEL] Cancellation requested...`, без числа 7002.
   Поиск исключительно по `7002` пропустит такую запись. Raw receive доступен
   только при verbose и может раскрывать command payload.
4. Завершившийся exec с EOF удаляется из forwarders/registry; UI сбрасывает
   activeSessionId. Disconnect без активной задачи не обязан создавать cancel.
   Unmount UI, напротив, может отправить случайный UUID при отсутствии session.
5. Отмена удаляет registry и останавливает WS forwarder сразу после dispatch,
   до terminal EOF; оператор может не увидеть подтверждение завершения.

Это установленные свойства локального кода, а не доказанный диагноз конкретного
production-инцидента. Для него нужны окно UTC, SN, task/session UUID и раздельные
gateway, task, RPC и terminal журналы. Проверять цепочку по UUID, не только по коду.

## Ошибки контракта и выполнения

- **Исполнение по tsk.** Consumer после отправки req не делает return.
  Затем читает вложенный `header.method_code` тем же поиском и для 7002
  немедленно отменяет активный процесс, хотя session payload ещё не получен.
  Следующий rsp может вызвать вторую отмену. Между ними могла начаться другая задача.
- **Чужая session.** Cancel извлекает session_id, но не сравнивает с current_cmd.
  В сочетании с unmount/random UUID и поздними задачами это риск остановки новой команды.
- **Ложное подтверждение.** Cancel res `cancelled/130` публикуется после установки
  флага, до worker cleanup. Сам exec worker публикует `status=completed` даже с
  exit 130/124. Нужны отдельные cancel accepted и terminal applied состояния.
- **Нет структурного парсинга.** `find_key` использует strstr, не проверяет
  путь/глубину JSON, dt type/cardinality/homogeneity или duplicate keys.
  Fallback method по цифрам/словам и dispatch любого `/rsp` как exec создают
  неоднозначности; неизвестная команда не должна запускать shell.
- **Свойства MQTT игнорируются.** Parser пропускает properties целиком. Работает
  за счёт дублирования metadata в task body; pure canonical MQTT metadata
  без такого дублирования не поддерживается. На выходе req/res также используют
  body aliases; event publisher уже умеет properties, но RPC path их не использует.
- **DTO не привязан к методу.** `DiagnosticRpcPayload.dt` — union stream/exec/cancel,
  `method_code` — произвольный int. Можно валидировать method7002 + exec object.
  `DiagExecPayload extra=allow` дополнительно ослабляет схему.
- **7004 без эффекта.** Executor использует фиксированный start+ttl; consumer
  keepalive его не меняет. Server lease и exec hard timeout следует разграничить.
- **Active Silencing не реализован в прочитанном bridge.** Unknown session output
  просто отбрасывается с DEBUG; заявленная документом отправка 7002 не найдена.
- **Документы конкурируют.** Локальный runbook показывает flat rsp и 7003–7005
  как доступный flow. IoT diagnostics DTO и builders используют dt и 7000–7002.
  Отдельный AgentContractV1 имеет flat exec/cancel DTO; его outbound exec/cancel
  usage найден только в tests, не в активном diagnostics sender.
- **Несколько exec не образуют очередь.** При следующем7001 consumer отменяет
  current_cmd, ждёт hWorkerThread через INFINITE и затем запускает новый worker.
  MQTT receiver во время join заблокирован. Single-instance l4con не защищает
  параллельные child processes l4pin; у l4pin межпроцессный enrollment mutex
  не найден. Для7011 требуется отдельная сериализация без cancel-and-replace.

## Предлагаемый канон

Различать REST task envelope, MQTT metadata, payload и browser DTO.
На REST уровне команда имеет `method_code` и `payload`; на MQTT wire payload
может быть самим `{dt:[...]}` согласно канону. Текущий full TaskResponse wire
нужно учитывать как versioned compatibility envelope, не ломать незаметно.

```json
{
  "method_code": 7002,
  "payload": {
    "dt": [{
      "session_id": "11111111-1111-4111-8111-111111111111",
      "reason": "operator_cancel"
    }]
  }
}
```

Это пример REST/task-level shape. Task correlation UUID передаётся отдельно
от target session UUID. На MQTT `method_code` — User Property,
correlation — native MQTT5 property / совместимый `correlationData` User Property
по документированному правилу. Конфликт метаданных должен отклоняться.

Общий envelope требует JSON object с обязательным dt array. Тип элемента задаёт
метод: простые или сложные однотипные значения; нельзя глобально ограничивать dt
объектами или молча выбирать dt[0]. Пустой список допустим структурно, но семантика
зависит от метода. Для 7001/7002 предлагается ровно один объект; `dt:[]` даёт
validation error без действия. Для 7003 без параметров можно закрепить `dt:[]`.
Массив нескольких exec нельзя включать до определения serial/parallel, per-item
result, cancel scope и resource budget. В текущем l4con один CommandContext.

Wrapper dt полезен для распознавания контракта и ограниченного parser loop;
это не криптографическая защита и не замена authorization/lease.
`out` и `res` не переводить автоматически в dt: канон запроса не определяет
схему результата/потока, они уже имеют собственные контрактные примеры.

## План перехода и проверки

1. Закрепить registry метода: typed item, cardinality, empty semantics,
   response/status, TTL единицы, поддерживаемые версии/агенты. 7003–7005
   маркировать как неподтверждённые до решения по их реализации.
2. IoT: единый method-aware validator для diagnostics и REST task creation,
   без глобального изменения чужих method ranges; убрать конкурирующие schemas.
   INFO audit без raw command: method/task/session/SN, origin, dispatch/result stage.
3. l4con: structured bounded parser и MQTT metadata; exact topic routing;
   tsk только req+return; action только после rsp validation; unknown method fail closed.
   Cancel проверяет session/exec identity, duplicates, stale task и ожидает cleanup.
4. IoT/UI: cancellation state retained до applied result/timeout; disconnect cleanup
   не зависит от успешной отправки последнего browser frame. Без random target UUID.
   Определить отдельно server lease, terminal lease и immutable exec hard timeout.
5. Совместимость: новая consumer версия сначала принимает явно различимые canonical
   и известные legacy shapes; telemetry legacy. Затем producer emits canonical;
   strict-only после инвентаризации клиентов. Numeric/keyword guessing не сохранять.
6. Проверки: malformed/missing/non-array/mixed/duplicate/nested dt; empty/one/many;
   tsk без исполнения; duplicate/rsp retry; чужая/старая cancel после новой exec;
   cancel idle/running/completed, WS abrupt close и revoke; EOF/res race, lost out,
   reconnect/offline, Unicode4096 и payload bounds; 7004 эффект или explicit unsupported.
   Потом x86/x64 builds и контролируемый E2E UI→task→tsk/req/rsp→cleanup→res/cmt.

## Выполненная проверка и ограничения

Изолированно загружены только реальные `commands.py` и `schemas.py`, без app config,
DB/Redis/сети. Проверка текущим Python/Pydantic: browser cancel UUID корректно
нормализуется в UUID; `method7002 + dt:[]` принят; `method7002 + exec item` принят
как DiagExecPayload. Это подтверждает отсутствие method-aware validation.
Первый обычный import не прошёл из-за отсутствующих обязательных app settings;
секретный env не загружался, затем выполнен isolated schema probe, exit0.

Не выполнялись production log audit, MQTT E2E, тесты backend/frontend, native build,
изменения клиента, deploy или broker operations. Архитектурный audit не доказывает
конкретную историю доставки 7002 и не подтверждает поддержку схем на других устройствах.

## Уточнение: classic «Управление устройствами → Команды» и production-БД

По уточнению пользователя «логи» означали также историю таблицы tasks в classic UI.
Причины отсутствия числа в текстовом журнале выше не объясняют сами по себе
отсутствие task в этой истории. Выполнен отдельный read-only audit 2026-10-04
17:18–17:20 UTC через штатный production SSH, `app1` и существующий DB helper.
Транзакции `SET TRANSACTION READ ONLY`, statement_timeout8s, rollback.
Секреты/env и raw command text не выводились; токены и новые задачи не создавались.

### Сравнение двух указанных задач

| Поле | Терминал773 | Device4624 |
|---|---|---|
| task UUID | b5b6b450-d5c6-4c8c-a464-7ff32b721b4a | df6be987-cc9d-405d-ba07-354209650cb7 |
| created_at UTC | 2026-10-04 17:11:41 | 2026-10-04 17:14:29 |
| method_code в БД | 7001 | 50 |
| task status / is_deleted | 3 / false | 3 / false |
| payload | dt, один exec object | dt, один object с ns |
| result.status_code в БД | **501** | **404** |
| JSON результата | status=completed, exit_code=0, status_code="200" | status=ERROR, method_code=50, method=GET_NVS_RECORD |

Task status3 не доказывает успешное прикладное действие: обе задачи имеют DONE,
но у device4624 прикладной ответ404. Значение/содержимое NVS не выводилось.

### История7002 существует у обоих устройств

Не удалённых задач7002: **773 —100**, последняя2026-09-14 05:33:51 UTC;
**4624 —3**, последняя2026-08-12 19:04:54 UTC. Последняя773:
`812a1d05-a451-4eed-8a83-778252cd2854`, payload.dt с reason/session_id.
**Последние25 не удалённых задач773 — только7001**, интервал
2026-10-01 11:27:57 → 2026-10-04 17:11:41 UTC. Это объясняет текущую страницу.

В production лимит поиска1000. В этом окне773:
7001=220,7002=100,7000=34,51=1. Запрос classic списка
`TasksRepository.get_tasks` фильтрует device/org/is_deleted и сортирует created_at;
условия, исключающего7002, нет. Runtime HTTP с browser tenant и пагинацией не
выполнялся; БД и код показывают наличие истории, а не факт её показа каждому пользователю.
У4624 дополнительно обнаружены18 задач7010; их схема и поддержка ещё не исследованы.

### «Метод #undefined» — несовпадение list и detail DTO

Production `DeviceTasksService.get` для обеих задач возвращает root keys
`id,created_at,header,status,pending_at,locked_at,results`.
Метод находится в `header.method_code`, верхнего `method_code` нет.
UI `DeviceTasksTab.tsx` читает `selectedTaskDetail.method_code`; API helper
`getTaskDetail` возвращает ответ без нормализации и ошибочно типизирует как flat TaskItem.
Это не отсутствие7001 в БД или только недостающая подпись справочника.
Аналогично теряются header.ext_task_id/priority/ttl в деталях.
В списке TaskListOut flat, поэтому строка списка и модальное окно расходятся.

Detail API также не возвращает сохранённый payload: get собирает header/results,
не присоединяя DevTaskPayload. Поэтому UI-блок параметров не может показать dt
этих задач, хотя dt есть в БД. При исправлении учитывать авторизацию и чувствительные
аргументы команд; не добавлять raw command в общедоступный audit.

### RES properties: подтверждённый дефект l4con

В записи773 JSON содержит status_code="200", но result.status_code=501.
IoT `save` берёт код из MQTT/AMQP headers и при отсутствии использует501;
он не подменяет его JSON-полем. l4con execution/cancel/ping/keepalive response
публикуется обычным send_publish_packet без properties, хотя event path уже
использует send_publish_packet_with_properties.
Старая задача50 имеет сохранённый404 и JSON без status_code — у неё нет
того же противоречия body/header. Raw packet capture не выполнялся;
БД и соответствующие producer/consumer paths согласованно показывают дефект.

### Дополнение к приоритетам реализации

1. Нормализовать detail response на boundary, отдельно типизировать list/detail;
   проверить одинаковый method/header для7001,7002,50 и неизвестных методов.
2. Определить authorized detail payload response, чтобы оператор видел реальный dt.
3. Передавать RPC RES status_code/correlation/result_uid в properties; header остаётся
   авторитетным, JSON не должен его противоречиво дублировать. Проверить200/404,
   exit0/124/130 и duplicate result→CMT.
4. Установить, почему после14сентября нет новых7002 у773: контролируемая отмена
   долгой test-owned команды с проверкой создания задачи, доставки и cleanup.
   История успешных быстрых7001 сама по себе не требует7002 после каждой команды.

SHA256 трёх production modules (`core.diagnostics.service`, schemas,
`api.internal_v1.diagnostics`) совпали с прочитанными локальными файлами.
Это подтверждает эти модули, не весь image/revision или установленный terminal binary.
