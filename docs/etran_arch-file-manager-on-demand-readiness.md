# Файловый менеджер по требованию

## Решение и статус

Целевой контракт от 2026-11-12: неиспользуемый файловый менеджер не создаёт
периодических HTTPS-запросов или записей в PostgreSQL. Готовность проверяется
при явном открытии терминала оператором; активность подтверждается существующим
ACK продления файлового сеанса. Отдельный heartbeat не нужен даже в сеансе.

Серверы и UI приняты в etranprocessing PR158 / IoT PR171 и развёрнуты.
Native L4Con1.12.5 выпущен в подписанном пакете1.13.66 отдельного репозитория
`OlegLebedevRU/l4tools` и установлен только на подтверждённом пилоте773.
Проверены реальные probe/start/renew/close и600 с fleet UI без FM-запросов.
Idle-наблюдение завершено:1863 с без FM child и1801 с без изменений FM rows;
пользователь также сообщил об установке66 на119 и55. Подробности — в
[пакете реализации](../.agent-context/tasks/completed/2026-11-12-fm-on-demand-release.md).
Старые клиенты сохраняют hello15 с до собственного обновления; массовый native
rollout этой задачей не выполняется. PB mitigation сохранён:
[handoff](../.agent-context/tasks/active/2026-11-12-fm-db-isolation.md).
Платежи, сертификатная выдача и существующий MQTT presence не меняются.

## Что устраняем

- Постоянный agent hello независимо от наличия пользователя FM.
- Обязательную HTTP-регистрацию агента при каждом запуске: метаданные поступают
  с ответом на probe, а не отдельным стартовым вызовом.
- Обход readiness всего парка каждые15 с из FilesPage. Fleet list получает
  имеющиеся inventory/presence, без fan-out, пробуждения или FM lease.
- Отдельный таймер active hello: свежесть обновляется в транзакции уже имеющегося
  подтверждённого agent ACK start/renew.

## Состояния и инициаторы

| Состояние | Допустимая работа FM | Переход |
|---|---|---|
| idle / unchecked | Никаких FM таймеров, HTTPS и DB-записей | Явный выбор терминала → checking |
| checking | Один ограниченный probe выбранного терминала | Валидный ответ → verified; timeout/deny → unavailable |
| verified | Короткоживущее разрешение попытаться открыть сеанс | Consume proof + обычная IoT admission → starting |
| starting / active | Существующие start/renew/transfer/status, ACK и lease deadlines | Close/expiry/error → stopping |
| stopping | Существующий stop/drain до ACK или deadline+5 с | idle; фоновые FM таймеры прекращены |

Unchecked означает отсутствие свежей проверки, а не доказанный offline.
Исторические version/capabilities/last_seen — справочная информация, не ready.
MQTT svc_online также не является FM readiness и не получает новые payload.

## Владельцы и поток

1. UI по клику создаёт request UUID; MB проверяет JWT, tenant/device policy,
   browser-view UUID и роль. Mount страницы, фокус окна и обновление fleet list
   не создают probe. Одновременные клики одной операции используют тот же UUID.
2. MB создаёт в PB pending probe с доверенным actor context, terminal/tenant,
   текущим serial, expires_at=server_now+10 с. Хранилище общее между PB workers;
   максимум один текущий probe на terminal. Другому owner — busy, без замены.
3. После создания pending MB вызывает авторизованный IoT dispatcher.
   IoT публикует probe в существующий srv/{SN}/fmc. Новый топик, RPC-код,
   MQTT-клиент или retained command не вводятся. Probe не захватывает общую lease
   и не запускает файловую операцию, не читает пользовательский каталог и не
   обращается к S3. Разрешён кратковременный изолированный metadata helper:
   его UUID сохраняется между probe и start, idle grace ограничен30 с. Сам MQTT
   CONNECT не создаёт helper; после stop/expiry/выхода нет idle respawn.
4. Агент проверяет срок, policy/Leo4Proxy и доступность подсистемы. Он посылает
   в PB один mTLS HTTPS-ответ с метаданными. Неизвестный/истёкший probe не даёт
   права зарегистрироваться или продлить сеанс.
5. PB проверяет pending UUID, terminal/tenant/current serial, срок и равенство
   UUID в header/body. Только здесь разрешается регистрация нового agent instance.
   PB атомарно сохраняет ответ и fm_agents; MQTT PUBACK не означает готовность.
   Если локально остались receipts, агент сообщает filesystem_ready=false:
   это регистрирует текущий экземпляр, но не создаёт доступный proof. Только
   после принятия такого ответа выполняется ограниченная сверка receipts через
   прежний endpoint. Новая передача запрещена до завершения восстановления;
   следующий probe запускается только новым явным действием пользователя.
6. MB ожидает результат максимум7 с; транспортный/доменные отказы различаются.
   Ожидание не удерживает соединение PostgreSQL. Если используется DB polling,
   он ограничен2 чтениями/с только для текущего запроса и немедленно прекращается.
7. Успешный proof действует15 с после ответа, доступен только своему actor/view
   и однократно потребляется при регистрации файловой сессии. PB повторно
   проверяет active/current certificate/instance. Сначала обычная IoT reservation,
   затем PB session registration с proof; при отказе — существующий revoke/drain.
   Проигрыш гонки за lease не разрешает повторное использование уже consumed proof.

Срок10 с ограничивает приём ответа;15 с — отдельное окно использования уже
полученного proof. Ответ после deadline или закрытия UI generation отбрасывается.
Потерянный ответ не вызывает автоматического повторного открытия сеанса.

## Additive wire контракт

Ниже — новые формы для реализации; текущие endpoints пока их не поддерживают.
FM protocol2 и существующие команды/результаты сохраняются. Новый capability
`fs.readiness_on_demand` и явно сохранённый режим `on_demand_v1` отделяют новый
жизненный цикл от legacy heartbeat; отсутствие capability не означает поддержку.

| Направление | Path / сообщение | Подтверждение и ограничения |
|---|---|---|
| MB → PB | POST /api/internal/file-manager/v1/devices/{device_id}/readiness-probes, body request_id | Сервисная auth + actor/view; duplicate своего UUID возвращает тот же probe, чужой owner скрыт/busy |
| MB → IoT | Аддитивный service-auth dispatch readiness probe | Producer проверяет trusted tenant/SN/request_id; URL маршрута фиксируется после чтения app1 |
| IoT → agent | srv/{SN}/fmc, QoS1, retain=false: v=2, action=probe, command_id=request_id, expires_at | Только probe не требует lease_id; для list/stop прежние lease checks обязательны |
| agent → PB | POST /api/file-manager/v1/agent/readiness-probes/{request_id}/result | mTLS/current serial, X-FM-Agent-Instance-Id; request_id, agent_instance_id, agent_version, protocol_version, capabilities, filesystem_ready, readiness_mode=on_demand_v1 |
| MB → PB | GET /api/internal/file-manager/v1/readiness-probes/{request_id} | Owner scoped, bounded wait; pending/ready/unavailable/expired, без выдачи чужого proof |
| MB → PB | Существующая session registration + readiness_probe_id | Single-use consume, точный actor/tenant/SN/serial/instance; повтор того же session ID идемпотентен |
| agent → PB | Существующий agent ACK active для start/renew | Обновляет last_seen только после проверки ticket/grant/lease/instance/current certificate, в том же commit |

Ответ probe не содержит storage URL, file bytes или произвольных путей. Тело
ограничено существующими лимитами AgentHello, неизвестные поля запрещены.
UUID не является авторизацией. Internal actor context не передаётся агенту.
Отказ агента оформляется ограниченным error_code, без содержимого локальных файлов.
Для wire fixture зафиксировать две строгие формы результата: success с полным
набором метаданных и failure с request_id/agent_instance_id/error_code. Error code
ограничен64 символами `[a-z0-9_]`; failure не создаёт ready/proof. Не добавлять
его как произвольное extra поле к существующему AgentHello.

Дубликат MQTT command не вызывает повторной проверки/регистрации: агент хранит
ограниченный TTL-кэш результата и может повторно доставить тот же ответ до
deadline. PB first-valid-response-wins; точный повтор идемпотентен, изменённый
payload/instance того же request_id отклоняется. После restart можно безопасно
повторить локальную проверку для ещё pending команды; PB не принимает второй
результат или смену instance для уже completed/consumed probe.

## Свежесть активного сеанса и безопасность

- Readiness и общая IoT lease остаются разными сущностями. Probe не даёт права
  файловых операций и не обходит конфликт с console/video/input.
- ACK start/renew продлевает freshness45 с только при принятом действующем ticket;
  сам факт HTTP-запроса, GET status или upload не продлевает её.
- Native получает локальный deadline исключительно из действующего ticket/lease.
  Окончание UI renew, потеря actor/view или cert/instance fence сохраняют stop/drain.
- В idle истёкшая запись агента не инициирует проверку, reconnect loop или запись.
  GET readiness нового режима сообщает unchecked, без side effects.
- Текущая mTLS/SN/serial/activity проверка не кэшируется и не заменяется presence.
  Старый hello не может молча понизить подтверждённый on-demand режим до legacy.
- Pending/proof имеют ограниченный срок и bounded storage: до одной текущей
  записи на terminal, атомарная замена только истёкшей/закрытой записи. Отдельный
  периодический DB sweeper ради readiness не вводится.
- Возврат на fleet/home, закрытие вкладки, logout/смена tenant и route unmount
  отменяют probe wait и очищают timers/listeners. Применяется прежний confirmed
  close для уже начавшегося lease, без раннего освобождения до stop ACK/deadline.

## Совместимый выпуск

1. PB mitigation уже выпущен; legacy agents по-прежнему работают.
2. Сначала выпустить MB bridge, принимающий schema034 и035. Затем добавить PB
   schema/API для probe и режима, затем совместимый IoT dispatcher.
   Общие модели — shared, миграции — только PB; чтение других consumers совместимо.
3. Выпустить MB/UI: никаких fleet-wide readiness polling; legacy выбранного
   терминала можно проверить единожды через старый endpoint. Включение нового
   flow — явным server capability/rollout policy, без предположения по версии.
4. После серверных consumers выпустить native: новый агент не посылает hello
   при старте/в idle; отвечает probe и использует существующий active ACK.
   Старый сервер не запускает таймерный fallback: несовместимость видна оператору.
5. На обновлённом пилоте проверить idle→probe→start→renew→close и fault cases.
   Затем раскатывать fleet; legacy background traffic исчезнет по мере обновления.
6. Удалять legacy hello только после инвентаризации оставшихся отправителей и
   отдельного cutover. Временный legacy compatibility не является целевой нормой.

## Приёмка

- Обновлённый idle terminal:0 FM HTTPS и0 readiness DB writes за30 мин; MQTT
  presence и другие службы продолжают работать. Agent restart не создаёт hello.
- Открытая fleet page без выбранного терминала:0 readiness fan-out/probe за10 мин.
- Один клик: один bounded probe только выбранного SN; no file operation/no lease до
  готовности. Затем обычный start и доказанный terminal ACK.
- Active: renew ACK каждые15 с, freshness45 с;0 дополнительных hello и0 новой
  периодической очереди поверх существующего renew/status flow.
- После confirmed close/expiry: FM traffic возвращается к0 за границей stop/drain;
  нет скрытого reconnect, повторного acquire или автооткрытия после503.
- Проверить duplicate/late/reordered replies, два owner/view, timeout/dispatch
  failure, malformed payload, stale/changed cert и UUID, agent/PB/app1 restart,
  потерю прав, остановку UI, потерянный ACK и lease conflict.
- PB payment pool saturation isolation и основные regressions остаются зелёными.
  Сравнить суточные FM requests/discovery writes и latency до/после на пилоте.

## Задание

[Точный пакет следующему агенту](../.agent-context/tasks/active/2026-11-12-fm-on-demand-agent-task.md).
Producer app1 и consumer L4Con сверены в пакете реализации. IoT dispatch route:
`POST /api/internal/v1/file-manager/devices/{device_id}/readiness-probes`.
Machine schema/fixtures: `contracts/file-manager-readiness-v1/`. Проверки кода,
сборки, deployment и аппаратного пилота фиксируются отдельно в handoff.
