# L4 Tools: единый отчёт каскада

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
- Следующий шаг: полный unsigned build, runtime проверка, операторская подпись и финальная верификация.

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
