# L4 Tools: единый отчёт каскада

Текущий итог каскада — подписанный и опубликованный комплект **1.9.6**, включая исправления чистой установки, зависимостей служб, прав на журнал и ожидания l4desk. Финальные проверки и ограничения приведены в этапе 15 и [completed handoff](../.agent-context/tasks/completed/2026-10-02-l4superv-service-dependencies.md). Этапы ниже сохраняют хронологию: их промежуточные статусы и версии не заменяют итог выпуска. Долговременные решения собраны в [документе по стабилизации и рискам](term_arch-l4tools-stabilization-decisions.md).

## Формат отчёта этапа

Каждый этап содержит: ID/компонент, статус, результат, изменения, проверки
с фактическими итогами, ограничения/риски, следующий шаг. Статусы:
«в работе», «выполнен», «ожидает проверки», «ожидает решения».
Отдельно различаются код/сборка, локальный runtime, production и подпись.
Успешная сборка не означает проверенную установку или подписанный выпуск.
Краткий отчёт пользователю повторяет результат, проверки и ограничения.

## 00 — Состав и источники

- Статус: выполнен (исследование).
- Результат: источники финальных исправлений найдены; выявлена потеря l4pin GUI
  и сертификатного набора в main. Работа ведётся в отдельной ветке
  `feat/l4tools-stabilization-cascade` от main84bf3aa, в чистом до плановых docs checkout.
- Изменения: введён этот единый отчёт; план и active packet связаны с ним.
- Проверки: remote main84bf3aa подтверждён; diff main/fix-l4pin рассмотрен.
  l4con1.9.4 присутствует, пакет1.9.3; identity-transition superv требует консолидации.
  Более поздние capture-профили остаются в main; старые ветки целиком не сливаются.
- Ограничения: основной пользовательский checkout грязный; его файлы не заменяются.
  Установленные EXE/службы сейчас не менялись. Внешний HTTP proxy и поддержка
  legacy CA вынесены на уточнение, зависимые изменения ждут ответа.
- Следующий шаг: l4pin — восстановление согласованных исходников, HTTP/discovery
  и проверки x86/x64/default.

## 01 — l4pin

- Статус: выполнен (код/автоматические проверки); runtime-приёмка ожидается.
- Результат: l4pin1.7.3 восстанавливает принятый GUI и безопасную замену сертификата.
- Изменения: адресно перенесены source/resources/build/tests из fix/l4pin-cert-cleanup;
  готовые старые EXE не перенесены.
- Изменения HTTP: NO_PROXY по решению пользователя; enrollment phase timeouts
  5/5/10/10с и guard чтения30с, discovery один loopback probe400мс;
  ограничение ответа, проверка Content-Length и отказ от частичного/HTTP-error ответа.
  Таймауты фаз не выдаются за жёсткий общий deadline системного вызова.
- Изменения сертификатов: после проверки issuer/time/email/CNG key — очистка
  Machine MY и профильных MY (загруженные registry hives, незагруженные через
  RegLoadAppKey); snapshots до удаления, rollback старых записей при ошибке. Новая проверенная запись
  сохраняется и при отказе очистки: SETUP уже обновил serial/потребил PIN. Возвращается ошибка очистки.
  Legacy certsrv определяется точным issuer CN и терминальным email .terminal@leo4.ru/forpay.ru;
  чужие корпоративные certsrv не удаляются. Приватные ключи старых записей не удаляются.
- Проверки: build.cmd all exit0, x86/x64/default; сертификатные8/8 на обеих архитектурах;
  HTTP/discovery12 сценариев на обеих; cross-profile изолированные registry fixtures:
  несколько MY, сохранение новой/посторонней записи, fault injection удаления,
  восстановление прежней записи в другом store и идемпотентность — failures0.
  В тестах системные roots/providers заменены fixture; настоящие MY не затронуты.
- Ограничения: действующий PIN не используется; выдача сертификата и установка
  в боевую папку не выполняются до контролируемой проверки.
- Ограничения: настоящий unloaded NTUSER.DAT, выдача/отказ CA и визуальная проверка
  GUI на разных DPI ещё не выполнялись. Клиентская HTTPS validation policy старого
  enrollment-кода не менялась на этом шаге; отдельная оценка доверия остаётся нужна.
- Следующий шаг: l4superv; runtime l4pin до включения в финальный signed release.

## 02 — l4superv/l4install

- Статус: в работе.
- Результат: graceful stop до kill; нет изменения идентичности по неуспешному HTTP probe.
- Изменения: принятую проверку store/proxy mismatch и обработку неуспехов identity
  transition восстановили адресно; поздний state_mgr/inventory не откатывался.
- Проверки: build.cmd all exit0, x86/x64/default. Изолированные SCM/process fixtures:
  штатная остановка без kill, таймаут, смена PID, shared process, отказ stop/start — failures0.
  Identity fixtures: неисправный HTTP probe, watchdog при ошибке probe,
  отказ restart/state_save, успешный переход и backoff mismatch — failures0 на обеих архитектурах.
- Ограничения: реальные службы не останавливались; Event7034 runtime не проверялся.
- Ограничения: прежний test_wait_active_component не запускался: использует реальные SCM/service names.
- Следующий шаг: leo4proxy — выбор сертификата и безопасность ротации TLS.


## 03 — leo4proxy

- Статус: выполнен (код/автоматические проверки); runtime-приёмка ожидается.
- Результат: версия1.7.3; legacy certsrv не запускает MQTT/RTP даже при явном thumbprint.
- Изменения: обязательный issuer CN iot.leo4.ru, действующий срок и доступный ключ;
  предпочтение последнего NotAfter вместо NotBefore. Без нового сертификата — standby.
- Изменения ротации: credentials имеют owner/session references, сертификат удерживается
  до закрытия последней TLS session; retired handles не используются для новых sessions.
  HTTP читает согласованный снимок identity/handles под SRW lock; reverse worker получает
  собственный снимок. Listener state не перезаписывается до завершения потока;
  незапущенные listeners повторяются на очередном certificate poll. MQTT/stream workers
  сохраняют generation, чтобы прежняя session не оживала после restart listener.
- Изменения crash: служба не ожидает Enter; crash log записывается рядом с EXE.
- Проверки: x86/x64/default build exit0. На обеих архитектурах: memory certificate store
  (legacy/явный legacy thumbprint/похожий issuer/expired/future/backdated replacement),
  ownership+retirement+concurrent borrows/SSPI handle reuse, существующие policy tests — failures0.
  Реальный Schannel loopback с временным CurrentUser KSP key: обмен ping/pong после retirement
  обоих owners и освобождения первоначального cert context — failures0 x86/x64.
- Ограничения: реальный TLS hot-swap и standby при настоящем legacy сертификате,
  Event7034 и отсутствие роста логов под боевым superv ещё не проверены.
  Разрешения server policy, 10-минутный polling, 72 часа, HTTPS и внутренние клиенты сохранены.
- Следующий шаг: l4con/l4desk — прямые и ограниченные локальные probes.

## 04 — l4con

- Статус: выполнен (код/автоматические проверки); runtime-приёмка ожидается.
- Результат: l4con1.9.5 включает финальную1.9.4 и ограниченные NO_PROXY probes.
- Изменения: общий проверенный HTTP helper; полный JSON и корневые поля identity,
  без принятия частичного тела или вложенного status за готовую идентичность.
- Проверки: build.cmd all exit0, x86/x64/default; MQTT5 tests PASS обе архитектуры.
  Отдельно test_user_events.cmd exit0: parser/IPC authorization/rate/Job lifecycle PASS x86/x64.
- Ограничения: новые события в production не отправлялись; установленная1.9.4 не заменялась.
- Следующий шаг: Mosquitto/standby, затем l4setup UI.

## 05 — Mosquitto и ожидание сертификата

- Статус: выполнен (аудит/код); runtime измерение роста логов ожидается.
- Результат: standby сохраняет локальный broker и содержит только local listener без bridge;
  активный SN приходит из проверенного new-CA store/proxy identity.
- Изменения: ошибки генерации standby/restart не коммитят пустую идентичность.
  Повторяющийся WAIT пишется при изменении состояния или раз в5 минут;
  mismatch предупреждение выдаётся при пересечении порога, не на каждом poll.
- Проверки: генераторы и topic matrix просмотрены; policy offline не рассматривается
  как исчезновение сертификата и не служит причиной рестарта локальных tools.
- Ограничения: реальный mosquitto.log при server policy=false ещё не измерялся;
  уровни broker logging без измерений не менялись.
- Следующий шаг: повторные изолированные superv tests после дополнительной правки;
  l4setup — обновление сетевых probes и UI.

## 06 — l4setup

- Статус: выполнен (код/автоматические проверки); runtime-приёмка ожидается.
- Результат: l4setup1.9.4, две постоянные панели, основные кнопки действие/отмена справа;
  операция Install/Upgrade/Repair/Verify видна до запуска. Retry использует основную кнопку
  и повторную проверку условий, вместо отдельной равнозначной кнопки.
- Изменения UI: Segoe UI в пунктах, DPI layout/WM_DPICHANGED/resize, компактное размещение
  при меньшей высоте, основной action с цветовым акцентом и high-contrast fallback.
  RichEdit журнал с ERROR/WARN цветом+начертанием, счётчиками, сохранением выделения/scroll,
  копированием. Статус/ошибки служб выделены отдельно от журнала.
- Изменения lifecycle: Check выполняется в worker, не блокируя message loop;
  Close/Cancel ждёт завершения этапа без второго принудительного выхода;
  завершение отправляется worker один раз, handles закрываются после реального окончания.
- Изменения сети: probes используют NO_PROXY/полное JSON тело; waiting и ready различаются.
  Certificate-ready polling ограничен deadline цикла, без 30 последовательных полных budgets.
- Проверки: x86/x64/default build exit0; isolated installer tests8/8 x86 и x64.
  Machine ROOT install test теперь opt-in --allow-machine-ca и в этом каскаде не запускался.
  Read-only --preview-ui визуально просмотрен на действующем Windows DPI192 (200%).
- Ограничения: настоящий install/upgrade/drain/cancel при работающих службах ещё не проверен;
  перенос окна между мониторами с разным DPI не проверялся физически.
  [Снимок read-only preview](assets/l4setup-ui-1.9.4.png) содержит демонстрационные сообщения.
- Следующий шаг: итоговые проверки/unsigned staging и controlled runtime перед подписью.

## 07 — Остальные компоненты и постоянный выпуск

- Статус: в работе.
- Результат: l4desk1.9.3 использует тот же ограниченный NO_PROXY discovery; x86/x64 сборки прошли.
  HTTP-only policy изменена также в native example; MQTT тип/presence не меняются.
- Изменения: финальные capture source/profile сохранены; l4sql не меняется,
  его версия в release manifest берётся из staged PE вместо хардкода1.0.0.
  Другие native component versions также извлекаются из staged PE и сверяются x86/x64.
- Изменения выпуска: сохранён existing Complete-SignedRelease.ps1 /tr /td SHA256;
  в native skill закреплён обязательный handoff подписи. Проверяется каждый staged EXE,
  без фиксированного числа18. После подписи components не пересобираются.
- Проверки: полный build_dist1.9.4 exit0, все native x86/x64 и installer x86/x64/default.
  Staged18 EXE, два embedded RCDATA, размеры/хеши payload проверены; signed=false.
  Отдельный native example также собран x86/x64.
  Первый build остановился на отсутствующем OpenH264 vendor в новом checkout.
  API headers и6 library files восстановлены из локального l4capture-debug vendor;
  library SHA256 совпали после копирования. l4capture EXE пересобран из актуального main source.
- Ограничения: выпуска в registry, установки нового пакета и подписи пока нет.
- Следующий шаг: runtime проверка, операторская подпись и финальная верификация.

## Общий барьер перед выпуском

- Исходники/автоматические проверки готовы; production registry/push release не выполнялся.
- Контролируемый runtime: сначала резервные копии установленных EXE/config, затем оператор
  останавливает службы; проверка новых EXE под superv, cert identity, local ready, media deny,
  сохранение HTTPS/внутренних клиентов и роста логов. Настоящие MY/PIN не изменялись в этом каскаде.
- Составная команда preview+--get-sn+--test-cert+localhost GET отклонена automatic approval
  review: `blocked by policy`, без детализированной причины. Ничего из неё не выполнилось.
  Более узкий read-only GUI preview разрешён и выполнен; проверка рабочего cert остаётся невыполненной.
- После runtime: оператор подписывает existing Complete-SignedRelease.ps1 с RFC3161;
  затем проверяются все signatures/timestamps, staged-vs-embedded hashes и только этот пакет публикуется.

## Контрольная точка перед runtime

- Финальные правки UI пересобраны штатным build.cmd all для x86/x64/default, exit0.
- Unsigned l4setup1.9.4: 29 521 920 байт. Manifest привязан к исходникам ae36bd86f5f968847998699aea421df63ebe4754; dirty=true отмечает незакоммиченные артефакты сборки.
- PAYLOAD_X86: 13 265 748 байт; PAYLOAD_X64: 15 993 135 байт.
  Извлечённые RCDATA побайтно совпали по SHA256 с подготовленными payload.
- Резервные копии установленного leo4proxy/l4con/l4superv/l4pin/l4desk и mosquitto.conf
  сохранены до замены в tools/dist/.runtime-backup/20261002-cascade; manifest содержит хеши.
- Службы, установленные EXE, сертификаты и PIN пока не изменены.

## 08 — Контролируемая установка в боевую папку

- Статус: EXE установлены, запуск оператором ожидается.
- Проверки: SCM подтвердил остановку Leo4Proxy/mosquitto/L4Con/L4Superv и PID=0;
  процессы заменяемых native tools отсутствовали. До копирования проверены хеши
  установленных файлов и резервных копий, после — совпадение с x64 build.
- Установлены: leo4proxy1.7.3, l4con1.9.5, l4superv1.9.3, l4pin1.7.3, l4desk1.9.3.
- Сертификаты/PIN не менялись, mosquitto.conf не перезаписывался при копировании.
- Baseline SCM/log sizes сохранён рядом с backup в runtime-baseline.json.
- Следующий шаг: оператор запускает proxy → mosquitto → con → superv;
  затем проверяются identity/ready, PID стабильность, MQTT и policy/log growth.

- После подтверждения запуска: все четыре службы Running; PID proxy197180,
  mosquitto178000, con219560, superv196820. Первоначальный baseline уже захватил
  старт proxy/mosquitto, поэтому это не полностью остановленный baseline.
- Local info: ready, version1.7.3, SN a4b0000773c82116d210826, issuer CN=iot.leo4.ru;
  policy успешно получена, storage_pending=false, внешнее MQTT подключено.
- Оператор отключил773. GET /api/leo4proxy/policy через обычный HTTP forwarding
  leo4proxy вернул200, mqtt_rtp_allowed=false, outgoing_https_allowed=true,
  stop_facts=[terminal_inactive]. Ожидается штатный polling без рестартов.
- Текст mosquitto.log не доступен текущему процессу по ACL (только SYSTEM),
  метаданные размера доступны. ACL не изменялся.

## 09 — Runtime policy и приёмка UI

- Статус: policy deny/allow и визуальная приёмка выполнены; настоящее installer upgrade ожидается.
- Результат: серверный false применён первым штатным polling, без offline grace;
  active MQTT стал0, local ready остался ready. После true MQTT восстановился следующим polling.
- Проверки: неизменные PID четырёх служб в течение цикла, System7034 отсутствуют.
  Локальный con→mosquitto TCP сохранялся. Входящий443 TLS12 с точным certificate pin дал200
  до и после запрета; исходящий HTTPS через18443 дал policy200 при запрете.
 20 локальных RTP/RTCP datagrams отброшены, upstream connections/bytes не увеличились.
- Проверки логов: 575 секунд измеренного denied состояния, рост3773 байта;
  последние293 секунды рост1415 байт (около290 Б/мин, экстраполяция около408 КиБ/сутки).
  После MQTT recovery размер не менялся в оставшемся наблюдении.
  Это измерение короткого окна, не гарантия долгосрочной ротации. ACL оставлен неизменным.
  У con/superv/proxy файлы stdout logs в собственных каталогах отсутствуют;
  это не доказательство отсутствия любых Windows/внешних журналов.
- UI: пользователь принял новый setup preview и финальный pin UI.
  l4pin выделяет Terminal0000773 из стандартного CN, сохраняет ведущие нули,
  показывает O/OU; увеличены PIN/Force, work-area fit и размер кнопок.
  [Снимок текущего l4pin](assets/l4pin-ui-1.7.3.png).
- Проверки l4pin: x86/x64/default сборки, certificate8/8, HTTP/profile fixtures;
  native GUI fixture x86/x64 на800x600 с taskbar40px: окно713x534,
  PIN font33px, все контролы внутри client, failures0. Issuance/store в fixture выключены.
  Обновлённый x64 pin EXE установлен с hash match; службы при этой замене не перезапускались.
- Ограничения: настоящий активный RTP session не создавался; проверены deny/drain/no-connect.
  Перевыпуск боевого сертификата/cleanup пользовательских MY не выполнялся.
- Следующий шаг: restage1.9.4 с финальным pin, настоящее UI upgrade, operator signing RFC3161.

## 10 — Финальный кандидат для actual upgrade

- Статус: подготовлен; оператору отправлена команда GUI upgrade --no-pin.
- Исходники: 3a2e48c, предыдущие eca0b8a и ae36bd8 сохранены; push/publish ещё не выполнены.
- Кандидат1.9.4: setup30 245 376 байт; x86 payload13 551 466, x64 payload16 430 931.
  Embedded SHA равны payload; l4pin внутри обоих ZIP совпадает с финальными builds.
  Staged18 EXE. Manifest signed=false; подписи/registry — следующие этапы.
- Runtime state сообщает installed_version1.7.2 (индивидуальные EXE обновлялись отдельно).
  Обычный запуск должен выбрать Upgrade к1.9.4; сертификат сохраняется, PIN не предоставляется.
- После actual upgrade: summary/state, хеши установленного набора, services/ready/identity,
  отсутствие ошибочных7034; затем existing Complete-SignedRelease.ps1 с RFC3161.
- После подписи components не rebuild. Если actual upgrade уже установил unsigned1.9.4,
  установка финального signed набора выполняется явным Repair того же пакета.

## 11 — Настоящее обновление l4setup

- Статус: выполнен, код0/status ready/phase finish.
- Результат: оператор выполнил GUI Upgrade --no-pin; state/package обновлён до1.9.4.
- Проверки: все9 установленых x64 EXE SHA совпадают с финальным staging.
  Четыре службы Running; drainage перечисляет остановку superv/con/mosquitto/proxy,
  processes_killed=[]; System7034 при обновлении отсутствуют.
  Proxy ready с прежними SN/сертификатом; MQTT и media policy разрешены.
- Сертификат: state valid/reused=true/reissued=false; боевой сертификат не перевыпускался.
  Штатные ROOT CA/firewall этапы installer выполнены, summary сообщает true.
- Smoke: proxy_info/mosquitto_port/ffmpeg_smoke_capture ok; l4desk_running=true,
  network reachable, remote_input available. Это результаты штатных локальных probes,
  не E2E удалённого ввода или активного RTP stream.
- Ограничения: текущая ОС Windows10 Home19045 x64, а исходный fail case был Win10Pro;
  проблемный WPAD/AV фильтр в этом runtime не воспроизводился.
  Warning pending_reboot_detected; автоматической перезагрузки не было.
  Actual cancel/rollback и live all-profile certificate cleanup не испытывались.
- Следующий шаг: operator RFC3161 signature → signature/hash verification;
  signed Repair при необходимости заменить installed unsigned EXE, затем publish/base push.

## 12 — Операторская подпись и signed Repair

- Статус: выполнен.
- Результат: existing Complete-SignedRelease.ps1 подписал18 staged EXE и setup;
  RFC3161 timestamp, SHA256. Все19 Authenticode Valid, signtool /pa /all /tw exit0,
  единственный согласованный signer. Компоненты после подписи не пересобирались.
- Проверки: embedded x86/x64 SHA совпадают с signed payload/manifest;
  все60 файлов каждого ZIP совпадают со staging. Manifest/SHA256SUMS согласованы.
  Signed setup SHA59180db4c1d3a11a4b16b5916d39013630a0e17d1b070d6f7d588337371d6a82.
- Оператор выполнил GUI --repair --no-pin, ошибок0/status ready.
  Все9 installed x64 EXE byte/hash совпадают со signed stage, подписи Valid и timestamps есть.
  Cert reused=true/reissued=false; исходный thumbprint/SN сохранены.
  Четыре службы Running; local ready, policy media/HTTPS true, storage_pending=false.
- Signed first-party bin artifacts синхронизированы из stage без компиляции;
  локальный runtime backup исключён через git/info/exclude, в Git/registry он не попадает.
- Registry preflight: version1.9.4 отсутствует (HEAD404).
- Следующий шаг: clean Git checkpoint/base push, manifest provenance refresh без изменения EXE,
  strict publisher verification и upload/HTTPS GET/hash verification; затем release record/base push.

## 13 — Registry и базовая ветка

- Статус: выполнен; release record и итоговый audit готовы к финальному push.
- Результат: clean checkpoint b76cf83dbb0e3e3ce7f995993904ca60f356c42f запушен
  в origin/main; manifest signed=true/dirty=false с этим source/artifact SHA.
- Registry: PUT трёх файлов1.9.4 вернул200, HEAD Digest совпал.
  Два Python GET оборвались раньше ожидаемого размера; publisher корректно
  не создал audit record по неполному чтению. Immutable version не перезаписывалась.
- Проверки: direct curl --noproxy '*' GET installer получил все29 643 832 байта
  за14.85s; SHA совпал, скачанная подпись Valid/timestamp есть.
  Direct HTTPS GET manifest/SHA256SUMS тоже совпал с локальными SHA.
  Причина различия Python/curl transport не установлена; это не доказанный дефект registry.
- После полной проверки создан штатный artifacts/l4tools/1.9.4.json и запись releases.jsonl.
  [Подписанный установщик](https://l4tools-generic.ar.cloud.ru/l4tools/1.9.4/l4setup.exe).
- Финальный runtime: подписанный1.9.4 оставлен в C:\l4tools,773 активен,
  исходный сертификат сохранён. Source/vendor cache/backup сохраняются для восстановления.
- Ограничения остаются перечисленными в этапах: Windows7/actual faulty Win10Pro,
  actual cancel/rollback, live all-profile certificate cleanup, active RTP session,
  physical mixed-monitor DPI и долгосрочная ротация журнала не проверялись.
- Следующий шаг: отдельные адресные проверки этих сценариев при воспроизводимом кейсе.
  Нового build/sign цикла сейчас не требуется: после подписи native code/resources не менялись.


## Этап 14 — первая установка, отчёт и payload 1.9.5

- Статус: подписанный1.9.5 опубликован; реальное Upgrade на773 завершилось ready/0. Полная чистая x86 установка с новым сертификатом остаётся отдельной непроведённой проверкой.
- Владелец/scope: l4setup, локальный bootstrap l4superv, release scripts; capture runtime source и MQTT-контракты не изменены. Server stacks не затронуты.
- Причина: terminal35/Win10Pro19045 x86 — сертификат установлен, но ready ожидался до запуска служб; повторная попытка запускала Mosquitto без конфигурации. Операторский запуск L4Superv восстановил четыре службы и правильный SN. Это evidence удалённого терминала, а не самостоятельный E2E агента.
- Изменение: certificate discovery после enrollment без HTTP/SCM; missing config через существующий генератор l4superv в одноразовом local-only режиме; после запуска служб ожидание ready/standby. STOPPED/zero-exit больше не ждёт120с.
- Отчёт: schema2, реальный exit_code и SCM status, unknown/not_run/null вместо выдуманных успехов; установленная версия отдельно от target_version. PE без версии отображается как установленный файл с неизвестной версией.
- Версии: пакет/setup1.9.5, superv1.9.4; legacy install1.9.3 не менялся. Capture1.0.0.0 — собственная PE-версия текущего кода, не версия всего пакета. MF-stability4456586 уже включена; сохранены поздние23cc4cb/4b2713a medium/HD настройки. SBOM suite_release/source hint актуализированы, старый checkout не копировался поверх нового.
- Манифесты: устанавливаемый l4superv/package-components.json для каждой архитектуры с9EXE, PE versions/size/SHA; обновляется после подписи. Release manifest содержит оба inventory и сверяет хеши/две версии first-party; setup version — точное сравнение. Embedded gate сверяет все61файл каждого payload со staging и capture PE sections со свежей сборкой.
- Проверено: setup x86/x64 сборка; на обеих архитектурах11сценариев actual engine с mock collaborators,2сценария actual certificate phase с fixture child (без CA/store/SCM), STOPPED/zero-exit regression,8существующих unit tests. Реальные superv x86/x64 bootstrap tests в временных папках: создание, сохранение существующего, пути с пробелами, invalid destinations, без state.json. Capture suite129/129.
- Проверено: семь компонентов собраны x86/x64 штатными build.cmd, unsigned staging/resources/setup/manifests подготовлены. Первое staging упало на недоступном Get-FileHash в дочернем Windows PowerShell; заменено SHA256 .NET, staging и manifest gates прошли. Сбой не скрыт как успешная полная build_dist команда.
- Ограничения: это isolated/local regression, не реальная чистая установка x86 с новым сертификатом. Работающие терминалы35/773, сертификаты, службы и registry release1.9.4 не изменялись. Signed release, signed Repair/Upgrade и публикация1.9.5 ещё не выполнены.
- Следующий шаг: operator Complete-SignedRelease.ps1 → все подписи/RFC3161/inventory/embedded hash → согласованная реальная приёмка → чистый release checkpoint и registry. До подписи unsigned пакет не публиковать.


### Этап14 — подпись подтверждена

- Оператор выполнил Complete-SignedRelease.ps1 для1.9.5. Все19EXE (18в двух staging и installer) проверены: один signer, Authenticode Valid, RFC3161 timestamp, signtool /pa /all /tw; SHA256SUMS совпали.
- Оба61-file embedded payload совпали с подписанным staging; capture code/data/resource PE sections совпали со свежей сборкой. Подписанные first-party EXE синхронизированы обратно в build outputs без пересборки.
- Setup29646904bytes, SHA3737d86ee322b458744a9a7b2bac8c9c1f9839eaefd67382f7c0baedfa5618f4. Подписанный пакет ещё не опубликован; операторская проверка Upgrade на773 запрошена.


### Этап14 — приёмка и публикация завершены

- Оператор выполнил подписанный Upgrade на773 (Windows10Home19045 x64): summary schema2 ready/0, installed_version=target_version=1.9.5, phase finish. Единственное предупреждение pending_reboot_detected; перезагрузка не выполнялась.
- Четыре службы Running; штатный Stop/Start без processes_killed. Сертификат reused=true/reissued=false, прежние SN/thumbprint сохранены; proxy ready/policy разрешает MQTT/RTP и HTTPS. Локальные smoke probes ok; это не отдельный E2E удалённого ввода/активного RTP.
- Установленный l4superv/package-components.json version1.9.5/archx64; все9EXE совпали по SHA со signed staging и inventory, Authenticode Valid и timestamp присутствуют. Новая l4capture сборка сохранена в payload/установке.
- Signed artifact checkpoint8bf2410b85f2037d5de9a092eac19f1f2f6273ae запушен в origin/main; manifest clean/signed, installer SHA3737d86ee322b458744a9a7b2bac8c9c1f9839eaefd67382f7c0baedfa5618f4 не изменился.
- Registry1.9.5: все3PUT HTTP200 и digest match. Python GET оказался обрезан29465227/29646904bytes и был отвергнут. Повторной загрузки/перезаписи immutable release не было. Direct curl NO_PROXY скачал все3полных файла с точным size/SHA; скачанный setup Valid/timestamp. Затем штатный publisher record сохранил artifacts/l4tools/1.9.5.json и releases.jsonl.
- Remote installer29646904bytes; manifest9665bytes SHAeaa757b0b1477bb2536dd02f2eb52f066ef7c3a7b4e74531be6cd5da623ab64d; sums165bytes SHA12e209bd270f1342d242e0cd946eca8b6a2deff9fe88eb31b44d4ac7ec8558e0.
- Осталось непроверенным: полный fresh x86/новый live CA cert и1.9.5 наterm35. Исходная причина проверена кодом, isolated regression и штатным восстановлениемterm35; реальную чистую установку на рабочемterm35 не имитировали удалением сертификата/конфига.

## Этап 15 — зависимые службы и достоверный status, пакет1.9.6

- Статус: код исправлен, полный unsigned пакет подготовлен; ожидается операторская подпись штатным Complete-SignedRelease.ps1 с RFC3161.
- Владелец/scope: l4superv, версии l4setup/package и release inventory. MQTT-клиенты/топики, серверы, сертификаты и текущий код l4capture не менялись.
- Причина: terminal55 — proxy ready с правильной идентичностью, но supervisor остаётся standby. Операторский sc enumdepend показал работающий L4Con; sc stop mosquitto вернул1051. После остановки supervisor/L4Con/Mosquitto и запуска supervisor состояние active, четыре службы Running, l4desk PID2732/session1. Это evidence оператора удалённого терминала.
- Изменение: остановка L4Con перед перезапуском Mosquitto, возврат после запуска брокера; изначально остановленный/отсутствующий L4Con не запускается helper, его запуск определяется watchdog. При отказе брокера восстановление L4Con возможно только если брокер остаётся Running; переход идентичности не фиксируется при неуспехе. Activation/renewal/standby/repair используют общий helper; повторный restart L4Con при смене SN удалён.
- Диагностика: ошибки SCM содержат этап/Windows code. Отдельный --status больше не проверяет только свои статические handles: поиск живого l4desk по полному пути и active console session без остановки процессов; требуется доступ на чтение пути процесса.
- Версии: setup/package1.9.6, superv1.9.5. Остальные PE versions сохранены, capture1.0.0.0 текущего кода. SBOM suite_release и assembly identity установщика1.9.6.0 актуализированы.
- Проверено: build_dist.cmd1.9.6 exit0; все семь компонентов x86/x64/default; новые dependency/failure-order и process discovery fixtures на обеих архитектурах плюс существующие gates. Реальный read-only --status обеих архитектур на773 находит PID202120/session1, PID четырёх служб не изменились. Bootstrap x86/x64 temporary directories: missing/existing config, spaces, invalid destinations, no state mutation — exit0.
- Проверено: inventory9EXE каждой архитектуры; все61embedded files каждого payload совпали со staging; capture PE sections совпали со свежей сборкой. Unsigned setup SHA5c86a4effae1e2cba7ff79e4ea250eb0416d93f6007e6dca50d68684e9fd50fb. Manifest signed=false/dirty=true — это подготовка, не опубликованный релиз.
- Сбои проверок: первоначально не собрались fixture macros из-за конфликтующих имён; первая suite попытка выявила ошибку отсутствующего L4Con после сброса output SCM query. Исправлены, финальные проверки прошли. Старые Windows SDK sqlext.h C4201 warnings не относятся к изменённому supervisor.
- Ограничения: новая версия ещё не подписана, не установлена в боевую папку, не опубликована. Исправленный transition проверен isolated SCM fixtures; live transition/reboot на55 и удалённые input/video E2E ещё не проверены. Работающие cert/state не удалялись.
- Следующий шаг: operator signing → подписи/timestamp/embedded hashes → согласованная установка и приёмка → clean release checkpoint, base push и registry. Логи/backup предыдущего signed1.9.5 сохранены в ignored tools/dist/.runtime-backup/20261002-supervisor-dependencies.

### Этап15 — ACL Mosquitto и подготовка чистой установки

- По уточнению пользователя журнал должен читаться обычными Windows Users. Настройка только каталога не исправляла защищённый SYSTEM-only ACL существующего файла. Общий helper для installer/supervisor ограничен mosquitto/log и mosquitto.log: SYSTEM/Admins full control, Builtin Users чтение/проход; без изменения владельца, содержимого и других папок. Отказ ACL даёт предупреждение; supervisor повторяет один раз при запуске службы от SYSTEM. Пути reparse отвергаются.
- Проверено actual Windows AccessCheck с токеном без прав Administrators x86/x64: новый наследуемый log и старый защищённый SYSTEM-only log читаются; запись и создание файлов для обычного токена запрещены, содержимое сохранено. Временные файлы удалены. После исправления диагностической переменной legacy installer полный build_dist1.9.6 exit0; оба61-file payload/current capture gates прошли. Bootstrap проверки обеих архитектур повторены и прошли.
- Текущий unsigned installer29536256bytes SHAe2bf056639dbc72b7b58b81e8d26a1947055cb02f1e62b2cc3b5c77ce94b708f; прежний unsigned SHA этапа15 заменён. Подпись ещё не выполнена.
- Оператор очистил локальную773 для настоящей первой установки. Read-only baseline2026-10-02T12:45:48Z: C:\l4tools отсутствует, четыре службы/процессы tools отсутствуют, порты1883/18883/18443 не заняты. Нового сертификата773 нет в MachineMY/CurrentUserMY; другие профили здесь не проверялись. Пользователь приготовил новый PIN; он не сохраняется в context/logs.
- Следующий шаг: операторская подпись → signature/embedded verification → GUI чистая установка с PIN → summary/SCM/SN/bridge/l4desk/ACL probes → отдельно reboot и remote55 acceptance. Чистая установка ещё не выполнена.

### Этап15 — подпись проверена, чистая установка ожидается

- Complete-SignedRelease.ps1 выполнен оператором.19EXE Valid с меткой времени и одним signer; signtool /pa /all /tw прошёл на каждом. SHA256SUMS и61-file embedded checks обеих архитектур прошли; current capture сохранён. Signed first-party build outputs синхронизированы без пересборки.
- Signed setup SHA02862805f490de064318f5b0acab4d0f34365cc2f586fab0236643e2e25bfd62. До установки C:\l4tools отсутствует, SCM registrations suite отсутствуют. Запрошена операторская чистая GUI установка773 с новым PIN; результат ещё не получен.

### Этап15 — первая чистая приёмка и поправка smoke

- Фактический Install77312:57:13–12:57:49UTC: installed none/cert absent → новый сертификат verified → четыре службы зарегистрированы/запущены, но summary degraded12 из-за l4desk false. Сам l4desk стартовал12:57:49.635UTC, после однократного probe в ту же секунду. Последующая проверка: active, четыре Auto/LocalSystem Running, l4desk211416/session1, MQTT proxy active1, правильный SN/новая thumbprint, policy разрешает. Ошибка1051 устранена. Это не input/video E2E.
- ACL действующего mosquitto.log: SYSTEM/Admins F, Builtin Users R. Предупреждение error3 в Prepare возникло раньше распаковки, когда parent directory не существовал, а не из-за итогового отказа прав.
- Доправка l4setup в рамках ещё неопубликованного1.9.6: ограниченное ожидание l4desk15s (без старта/рестарта процесса), genuine timeout остаётся degraded с причиной; проверка ACL после подготовки payload/config. Новые x86/x64 readiness fixtures плюс existing pipeline/cert/start gates; второй signed цикл и чистая приёмка ещё ожидаются.
- Оператор решил снова обнулить773 и использовать новый PIN. Агент сам не очищает службы/сертификаты. Первый signed артефакт и приёмочный log/summary сохранены в ignored first-clean-signed backup.

- Поправка проверена: x86/x64 readiness fixtures (immediate/delayed/15s timeout/invalid/changed session), pipeline11cases, certificate phase2cases и STOPPED/zero-exit. Test service fixture initially lacked new ACL header include; include path corrected, both runs passed. Setup x86/x64/default rebuilt exit0 with existing signed native payload; refreshed manifest и61-file embedded gates прошли. Unsigned correction29645824bytes SHA9e32ff38e25e12537e061380ad98ec098e7fbc8bde421cb88cdaee30a7f95a67; требуется повторная operator signature и новая чистая приёмка.

- Повторная подпись подтверждена:19EXE Valid/timestamp/same signer, signtool /pa /all /tw, SHA256SUMS и61-file embedded checks обеих архитектур прошли. Signed correction SHA83c261fd018b51c011b6b1b9ae6e520e9c4d15477d9aefbf6911d8e244ec71ef. Outputs синхронизированы без пересборки компонентов. Вторая clean baseline773 подтверждена: нет C:\l4tools/служб/процессов и нового CA773 в MachineMY/CurrentUserMY. Результат второй установки ожидается.

### Этап15 — вторая чистая установка ready/0

- Install773 Windows10Home19045x64,13:10:21–13:10:43UTC: none/absent → новый сертификат verified → ready/exit0. Ожидание l4desk13:10:41→13:10:42 успешно. Ошибки ACL error3 нет; единственное предупреждение pending_reboot_detected.
- Текущий runtime: четыре службы Running, supervisor active с правильным SN/новой thumbprint, l4desk19956/session1; proxy ready, MQTT active client1, policy разрешает. Новый сертификат валиден до2027-10-02T13:10:39Z.
- Все9installed EXE совпали со signed x64 staging/inventory, Valid/timestamp. Реальный ACL log: SYSTEM/Admins FullControl, Builtin Users SID S-1-5-32-545 Read/Synchronize. Новые права не дают записи обычным пользователям.
- Evidence summary/log сохранены в ignored second-clean. Остаются отдельные gates: автостарт после reboot, remote55 и live fresh x86; это не remote input/video E2E. Пакет ещё не опубликован.

- Пользователь отказался от reboot и поручил продолжить. Автостарт после перезагрузки записан как непроверенный, не блокирует согласованный выпуск. Установленная signed1.9.6 остаётся на773; сертификат/службы повторно не меняются.

### Этап15 — выпуск1.9.6 завершён

- Source/signed artifact checkpoint a76e586032a518e33540ce45406ee6c2a462af9a запушен в main и проверен git ls-remote. Manifest signed=true/dirty=false, source SHA совпадает; setup hash не изменён после подписи.
- [Signed1.9.6 installer](https://l4tools-generic.ar.cloud.ru/l4tools/1.9.6/l4setup.exe):29653560bytes, SHA83c261fd018b51c011b6b1b9ae6e520e9c4d15477d9aefbf6911d8e244ec71ef.
- Registry: version absent до загрузки; initial PUT network reset автоматически повторён publisher, затем3PUT200/Digest match. Python GET отверг усечённый29508510/29653560bytes ответ без записи успеха. Direct curl NO_PROXY полностью скачал3файла с точным size/SHA; скачанный setup Valid/timestamp. После этого штатный record создал artifacts/l4tools/1.9.6.json и releases.jsonl. Повторного ручного publish/overwrite/allow-dirty не было.
- Remote manifest4721bytes SHA42e3025795743b0fb818d467929229eaa86a8de13a32ebea0e4b20e0f6fbb8a5; sums165bytes SHA86604b130ae6fa2484c0a53d178c96ed6e719fb6f080cfd5749f0f836523f6b6. Инвентарь обеих архитектур содержит9EXE.
- Итог: signed1.9.6 опубликован, чистый Install773 ready/0 подтверждён, текущая установка оставлена. Непроверены reboot (пользователь отказался), новая версия на55/live fresh x86 и active remote input/video E2E. Server stacks не менялись; исходный грязный checkout сохранён.
