# CAPTCHA при входе и регистрации

Опубликовано 2026-10-03: backend c5db1c2, frontend b066e59.
[Проверки и ограничения](../.agent-context/tasks/completed/2026-10-03-l4desk-auth-captcha.md).

MenuBuilder использует Yandex SmartCaptcha с той же парой ключей, что и форма
обращений l4desk.ru. Ключи берутся из окружения, в Git не сохраняются.

Настройки backend:

- `SMARTCAPTCHA_ENABLED`: по умолчанию `true`; `false` предназначен для явно
  отключённого локального/test контура.
- `SMARTCAPTCHA_SITE_KEY`: публичный клиентский ключ существующей CAPTCHA.
- `SMARTCAPTCHA_SECRET_KEY`: серверный ключ существующей CAPTCHA.
- `SMARTCAPTCHA_ALLOWED_HOSTS`: JSON-массив допустимых `host` в ответе провайдера.
  Для опубликованного портала используются его домен и домен с портом.

Публичный `GET /api/auth/captcha` возвращает только `enabled` и `site_key`.
Frontend не требует ключей в build-time env. В настройках существующей CAPTCHA
Yandex должен быть разрешён домен портала, если включено ограничение доменов.

`POST /api/auth/login`, `/api/auth/register`, `/api/auth/register/resend`
принимают `captcha_token`. Проверка выполняется до аутентификации, отправки
письма или создания регистрации. HTTP Basic не обходит CAPTCHA; токен можно
передать в JSON вместе с Basic заголовком. Refresh, подтверждение email и
авторизованные сессии продолжают работать без CAPTCHA.

Backend отправляет POST form-urlencoded в официальный `/validate`, допускает
только `status=ok` и разрешённый `host`. Пустой, неверный, истёкший токен или
чужой домен дают 400; ошибка HTTP/сети/JSON или неполная конфигурация — 503.
Provider message и токены не попадают в лог или HTTP-ошибку.
См. [контракт проверки Yandex](https://yandex.cloud/en/docs/smartcaptcha/concepts/validation).

UI не отправляет форму до прохождения проверки. При истечении токена кнопка
снова блокируется; после каждого запроса CAPTCHA создаётся заново, поскольку
токен одноразовый. Ошибки загрузки виджета показывают кнопку повтора.

Лендинг хранится в отдельной существующей локальной папке `l4desk-landing/`
основного checkout. Изменения его HTML/CSS сохранены в
[патче](../l4desk-landing/materials/history/landing-auth-buttons.patch), без ключей и импорта
неотслеживаемых исходников проекта. Кнопки ведут на HTTPS `/register` и `/login`
портала, доступны в шапке и первом экране, включая мобильную ширину.
