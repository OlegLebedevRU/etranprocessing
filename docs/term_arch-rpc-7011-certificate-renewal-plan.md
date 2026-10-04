# План: канонизация RPC7xxx и установка сертификата через7011

2026-10-04: исторический согласованный план. Реализация на 2026-10-05 описана
в [актуальной матрице](term_arch-rpc7011-flow-matrix.md); ниже сохранены исходные
решения и этапы, это не доказательство публикации или E2E acceptance.
Основание: [аудит7xxx](term_arch-rpc-7xxx-contract-audit.md), read-only production
сверка tasks и текущее l4pin1.7.3. RPC7011 выбран пользователем.

## Подтверждённые решения

- PIN передаётся внутри RPC payload.dt; отдельного HTTPS получения PIN нет.
- Существующее действие «Создать PIN» сохраняет ручной/legacy flow.
- Новое действие (предложенное имя **«Заказать удалённое продление»**) синхронно
  создаёт PIN renew и ожидает прямой ответ IoT на постановку7011 в очередь.
  Оператор PIN не видит. Outbox/фоновый dispatcher для постановки не вводятся.
  При ошибке пользователь повторяет вручную; response loss означает неопределённый
  исход постановки, не доказанное отсутствие задачи. IoT самостоятельно назначает
  task_id; заранее создавать UUID задачи или вводить operation_id нельзя.
  Кнопка «Повторить» делает новую постановку7011 с тем же PIN renew, максимум
  три ручных повтора в открытой форме. Затем требуется выход из формы и уведомление
  «Повторите через 3 минуты». Это лимит формы, не автоматический цикл отправки.
  Возможные duplicate tasks допустимы; использованный PIN PB отбрасывает до
  нового выпуска/изменений сертификата, без побочных business effects.
  Это последовательные межсервисные действия, не атомарная DB-транзакция.
- Затем устройство асинхронно выполняет операцию и отчитывается; открытая форма
  и online terminal для постановки задачи не требуются.
- IoT/task protocol и результат платформонезависимы. Windows adapter использует
  l4pin; другие ОС должны заявить capability7011 и иметь собственный adapter.
- 7011 выполняется dedicated native handler, без7001, cmd/powershell и stdout stream.
- Подтверждено пользователем: отдельный **тип PIN renew**, соответствующий
  authenticated installation flow renewal и current live terminal certificate.
  Второй X.509 сертификат не требуется.
- Продление сохраняет SN. Первичная установка без доверенного рабочего
  сертификата остаётся прежним enrollment flow. Изменение SN не является7011 renewal.
- Никакого автоматического downgrade к anonymous enrollment, insecure TLS или
  проверке лишь совпадения CN. Требуются доверенная цепочка, срок, назначение,
  proof of private key через mTLS и привязка serial к terminal record.

## Этап0 — окончательный контракт и ownership

1. Зафиксировать registry фактически используемых7xxx, включая найденный7010:
   назначение, typed dt item, cardinality, версия, capability и response.
2. Утвердить7011 и его режимы. Первая версия — одна операция на один terminal,
   один dt object, целевой SN/текущий serial, тот же SN в новом сертификате.
3. Зафиксировать purpose renewal PIN и запрет его принятия обычным anonymous
   enrollment route. Renewal route принимает только этот purpose и правильного owner.
4. PB владеет issuance и миграциями общей схемы; MenuBuilder — operator UI и права;
   IoT — task transport/lifecycle; native adapter — key/CSR/store/reconnect.
5. Согласованно обновить invariant AGENTS: сейчас cert_serial меняется исключительно
   через POST /api/certificates/?function=setup. Новый route не должен становиться
   независимым issuer: единый issuance service, две явно описанные точки входа.
   До реализации это изменение правила должно быть закреплено вместе с контрактом.

## Этап1 — привести существующие RPC в порядок

| Стек | Изменения | Критерий |
|---|---|---|
| IoT docs/schema | method-aware dt, typed homogeneous elements, empty semantics; schema/fixtures едины для REST/MQTT | 7002+exec item отклоняется; неизвестный метод не вызывает exec |
| IoT transport | metadata method/correlation/status/result_uid; redacted audit task/session/method/stage | 7001 result header200 совпадает с сохранённым200, не default501 |
| l4con | bounded structured JSON + MQTT properties; exact topic/method routing; method-specific tsk execution | параметризованные методы ждут rsp; беспараметрические исполняются по tsk один раз |
| l4con cancel | separate accepted/applied; result после cleanup; dedup task/результатов | нет ложного cancelled и повторного запуска при QoS retry |
| IoT/UI lifecycle | сохранять cancel tracking до result/timeout, disconnect cleanup; scope cancel по контракту | параметризованный cancel проверяет session; беспараметрический cancel действует на текущую монопольную сессию |
| MenuBuilder UI/API | отдельные list/detail types, нормализация header; authorized payload; known/unknown labels | одинаковые методы в строке и деталях, нет #undefined |
| Docs | 7003–7005 реальные capability; fake7004ACK устранить через согласованную реализацию или explicit unsupported | документация не заявляет неподдержанный эффект |

dt обязателен на envelope уровне, допускает пустой массив и простые/сложные
однотипные значения. Семантика метода отдельна: обычные7001/7011 требуют параметры;
7002 может быть беспараметрической отменой текущей монопольной сессии по tsk.
Адресная отмена session требует payload и rsp. В обоих случаях req/rsp/res/cmt
сохраняются, rsp не повторяет выполненное действие. Для быстрых беспараметрических
методов пользователь допускает редкую нецелостность завершения протокола;
для7011 бизнес-idempotency и recovery обязательны.
7003 без параметров может иметь dt:[] после закрепления поддержки.
out/result schemas не меняются только ради dt. Read-only7010 audit ещё требуется.

До доставки PIN обязательно закрыть raw RPC payload logging/history exposure:
нынешние debug publisher/result paths и verbose native receive способны писать payload.

## Этап2 — PB/shared: проверяемое продление

Предложенный route: `POST /api/certificates/renew` с фазами check/setup/status.
Это enrollment HTTPS, а не отдельный канал выдачи PIN. JSON body, PIN не в URL.

- Предъявленный live certificate аутентифицируется у доверенного ingress;
  forwarded identity нельзя принимать от произвольного HTTP caller.
- Проверяются terminal SN+current serial, tenant, purpose/owner/expiry PIN,
  CSR signature и expected public key/operation binding. Нельзя выбрать чужой SN
  параметром тела или PIN другого terminal.
- mTLS должен доказывать владение предъявляемым ключом; CN сам по себе недостаточен.
- Идентификатор операции — только выданный IoT task_id. Durable issuance journal
  содержит task_id/PIN record/source serial/CSR fingerprint/issued cert/state.
  Одно активное обновление на terminal; PIN расходуется однократно.
- Повтор доставки той же task/CSR не выпускает второй сертификат. Новая task
  с уже использованным PIN не выполняет повторный выпуск; normal duplicate rejection
  не должен создавать шумные ошибки, billing/audit business events или менять serial.
  На устройстве7011/l4pin работают последовательно и не вытесняют активный
  выпуск. В PB сохранить существующий FOR UPDATE строки PIN, без введения
  дополнительной инфраструктуры блокировок. Один локальный worker сам по себе
  не означает атомарность CA+DB при timeout/разрыве клиентского соединения.
- CA и DB не являются единой транзакцией: описать issuing/reconciliation states,
  восстановление после CA success до DB commit; не обещать atomic exactly-once
  без проверки возможностей CA.
- После serial switch прежний сертификат не может выполнять обычные операции.
  Узко ограниченный повтор получения **уже выпущенного** результата может быть
  разрешён исходному cert fingerprint для той же operation/CSR и короткого окна.
  Это не общий serial mismatch bypass и не право на новый выпуск.
- Текущий in-memory15min setup retry cache не обеспечивает restart/multiworker recovery;
  durable ledger должен заменить эту зависимость для7011.
- Purpose PIN, operation journal и constraints — shared declarative models;
  Alembic исключительно PB. MenuBuilder обновляется как совместимый consumer.
- Обычный payment/policy auth не ослабляется. Подписка/блокировка — явная политика
  renewal, а не случайная зависимость от permissions remote console.

## Этап3 — IoT: task7011 и скрытый PIN в истории

```json
{
  "method_code": 7011,
  "payload": {
    "dt": [{
      "expected_sn": "<terminal SN>",
      "expected_certificate_serial": "<current serial>",
      "mode": "renew",
      "pin": "<operator-entered PIN>"
    }]
  }
}
```

Task UUID, назначенный IoT, — единственный идентификатор RPC/операции.
Его нет необходимости дополнительно дублировать в payload.dt.
Нужны authenticated tenant/terminal dispatch, capability check и per-device lock;
произвольный route URL, shell или executable path не передаются в task.

PIN создаётся сервером и доставляется этим же RPC. Он временно оказывается в task storage: это осознанное
свойство выбранного решения, а не утверждение end-to-end secrecy от IoT.
Ограничить доступ, маскировать в list/detail/audit/webhook/error, определить expiry,
поведение офлайн-очереди и удаление секрета после завершения/истечения.
Редакция history не должна повредить payload до подтверждённой доставки;
истёкший PIN никогда не запускает новый выпуск по позднему rsp.
RPC expiry не позже PIN expiry: ttl_minutes=floor(remaining_pin_seconds/60),
не создавать задачу при недостаточном остатке; не использовать ttl0, которое
в текущем протоколе имеет отдельную trigger-семантику. Дополнительно проверять
абсолютный deadline на устройстве/PB. Фактический PIN TTL конфигурируемый:
семь суток не считать установленным значением без проверки активной конфигурации.

Результат/stage без PIN и ключа: task_id, stage, code, old/new serial,
expiry, retryable, sanitized error. Task final ACK отдельно от подтверждения
работоспособности нового сертификата; result_uid стабилен при повторе.

## Этап4 — tools: l4con → l4pin → native supervisor

### Перепроверено: одновременные команды в текущем l4con

Статическая сверка `mqtt_client.c` и `command_runner.c`:
входящие MQTT PUBLISH разбирает один receiver loop; есть один current_cmd и
hWorkerThread. Если приходит следующая исполняемая7001, l4con вызывает
command_runner_request_cancel(current_cmd), WaitForSingleObject(worker,INFINITE),
закрывает handle и запускает новый worker. Это cancel-and-replace, не FIFO.
При пачке A/B/C A может получить exit130 от прихода B, а B — от C; зависит от
успевшего завершения каждой команды. Во время join блокируется MQTT receiver.
Single-instance mutex l4con защищает экземпляр сервиса, не несколько child processes.

У l4pin нет межпроцессного mutex выпуска; busy в GUI защищает только одно окно.
Один shell/Job также может запустить несколько экземпляров l4pin. Вызов через
консоль остаётся частью требуемого7011 gate, не отдельным путём обхода.
Runtime параллельные команды на production не запускались.

Для7011 закрепить protected enrollment worker со штатным покрывающим timeout:
следующая task не отменяет активный выпуск. Busy/FIFO policy уточнена ниже;
receiver не должен ожидать worker через INFINITE. Все вызовы
--renew-authenticated через RPC и remote-console сходятся в один local gate.
Закрытие console Job не прерывает уже начатую irreversible issuance phase.
Повтор с used PIN отклоняется PB до нового выпуска; повтор same task не запускается
дважды. Local gate означает serial execution, не распределённую atomic transaction.

PB уже использует `_find_terminal_by_pin(for_update=True)` в setup, с
`with_for_update(of=CertificatePin)`. Сохранить эту существующую защиту в renewal,
не усложнять постановку7011 новой distributed lock/outbox системой.

### Штатный timeout l4con: использовать существующий механизм

Проверено: CLI `--timeout <sec>` задаёт default_cmd_timeout (default30s);
RPC payload `ttl_sec` переопределяет его для команды. IoT exec DTO ограничивает
значение1–3600s, native CLI использует atoi без аналогичного bounds check.
Console UI предлагает15/30/60/120s. В runner elapsed монотонный GetTickCount64;
при превышении ttl_sec Job/process tree завершается с exit124 и EOF.
Этот механизм уже есть: новый параллельный таймер исполнения не нужен.
Для7011 перенести/переиспользовать его с bounded ttl_sec и рассчитанным бюджетом,
отдельно от delivery TTL в минутах (до PIN expiry).

Есть ограничения текущего кода: начало run deadline после старта child и первого
output callback; blocking callbacks/cleanup не имеют единого absolute deadline;
replacement joinINFINITE сам по себе не становится bounded из-за ttl_sec.
Для7011 требуется ограничить весь execution/cleanup path, не заявлять строгий
wall-clock ceiling только по наличию поля ttl_sec. В socket есть5s per-send timeout,
но это не общий deadline всей операции.

Осталось выбрать минимальную admission policy при занятом protected worker:
рекомендация — отвечать следующей исполняемой команде busy без вытеснения,
а не вводить локальную очередь и блокирующее ожидание. При необходимости FIFO
допускается только bounded и без blocking MQTT receiver. Не менять default
cancel-and-replace всех обычных7001 автоматически: protected renewal задаётся
доверенным handler/CLI provenance, а не произвольным payload flag.
Явный7002 и service shutdown отличать от вытеснения новой7001;
после отправки setup даже timeout требует сверки исхода выпуска.

- l4con handler7011 отдельно от current console CommandContext и shell runner.
  Проверяет exact dt, identity/capability/deadline, journal и отсутствие конкурента.
- Предлагаемый CLI l4pin `--renew-authenticated`: доверенный renewal endpoint,
  current certificate pinning/selection, строгая TLS server validation,
  PIN через stdin/защищённый IPC, structured progress/result. Этот флаг сейчас отсутствует.
- Этот режим разрешён только при доверенном вызове через l4con: handler7011
  либо процесс, запущенный удалённой консолью l4con. Самостоятельный локальный
  CLI-вызов отклоняется; проверять provenance/IPC, а не наличие строки CLI.
- Текущий http_client при enrollment явно выбирает NO_CLIENT_CERT_CONTEXT;
  новый режим должен предъявлять current cert непосредственно через WinHTTP
  либо доказуемо корректный proxy mTLS path. Anonymous fallback запрещён.
- URL normalization текущего helper автоматически добавляет certificates;
  новый route должен учитываться явно, без запроса на случайный path.
- Durable local journal хранит operation/CSR/key reference и стадии, не raw PIN.
  Приватный ключ остаётся non-exportable CNG. Повтор не генерирует новый CSR/key
  после того, как CA уже выдала сертификат для прежнего CSR.
- Expected SN/current serial проверяются до выпуска; новый cert/chain/key — до cleanup.
  Удаление старого cert учитывает действующие l4pin правила и recovery ledger.
- Native supervisor обеспечивает завершение операции и смену соединений независимо
  от исчезновения MQTT/browser; заранее определить владельца — l4superv или
  независимый enrollment worker, не убиваемый console cancel/Job cleanup.
- Reload/reconnect leo4proxy и зависимых клиентов: наличие30s poll — не evidence
  успешной полной ротации. Проверить свежий TLS handshake с новым serial для каналов,
  затем публиковать connected_new_identity; не полагаться на старую TCP-сессию.
- Во время CA setup нельзя считать generic7002 безопасной отменой7011.
  До irreversible issuance можно cancel; после — только recovery/finish.
- x86/x64/default /MT, packaging, component/suite version и операторская подпись.

## Этап5 — MenuBuilder: форма вместо консоли

- Действие **«Заказать удалённое продление»** (предложенное имя), текущие
  SN/expiry/serial и capability7011. Старое «Создать PIN» — отдельный ручной flow.
- Одно нажатие запускает серверное создание PIN renew + постановку7011;
  UI получает IoT task_id/status, но никогда не получает созданный PIN.
- Форма ждёт только ответ постановки: queued/task_id либо error/неподтверждённый
  исход при timeout. Повтор выполняется пользователем, с тем же серверным PIN,
  максимум три раза в открытой форме; после выхода сообщить «Повторите через 3 минуты».
  Outbox/lookup для гарантии единственной task не вводятся. Ожидать terminal
  RES7011 здесь нельзя: устройство может оставаться офлайн до PIN expiry.
- Отдельная permission certificate renewal, не console lease или arbitrary exec.
  Tenant/device binding проверяется сервером, не только UI.
- Dedicated operation progress/status с восстановлением после перезагрузки вкладки.
- Форма доступна административно активным терминалам с действующим бесплатным
  правом первого terminal или оплаченной подпиской. Проверка на сервере;
  certificate renewal permissions не выводить из наличия console lease.
- PIN не появляется в classic task details/export. Офлайн разрешён: queued_until
  ограничен PIN expiry, операция не требует ожидания результата в открытой форме.
- Success только после установки и подтверждения новой рабочей идентичности;
  issued_but_not_connected требует восстановления, не повторного ввода нового PIN.

### Отложенное завершение и сверка

Синхронная часть выдаёт PIN/создаёт задачу; сам сертификат выдаётся позже по CSR.
Предлагается отдельное событие результата операции (код после сверки реестра):
task_id/stage/code/new_serial/not_after, без PIN/ключа.
Событие75 сохраняет роль инвентаризации фактического сертификата и помогает сверке.
Финальный7011 RES подтверждает протокол, событие/сверка — новую рабочую идентичность.

Сверка triggered по результату,75,reconnect и подходящим существующим запросам;
debounce/coalesce по terminal/operation. Отдельный частый timer на каждую задачу
не нужен. Кнопка «Проверить состояние» запускает bounded сверку и при необходимости
запрос свежего состояния. Потеря события не должна навсегда оставлять pending.

## Дополнение: policy при истечении сертификата

### Повторная сверка текущего кода

По уточнению пользователя проверено, означает ли потеря доступа к policy
автоматический запрет MQTT/RTP. **Не означает немедленный запрет**:
`policy.c:run` при fetch/parse failure выставляет last_error,
но не меняет record.allowed и не сокращает offline_allowed_until.
`POLICY_GRACE_SECONDS`=72h от последнего успеха, normal poll600s;
HTTP не200, TLS/DNS/timeout сейчас объединены как unavailable/invalid.
Этот grace — намеренное поведение для сетевого сбоя, не expiry detection.

При service mode есть независимый автоматический путь: cert_store selection
отбрасывает time-invalid cert всегда, даже без --drop-on-expire. Через existing
poll30s service_mgr не находит пригодный cert и переводит proxy в standby:
останавливает MQTT/RTP/stream и сохраняет HTTP diagnostics/bootstrap.
Если найден новый пригодный cert, вместо standby происходит rotation.
Реакция ограничена очередным poll и временем остановки listeners, не hard realtime.

При переходе standby http_proxy_update_creds(NULL) вызывает policy_identity(NULL),
что убирает certificate и прекращает policy fetch, **но не сбрасывает media_allowed**.
Поэтому diagnostics policy.mqtt_rtp_allowed может остаться true до конца72h grace,
хотя service listeners уже остановлены. Foreground не имеет того же service poll loop.
Это расхождение admission state/действия, а не подтверждение reconnect storm на runtime.

Минимальное предложение: в effective local policy добавить prerequisite
«активный сертификат присутствует и time-valid». Нет cert/expired — local deny;
grace остаётся для unavailable policy с ещё действующим cert. При ротации gate
пересчитывается. Использовать уже загруженный cert и existing policy tick/poll,
без дополнительных DB/network запросов и без общего deny на любую сетевую ошибку.
Серверная expiry formula ниже — дополнительная диагностика, не необходимое
условие автоматического отключения; local gate должен работать без ответа PB.

Проверка здесь статическая: production/локальный сертификат не удалялся,
время системы не менялось, installed services не останавливались. Native tests
expiry→standby→diagnostics и same-SN rotation запланированы до реализации.

Предлагаемая формула:

`mqtt_rtp_allowed = administrative_active AND subscription_allowed AND certificate_time_valid`

PB уже получает Terminal для policy; поле `cert_not_valid_after` существует и
заполняется при setup, уточняется в terminal auth. При текущем policy request
сравнить expiry с UTC now без отдельного SELECT, scanner/job или записи is_active.
Добавить диагностический stop_fact `certificate_expired` в schema/consumer/docs;
при нескольких причинах не терять остальные. Неизвестный expiry не объявлять
expired автоматически: определить verified certificate fallback/unknown handling.

Сервер не всегда сможет доставить false после истечения: новая mTLS сессия
отвергнет expired cert до policy handler. Поэтому effective admission в leo4proxy
также ограничивается NotAfter **активного** сертификата; offline grace не перекрывает
certificate expiry. Использовать существующий local certificate poll30s и admission
worker; дополнительных DB polls нет. Текущий normal policy poll600s сохраняется.
Если нужен deadline в persisted policy, clamp к активному cert expiry; при ротации
пересчитать для нового cert, не переносить старое certificate_expired denial навсегда.

В service_mgr уже есть остановка MQTT/RTP при отсутствии пригодного сертификата.
Перед изменением проверить текущий effective path, policy state и standby/reconnect,
не добавлять дублирующий таймер. Цель — suppress upstream retries, сохранить
локальную диагностику и доступный HTTPS recovery path со строгим TLS.
Expired сертификат не становится допустимым для authenticated renew; при отсутствии
живого cert остаётся существующий ручной PIN/enrollment flow.

Отключение MQTT закрывает и канал доставки7011: задача остаётся в серверной очереди,
но этот механизм не является recovery через истёкший сертификат.

Проверки: граница expiry, missing expiry, offline grace, stale policy, time skew,
ротация same SN/new serial и снятие локального expiry gate; отсутствие upstream
reconnect storm при сохранённых диагностике/ручном enrollment.

## Этап6 — тестирование и выпуск

1. Fixtures producer/consumer:dt envelope,method/cardinality,metadata,redaction.
2. PB: wrong tenant/SN/serial/purpose/expiry, CSR substitution, races/replay,
   CA success/response loss, restart/multiworker, bounded original-cert result retrieval.
3. Native: decline invalid target before issuance, install failure, store denied,
   partial cleanup, process/service restart, dead network during each stage,
   fresh TLS reconnect and result retry; Win7 отдельно.
4. E2E test terminal: operator PIN→task7011→adapter→PB→issued→installed→reconnected;
   убедиться в DB serial, PIN consumed once, canonical result и скрытом PIN в истории.
5. Release order: backward-compatible schema/DB and PB; IoT registry/redaction with
   feature disabled; signed capable tools; capability confirmation; enable UI7011.
   Изменения phase1 можно выпускать отдельно до7011.
6. Rollback выключает новый dispatch/UI. Уже committed issuance не откатывать
   возвратом старого cert_serial; завершать recovery выпущенного сертификата.

## Проверено / не проверено

Прочитан текущий l4pin CLI/HTTP enrollment, PB certificates/auth и retry cache,
MenuBuilder PIN ownership и прежний RPC audit. Реализации renewal route,
purpose PIN, CLI authenticated renewal, native7011 и capability пока нет.
В этом этапе не выполнялись production operations, tests/build или code changes.
