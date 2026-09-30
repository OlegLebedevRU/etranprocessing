# Передача предложения по разрешению leo4proxy

## Контекст

- Дата: 2026-10-01; рабочее дерево без commit.
- Задача: реализация и deployment только ProcessingBackend, затем план leo4proxy по развернутому API. Пользователь явно разрешил builder 176 → registry → production pull и push в main, а также чтение зависимых tools.
- Владельцы: ProcessingBackend — вычисление и API; leo4proxy — транспорт и локальное хранение; shared — существующий Terminal.is_active.
- Поток: Terminal.is_active и независимая константа разрешения HTTPS → ProcessingBackend → mTLS HTTPS → leo4proxy → внешние MQTT/RTP и исходящие HTTPS-маршруты.
- Scope: ProcessingBackend API/tests и терминальный Nginx; документы и чтение зависимых tools для следующего клиентского этапа. Shared и исходники tools не изменены. Чтение зависимостей дополнительно разрешено последним запросом пользователя.

## Результат

- [Отчет](../../../docs/term_arch-leo4proxy-server-permission.md) зарегистрирован в docs/README.md.
- Контракт GET /api/leo4proxy/policy реализован и развернут; добавлены schema, вычисление, router, терминальный Nginx location без mirror и auth/route tests.
- Код опубликован в main: 1b8fdb5dbdbe944c123c65db90876b20982b320c. Builder 176 → registry → production pull; digest sha256:20307553eec9231d7724f6df13e6a7eef9bdd40cbf0e46fd3782024bcce1867e. Health прошел, running=true, restart-count=0, revision/image совпали.
- В отчете добавлен отдельный раздел внедрения leo4proxy по развернутому API, с таблицей зависимостей и критериями отсутствия рестартов.
- Согласовано: MQTT/RTP-разрешение без поля БД, единственный аргумент Terminal.is_active; входящий HTTPS и поллинг сохраняются; запас MQTT/RTP 72 часа; потеря двух копий возобновляет запас; ужесточения исключены.
- Добавлен outgoing_https_allowed: backend пока всегда true; только явный false запрещает обычные исходящие маршруты. Policy/certificates/licensebilling остаются доступны. Отсутствие достоверного false означает true, независимо от MQTT/RTP-политики.
- Хранение выбирается по доступности реестра/JSON, версии Windows, разрядности и учетной записи. Отказы хранения не дают санкций; неоднозначность восстановления решается в пользу клиента.

## Проверки и evidence

- [x] Прочитаны модель Terminal, get_current_terminal и регистрация маршрутов.
- [x] Прочитаны документы архитектуры утилит и контракт медиатракта.
- [x] Consumer: прочитаны main, service manager, конфигурация, HTTP/reverse HTTPS, MQTT и RTP внутри tools/leo4proxy. Подтверждены локальные диагностические маршруты, cert-based ready, направления HTTPS, connect и SCM failure actions.
- [x] Прочитаны зависимые участки l4superv, l4setup, Mosquitto bridge, l4con, l4desk, l4capture, l4pin, service MQTT example и упаковки. Подтверждены триггеры standby/смены identity/рестартов; требование сохранения ready, identity и слушателей закреплено в отчете.
- [x] Локальный ProcessingBackend: 138 passed; Ruff check/format app и измененных тестов; Pyright app без ошибок.
- [x] Linux builder: 138 backend tests и 31 release-flow test; Ruff/format/Pyright прошли.
- [x] Read-only scan 12 подготовленных tracked files на credential URLs, private keys и tokens: 0 совпадений.
- [x] Production: digest/revision/health/restart count; Nginx config SHA, nginx -t/reload; публичный policy без сертификата возвращает 401; схема проверена в работающем OpenAPI; соседние контейнеры не пересозданы.
- [x] По дополнительному прямому разрешению пользователя проверен authenticated GET через установленный leo4proxy на http://127.0.0.1:18443 для терминала 773. Local info/SN: 200, ready, certificate_found=true, a4b0000773c82116d210826; policy: 200, application/json, no-store, оба флага true, stop_facts=[]. Сертификат не экспортировался, headers не подменялись.
- [ ] Отключение реального терминала на production не проверялось: состояние не менялось. Оба состояния покрыты настоящей auth dependency с mock DB.
- [ ] Runtime tools, native build и E2E клиентской политики не выполнялись: клиентский код еще не реализован.
- N/A: MenuBuilder/shared и native checks — код этих компонентов не изменен.
- Уровень evidence: статический server/tools код, Windows/Linux tests, registry release, production runtime и authenticated policy GET через реальный proxy терминала 773. Production secrets не читались, тестовые credentials не создавались.

## Следующий шаг

Следующий этап — реализовать только leo4proxy по готовому разделу отчета, затем проверить отсутствие policy-induced рестартов на изолированном стенде и выпустить пакет отдельно. Исследование злоупотреблений — новая задача. Карточка ProcessingBackend обновлена под развернутый API.

Штатные builder/registry release artifacts и production deployment metadata сохранены как evidence/rollback. Временных credentials не создавалось. Локальный временный nginx config удаляется после проверки доставки. Рабочая копия пользователя не очищается; посторонние untracked files сохраняются.
