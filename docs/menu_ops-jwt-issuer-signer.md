# Внутренний JWT issuer MenuBuilder

## Контракт и границы

Отдельный процесс `app.jwt_signer:app` использует release-образ MenuBuilder.
Он принимает `POST /etranprocessing/jwt` в формате существующего клиента:
`params.requestBody` — точная строка JSON; HMAC-SHA256 вычисляется по
`timestamp.nonce.requestBody`, передаётся как `sha256=<base64>`.
Внешние identity-поля должны совпадать с подписанным телом. `clientId`,
`aud`, `iss` и `kid` фиксируются настройками signer; TTL ограничен его политикой.

Результат содержит RS256 `accessToken`, случайный непрозрачный `refreshToken`,
`tokenType`, `expiresIn`, `refreshExpiresIn`, `orgId`, `userId`, `roleId`.
Проверка refresh и хранение/ротация сессий остаются ответственностью MenuBuilder.
Signer не подключается к БД и не создаёт новые пользовательские auth-маршруты.

Подпись запроса обязательна; допустимый возраст по умолчанию 120 секунд,
опережение часов — не более 30 секунд. Повтор идентичного запроса возвращает
тот же результат. Повтор nonce с другим телом отклоняется. Кэш ограничен;
при заполнении возвращается 429 без удаления действующей защиты от replay.
Кэш находится в памяти одного worker: не запускать несколько workers/реплик.
Перезапуск очищает кэш; ранее подписанный запрос может повторно пройти
до истечения короткого окна свежести. Часы host должны быть синхронизированы.

## Ключ и окружение

Ключ проверки `lk-key-000003_public.pem` встроен в `app.jwt_public_key` и
обе конфигурации nginx: `MenuBuilder/nginx.conf`, `nginx-configs/port_3000.conf`.
`JWT_PUBLIC_KEY` в окружении backend, если задан, перекрывает встроенный ключ:
при выпуске удалить старое значение либо заменить его новым публичным ключом.
Signer сверяет пару ключей при старте и не запускается при несовпадении.

Закрытый ключ не включается в Git, образ, build context или окружение backend.
На host он хранится вне checkout: каталог root-only 0700, файл UID/GID
10001:10001, mode0400. Подключается отдельным read-only bind mount;
`create_host_path: false` не допускает незаметного создания каталога вместо ключа.

Signer запускается как 10001:10001, с read-only root filesystem, без capabilities
и без повышения привилегий. Публичного порта нет. Приватная Docker-сеть
соединяет только signer и MenuBuilder backend; backend сохраняет обычную сеть.

Для signer подготовить отдельный untracked env-файл по
`MenuBuilder/deploy/jwt-signer.env.example`, mode0600. Значение
`JWT_ISSUER_INTERNAL_SECRET` и issuer-контекст должны совпасть с backend.
Секрет внутреннего signer отделён от `SERVICE_TO_YC_SERVICE_SECRET` внешнего
резерва; его восстановление не требует раскрывать внутренний секрет.
Не подключать весь backend env-файл: signer не нужны DB, платежные или другие ключи.

Compose overlay: `MenuBuilder/deploy/jwt-signer.compose.yaml`.
Переменные интерполяции задаются в защищённом deployment environment:

| Переменная | Назначение |
|---|---|
| `MENUBUILDER_BACKEND_IMAGE` | Проверенный registry image с immutable digest; тот же образ для backend и signer |
| `JWT_SIGNER_PRIVATE_KEY_HOST_FILE` | Абсолютный путь защищённого закрытого ключа на host |
| `JWT_SIGNER_ENV_FILE` | Абсолютный путь отдельного env-файла signer |
| `JWT_ISSUER_INTERNAL_URL` | Полный URL signer в приватной Docker-сети, включая `/etranprocessing/jwt` |
| `JWT_ISSUER_FALLBACK_ENABLED` | false, пока резервный issuer не проверен с новым ключом |

Overlay предназначен для корневого production Compose (`default` network).
Его следует сохранять в постоянном наборе Compose-файлов будущих релизов.
Первое включение: `MenuBuilder/deploy/activate_jwt_signer.py` с проверенным image
digest, SHA, путями ключей и internal URL из окружения выпуска. Он сохраняет сервис
в основном Compose, создаёт внутренний HMAC-секрет и защищённые env-файлы,
проверяет health и меняет только JWT PEM в mounted nginx. Этот шаг включает native
CAPTCHA MenuBuilder с отдельным секретом. При ошибке восстанавливает конфигурацию.
После установки штатный launcher обновляет backend и signer одним digest,
проверяет оба и откатывает оба при отказе; соседние сервисы не пересоздаёт.

## Резерв и порядок переключения

Serverless URL и транспорт сохраняются. Резерв сейчас недоступен и его прежний
ключ недоступен; он не считается рабочим после ротации. Включать резерв только
после перевода на соответствующий новый ключ и проверки контракта.
Существующие access JWT старого ключа перестанут проходить проверку;
refresh-сессии остаются в БД и должны получить новый access JWT через signer.

Перед переключением проверить новую пару ключей, общий HMAC-секрет без его
вывода, signer health и подпись его ответа новым ключом. Затем обновить
backend env/образ и оба nginx public-key consumer, выполнить nginx config test
и reload. Выполнить login, refresh, tenant switch, logout и проверки отказа
для неправильной подписи/старого ключа. Не выводить реальные JWT или ключи.

Миграции БД не нужны. Проверки Python обязательны для MenuBuilder backend;
frontend и ProcessingBackend не изменяются и не требуют своих проверок.
Runtime переключение выполняется только после локальной валидации и
проверки состава постоянных Compose overrides.
