# Передача предложения по разрешению leo4proxy

## Контекст

- Дата: 2026-10-01; серверный этап опубликован, leo4proxy 1.7.2 установлен и оставлен работающим; real native deny/allow polling под l4superv прошел, терминал 773 снова активен.
- Задача: реализация и deployment только ProcessingBackend, затем план leo4proxy по развернутому API. Пользователь явно разрешил builder 176 → registry → production pull и push в main, а также чтение зависимых tools.
- Владельцы: ProcessingBackend — вычисление и API; leo4proxy — транспорт и локальное хранение; shared — существующий Terminal.is_active.
- Поток: Terminal.is_active и независимая константа разрешения HTTPS → ProcessingBackend → mTLS HTTPS → leo4proxy → внешние MQTT/RTP и исходящие HTTPS-маршруты.
- Scope: ранее ProcessingBackend API/tests и терминальный Nginx; текущий этап — код/тесты/бинарники только leo4proxy и документы. Shared и зависимые tools не изменены; их чтение разрешено пользователем.

## Результат

- [Отчет](../../../docs/term_arch-leo4proxy-server-permission.md) зарегистрирован в docs/README.md.
- Контракт GET /api/leo4proxy/policy реализован и развернут; добавлены schema, вычисление, router, терминальный Nginx location без mirror и auth/route tests.
- Код опубликован в main: 1b8fdb5dbdbe944c123c65db90876b20982b320c. Builder 176 → registry → production pull; digest sha256:20307553eec9231d7724f6df13e6a7eef9bdd40cbf0e46fd3782024bcce1867e. Health прошел, running=true, restart-count=0, revision/image совпали.
- В отчете добавлен отдельный раздел внедрения leo4proxy по развернутому API, с таблицей зависимостей и критериями отсутствия рестартов.
- Согласовано: MQTT/RTP-разрешение без поля БД, единственный аргумент Terminal.is_active; входящий HTTPS и поллинг сохраняются; запас MQTT/RTP 72 часа; потеря двух копий возобновляет запас; ужесточения исключены.
- Добавлен outgoing_https_allowed: backend пока всегда true; только явный false запрещает обычные исходящие маршруты. Policy/certificates/licensebilling остаются доступны. Отсутствие достоверного false означает true, независимо от MQTT/RTP-политики.
- Хранение выбирается по доступности реестра/JSON, версии Windows, разрядности и учетной записи. Отказы хранения не дают санкций; неоднозначность восстановления решается в пользу клиента.
- Реализованы policy.c/h и ограниченный JSON parser, lifecycle в console/service, mTLS WinHTTP polling с отдельной копией cert context, отмена external media/MQTT sockets, UDP drain, HTTPS route filtering и policy diagnostics без изменения ready. Код остальных tools не менялся.
- Пользователь подтвердил итоговую версию 1.7.2 и сохранение прозрачного MQTT proxy без presence. Runtime macro и resource version синхронизированы. Новый пакет l4tools не собирать.

## Task intake клиентского этапа

- Тип: native Windows tool change; только tools/leo4proxy, его сборки/тесты и связанные документы.
- Владелец серверного решения: ProcessingBackend; хранения, polling и transport gates: leo4proxy. Модели/миграции не меняются.
- Producer → transport → consumer: развернутый terminal policy → certificate-authenticated HTTPS → leo4proxy → external MQTT/media и отдельный outgoing HTTPS gate.
- Инварианты: локальные listeners, incoming HTTPS, metadata/identity, ready и SCM running сохраняются; сертификаты не удаляются; другие tools не получают stop/restart команд.
- Проверка: x86/x64 /MT и негативные/state/socket/UDP tests; затем установленная папка под реальным l4superv с ручными действиями пользователя. Backend checks и l4tools packaging не запускать.
- Пользователь сам останавливает/запускает службы и отключает/включает терминал 773 по шагам; зависимые действия не выполнять до подтверждения.

## Проверки и evidence

- [x] Прочитаны модель Terminal, get_current_terminal и регистрация маршрутов.
- [x] Прочитаны документы архитектуры утилит и контракт медиатракта.
- [x] Consumer: прочитаны main, service manager, конфигурация, HTTP/reverse HTTPS, MQTT и RTP внутри tools/leo4proxy. Подтверждены локальные диагностические маршруты, cert-based ready, направления HTTPS, connect и SCM failure actions.
- [x] Прочитаны зависимые участки l4superv, l4setup, Mosquitto bridge, l4con, l4desk, l4capture, l4pin, service MQTT example и упаковки. Подтверждены триггеры standby/смены identity/рестартов; требование сохранения ready, identity и слушателей закреплено в отчете.
- [x] Дополнительный аудит по запросу планирования leo4proxy: проверены service restart/SCM recovery и дочерние media recovery. Прямой рестарт по внешнему MQTT/RTP disconnect в прочитанных участках не найден. Выделены косвенные риски: локальный metadata failure/ready → standby и Mosquitto restart; FFmpeg progress stall >10 секунд или выход media child → recovery. l4capture send failure учитывается как drop, adapter восстанавливает завершившийся child при активной lease. План дополнен контролем PID media children, handshake/backoff-переходами и приемом/отбрасыванием UDP при запрете.
- [x] Локальный ProcessingBackend: 138 passed; Ruff check/format app и измененных тестов; Pyright app без ошибок.
- [x] Linux builder: 138 backend tests и 31 release-flow test; Ruff/format/Pyright прошли.
- [x] Read-only scan 12 подготовленных tracked files на credential URLs, private keys и tokens: 0 совпадений.
- [x] Production: digest/revision/health/restart count; Nginx config SHA, nginx -t/reload; публичный policy без сертификата возвращает 401; схема проверена в работающем OpenAPI; соседние контейнеры не пересозданы.
- [x] По дополнительному прямому разрешению пользователя проверен authenticated GET через установленный leo4proxy на http://127.0.0.1:18443 для терминала 773. Local info/SN: 200, ready, certificate_found=true, a4b0000773c82116d210826; policy: 200, application/json, no-store, оба флага true, stop_facts=[]. Сертификат не экспортировался, headers не подменялись.
- [ ] Отключение реального терминала на production не проверялось: состояние не менялось. Оба состояния покрыты настоящей auth dependency с mock DB.
- [x] Native build.cmd all: x86/x64 /MT, default=x86. --version обеих сборок: 1.7.2. x86 SHA256 59eb0f0c1ef34b5a01d9d61de0d7be88de09192fee1638dbc3c9ad9f4e6ef947; x64 69c41be9473533d3d286711e01be00a44d083fea78aa32ae7590907b6f19f3f1.
- [x] tests/test_policy.cmd: x86/x64 /W4 /WX; schema/duplicate/SN/invalid, grace boundary, known deny, copy precedence, strict HTTPS paths, shutdown existing socket/refuse new connect, real registry/JSON recovery and denied RTP/RTCP UDP drain on isolated dynamic ports. Disposable test key/files removed.
- [x] До замены: установленный exe x64, 1.2.0; ready, identity terminal 773 и active l4superv state. Rollback C:\\l4tools\\leo4proxy\\leo4proxy.exe.before-1.7.2-20261001-113851; original SHA256 de3dd91b8a3f65562c365be8f7c44eb4ad3da87f60afeb990390f2bd0fc7e5b2. Baseline в ignored tools/leo4proxy/obj/runtime-policy-baseline.json. Mosquitto config SHA256 6fcd543a903a7167183a2693df01c6f39ed065fafef7982c8256aad754eb177e.
- [x] После ручной остановки пользователем SCM stopped/PID=0 проверены; установлен только x64 exe, его hash/version совпали со сборкой. Пользователь запустил службы. Native worker успешно принял настоящий authenticated policy: generation=2, оба флага true, last_error пустой, storage_pending=false; registry и JSON совпали. Локальный ready/SN/cert/routes_active сохранены.
- [x] Новый baseline: Leo4Proxy PID 183528, Mosquitto 218760, L4Con 187632, L4Superv 216736, l4desk 219260. Исходящий MQTT 8883 Established; video child сейчас отсутствует. Incoming HTTPS metadata на локальном 443: 200 ready. Baseline в ignored obj/runtime-policy-started.json.
- [x] Пользователь отключил 773 на сервере. Проверка через proxy: false + terminal_inactive, HTTPS true. Client до планового poll сохраняет true, без принудительного refresh. Первый success 11:43:58 MSK; следующий poll ожидается около 11:53:58 MSK.
- [x] Real deny принят в 11:53:59 MSK, generation=3, terminal_inactive; external MQTT закрыт, RTP/media connections=0, ready/identity и все baseline PID включая l4desk сохранились более 10 минут. Registry/JSON deny совпали, storage_pending=false. 200 probe UDP packets на реальных 5004/5005 прочитаны/отброшены, listeners сохранены; локальный MQTT accepts/closes без CONNACK; incoming HTTPS metadata 200 ready.
- [x] По дополнительному запросу проверены размеры логов: Mosquitto 656909→660068 байт на полном наблюдении, без устойчивой rate-оценки; отдельные 162,7 секунд — delta=0. l4desk 146416, delta=0; другие log-файлы без роста, video child отсутствует. Mosquitto content недоступен из-за SYSTEM-only ACL; ACL не менялся. l4desk имеет 5MB rotation; Mosquitto rotation в прочитанных suite config не найдена. Evidence в ignored obj/runtime-policy-log-growth.json.
- [x] Пользователь включил 773 обратно, server true/true подтвержден. Client до следующего poll сохраняет deny. Новый exe по указанию пользователя остается в боевой папке при успешной приемке; rollback не выполнять.
- [x] Allow poll в 12:14:01 MSK: generation=5, true/true, stop_facts очищен; external MQTT 8883 Established, mqtt_active_clients=1. Final assertions подтвердили прежние PID всех четырех служб и l4desk, ready/SN/thumbprint/certificate_found/routes_active, supervisor active и неизменный Mosquitto config hash. Registry/JSON совпали, storage_pending=false. Evidence в ignored obj/runtime-policy-final.json. Новый exe оставлен установленным, 773 активен.
- [x] Итоговый log snapshot: Mosquitto 659832→664146 (+4314 байт за 17,5 минуты), включая конец deny и reconnect; это не постоянная rate-оценка. l4desk остается 146416. Быстрого роста в данном наблюдении нет; content Mosquitto по-прежнему не прочитан из-за ACL.
- [ ] Real active media lease/FFmpeg recovery, реальная 72-часовая API outage и future HTTPS false на работающем backend не проверены. Grace/fallback/HTTPS false покрыты native tests, текущий backend всегда HTTPS true.
- Клиентский этап не меняет backend: его tests/linters не запускались. Новый пакет l4tools не собран.
- N/A: MenuBuilder/shared checks — код этих компонентов не изменен.
- Уровень evidence: статический server/tools код, Windows/Linux tests, registry release, production runtime и authenticated policy GET через реальный proxy терминала 773. Production secrets не читались, тестовые credentials не создавались.

## Следующий шаг

Текущий клиентский этап завершен: установленный leo4proxy 1.7.2 работает, terminal 773 активен, native deny/allow без рестартов подтвержден. Пакет не выпускался. Будущий выпуск пакета, расширенная media/outage приемка, исследование злоупотреблений и отдельное логирование/rotation других tools — отдельные задачи. Runtime evidence и rollback exe сохранены; новый exe не откатывать.

Штатные builder/registry release artifacts и production deployment metadata сохранены как evidence/rollback. Временных credentials не создавалось. Локальный временный nginx config удаляется после проверки доставки. Рабочая копия пользователя не очищается; посторонние untracked files сохраняются.
