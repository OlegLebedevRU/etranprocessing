---
name: mqtt-device-probe-testing
description: Контролируемый mqttx probe терминала 773 — passive observe, разрешённые contract messages, lease/recovery observation с изоляцией, ACL и безопасным cleanup.
---

# MQTT device-probe testing

## Перед запуском
1. Прочитай [terminal 773](../../../.agent-context/testing/mqtt-device-probe-terminal-773.md)
   и [topic matrix](../../../.agent-context/contracts/mqtt-topic-matrix.md).
   Для документационной/read-only задачи не подключайся без запроса на запуск.
2. Подтверди local/test или явно согласованный remote контур. Loopback broker может
   иметь bridge в production: сначала проверь изоляцию, ACL, маршруты и владельца сессии.
3. Подтверди binding SN `a4b0000773c82116d210826` ↔ terminal_id `773` у потребителей;
   не считай client_id доказательством identity. Не вытесняй реальное устройство.
4. Используй отдельный разрешённый client_id и read-only ACL для наблюдения;
   если ACL требует collision с действующим клиентом — остановись и согласуй стенд.
   Не публикуй presence/LWT от observer, не заменяй сохранённый статус устройства.
5. Не сохраняй credentials, ключи, пути к ним, сертификаты, broker profile/connection
   параметры в docs/source/logs. При настройке/изменении MQTT-клиента обязателен вопрос
   «Какой тип MQTT-клиента создаётся: main_app или extra_service?»; тип не угадывать.
6. Выбери flow: presence, inventory, stream start/stop, renew, stream_event, input,
   diagnostics. Только утверждённые topic/schema/QoS/retain/TTL для этой версии.

## Выполнение
1. **Passive observe первым:** bounded окно, только разрешённые точные topics этого SN;
   собрать command/event sequence и correlation, не менять состояние.
2. **Contract publisher:** только device→server разрешённые ACL сообщения тестовой identity.
   Команды server→device инициировать штатным авторизованным API, а не device credentials.
   Presence меняет retained state — только с согласованием sole owner и cleanup.
3. В evidence обязателен test correlation ID; в wire — реальные поля schema:
   ctl command_id/lease_id/timestamps/version, RPC correlationData/session_id.
   Не добавляй произвольные cmd_id/v/test_id в payload. Поля stream — когда применимы.
4. Negative tests (malformed JSON, unknown type/version, expired, duplicate, wrong lease)
   допускаются только как отдельные изолированные cases, не произвольные сообщения в live flow.
5. **Lease probe:** UI keepalive → app1 publish → terminal ACK → вырос local expiry →
   FFmpeg жив дольше исходного окна; stop keepalive → lease_expired без restart.
6. **Recovery probe:** mqttx только наблюдает. По отдельному разрешению завершить
   единственный test-owned FFmpeg PID при valid lease; ждать restarting → running/recovered.
   Не использовать массовый kill по имени; не имитировать recovery событием mqttx.
7. Cleanup: stop/release test lease штатным API, disconnect observer, проверить отсутствие
   test processes/timers и чужих изменений. Retained status восстанавливает его владелец;
   не очищать topics wildcard-публикациями.

## Результат
Заполни [probe result](../../../.agent-context/testing/mqtt-device-probe-result-template.md).
ACK/PUBACK/emulated event не доказывают весь E2E; укажи реально наблюдавшиеся слои.
Новые debug/test/cmd/ctrl topics, retained commands/ACK/NACK/diagnostic output,
обход lease и неограниченное shell/process execution запрещены.