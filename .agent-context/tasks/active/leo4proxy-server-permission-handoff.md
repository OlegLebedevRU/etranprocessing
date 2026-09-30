# Передача предложения по разрешению leo4proxy

## Контекст

- Дата: 2026-10-01; рабочее дерево без commit.
- Задача: реализация и deployment только ProcessingBackend, затем план leo4proxy по развернутому API. Пользователь явно разрешил builder 176 → registry → production pull и push в main, а также чтение зависимых tools.
- Владельцы: ProcessingBackend — вычисление и API; leo4proxy — транспорт и локальное хранение; shared — существующий Terminal.is_active.
- Поток: Terminal.is_active и независимая константа разрешения HTTPS → ProcessingBackend → mTLS HTTPS → leo4proxy → внешние MQTT/RTP и исходящие HTTPS-маршруты.
- Scope: правки документации, минимальное чтение ProcessingBackend/shared и карточек, разрешенное чтение только tools/leo4proxy. Другие tools не участвуют и не исследованы.

## Результат

- [Отчет](../../../docs/term_arch-leo4proxy-server-permission.md) зарегистрирован в docs/README.md.
- Контракт GET /api/leo4proxy/policy реализован; добавлены Pydantic schema, вычисление, router, конфигурация Nginx и auth/route tests. Deployment evidence будет записано после выпуска.
- Согласовано: MQTT/RTP-разрешение без поля БД, единственный аргумент Terminal.is_active; входящий HTTPS и поллинг сохраняются; запас MQTT/RTP 72 часа; потеря двух копий возобновляет запас; ужесточения исключены.
- Добавлен outgoing_https_allowed: backend пока всегда true; только явный false запрещает обычные исходящие маршруты. Policy/certificates/licensebilling остаются доступны. Отсутствие достоверного false означает true, независимо от MQTT/RTP-политики.
- Хранение выбирается по доступности реестра/JSON, версии Windows, разрядности и учетной записи. Отказы хранения не дают санкций; неоднозначность восстановления решается в пользу клиента.

## Проверки и evidence

- [x] Прочитаны модель Terminal, get_current_terminal и регистрация маршрутов.
- [x] Прочитаны документы архитектуры утилит и контракт медиатракта.
- [x] Consumer: прочитаны main, service manager, конфигурация, HTTP/reverse HTTPS, MQTT и RTP внутри tools/leo4proxy. Подтверждены локальные диагностические маршруты, cert-based ready, направления HTTPS, connect и SCM failure actions.
- [ ] Остальные tools и их runtime-реакция не проверены, вне scope. Документ требует сохранения локального контракта, слушателей, SERVICE_RUNNING и процесса leo4proxy без рестартов по политике.
- [x] Локальный ProcessingBackend: 138 passed; Ruff check/format app и измененных тестов; Pyright app без ошибок.
- [ ] Deployment и интеграция еще не проверены; результаты будут добавлены после выпуска.
- N/A: MenuBuilder/shared и native checks — код этих компонентов не изменен.
- Уровень evidence: документ, статический серверный код и код leo4proxy. Секреты и тестовые credentials не использовались.

## Следующий шаг

Реализация leo4proxy пока не входит в задачу. Выпустить серверный API и проверить его, затем прочитать зависимые tools и закрепить в отчете точки внедрения и условия стабильности. Исследование злоупотреблений — новая задача. Карточка ProcessingBackend обновлена под реализованный API.

Внешних ресурсов и временных credentials не создавалось. Остались отчет, запись в индексе и этот handoff.
