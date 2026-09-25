# L4D-17C-VIDEO-WATCH-IOT-01 — read-only наблюдение за видеосессией до приёмки IoT

## Метаданные

- `prompt_id`: `L4D-17C-VIDEO-WATCH-IOT-01`
- `registration_id`: `R-L4D-17C-VIDEO-WATCH-IOT-01-v1`
- `scope_project`: `iot-rpc-rest-app`
- `scope_root`: `D:\work\iot.leo4.ru\iot-rpc-rest-app`
- `blocked_prompt_id`: `L4D-17C-IOT` (приёмка ещё не начата)
- `required_handoff_ids`: [`H-L4D-07-IOT-STOP-v1`]
- `sequence_gate_handoff_id`: `H-L4D-16-MB-v1`
- `output_handoff_id`: `H-L4D-17C-VIDEO-WATCH-IOT-01-v1`
- `next_prompt_id`: `L4D-17C-IOT`
- `report_path`: `docs/l4desk/handoffs/L4D-17C-VIDEO-WATCH-IOT-01-report.md`
- `candidate_path`: `docs/l4desk/handoffs/L4D-17C-VIDEO-WATCH-IOT-01-candidate.md`
- `candidate_format`: `DETACHED_V1`

## Основание и границы

По явному поручению пользователя закрыть масштабирование browser status polling до
приёмки `L4D-17C-IOT`. Сейчас операторский экран делает три status GET каждые 5 секунд;
два из них вызывают IoT status. Существующий IoT control WebSocket привязан к lease,
допускает только одно соединение на lease и открывается браузером лишь для remote input.
Он не является read-only подпиской на состояние video для всех разрешённых наблюдателей.

Изменять только `iot-rpc-rest-app`: добавить внутренний read-only event transport
для MenuBuilder. Нельзя менять MQTT topics, команды агента, правила lease/session lock,
долговечное хранение stop intent или внешние terminal wire payload. Миграция БД не
предполагается. Работа MenuBuilder регистрируется отдельно после приёмки provider.

Текущий deployment работает с **одним `app1` worker**. Доставка наблюдателям между
несколькими экземплярами/воркерами не входит в этот corrective и остаётся задачей
после каскада. Нельзя объявлять подписки в памяти межпроцессно надёжными. В отчёте
и runbook нужно явно зафиксировать `WEB_CONCURRENCY=1` как условие данного контракта.

## Аддитивный provider-контракт

- Internal WebSocket: `/api/internal/v1/remote-input/ws/watch/{sn}`.
- Доступ только по существующей internal-service авторизации и проверке `org_id` ↔ `sn`.
  Отсутствующая/неверная identity закрывает соединение без выдачи фактов.
- Канал **только для чтения**: входящие команды управления, keepalive и release через
  него не принимаются. Он не создаёт и не продлевает lease.
- При подключении подписаться на локальные presence и stream event queues, затем
  отправить актуальный snapshot. Последующие события служат сигналом invalidation;
  consumer сверяет текущий snapshot, не трактует transient event как долговечный факт.
- Для video нужны как минимум online/offline, stream state/reason, `stream_instance_id`
  и время события. При разрыве соединения consumer заново читает snapshot и не
  восстанавливает `running` из старого события. События из старой stream epoch
  не могут менять состояние новой.
- Входящий control WS и REST status сохраняют прежнюю совместимость. Нельзя выдавать
  browser JWT или внутренний service key в event payload и логи.
- Медиакачество и декодированные кадры не являются фактом IoT; этот контракт их
  не объявляет. Lease keepalive остаётся отдельным механизмом безопасности.

## Проверки и передача в 17C

Сначала воспроизвести отсутствие безопасного read-only observer. Затем проверить
авторизацию/tenant isolation, начальный snapshot, событие stream stop/restart,
устаревший `stream_instance_id`, disconnect/cleanup, медленного подписчика,
отсутствие lease mutations и регрессию существующего control WS. Провести полный
локальный suite, lint/type/format, опубликовать код, отчёт и DETACHED_V1 candidate.
После контролируемого deploy только `app1` проверить внутренний watch smoke и
фактический один worker. 17C начинает приёмку с точного provider commit и использует
новый handoff как дополнительный вход; существующие критерии 17C не ослабляются.
