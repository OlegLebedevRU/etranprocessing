# l4desk.ru → MenuBuilder: вход, регистрация, SmartCaptcha

## Task intake

- Scope: HTML/CSS существующего лендинга; MenuBuilder auth backend и frontend.
- Owner: MenuBuilder владеет auth/registration; landing — ссылками перехода.
- Flow: landing → HTTPS portal → CAPTCHA widget → MenuBuilder → Yandex validate
  → существующий credential/registration service.
- Инварианты: секреты только env; CAPTCHA до auth/registration mutation;
  session refresh, JWT, tenant policy, billing и shared DB не меняются.
- Миграции не требуются. MQTT/native/legacy не затронуты.
- Source: base a5ff1f2; backend/initial frontend c5db1c2; corrective frontend b066e59.
  Отдельный landing checkout
  пользователя сохранён без очистки его неотслеживаемых файлов.
- MCP Ops Readiness: UNAVAILABLE (нет callable MCP ops); SSH fallback.
  Production: RAM 2138 MiB, disk 41%, load .11; landing/builder:
  RAM 2793 MiB, disk 57%, load .01, 2026-10-03 20:28–20:32 UTC.

## Выполнено и проверено

- CAPTCHA runtime config, серверная проверка домена/status, fail-closed 400/503,
  общий виджет с expiry/error/retry/reset, защита initial/resend регистрации.
- Landing: кнопки регистрации/входа в header и hero; mobile сохраняет кнопки.
  [Воспроизводимый патч](../../../l4desk-service/docs/landing-auth-buttons.patch).
- Ruff check/format и Pyright app: PASS.
- Backend полный suite с CAPTCHA: 613 PASS, 20 PostgreSQL-dependent SKIP.
- Frontend TypeScript/Vite build и 70 Vitest: PASS.
- Playwright Edge initial полный набор (auth + subscriptions) повторён дважды:
  8 PASS. Corrective auth повторён дважды: 6 PASS (нельзя submit без токена; failed login
  сбрасывает токен; configuration failure блокирует регистрацию и позволяет retry).
- Existing key pair извлечена из разрешённого локального неотслеживаемого файла
  без сохранения в tracked files; production env настроен, защищённый backup есть.
- Дополнение пользователя: отдельная колонка «Номер терминала» в подписках
  показывает `Terminal.device_id` (например 1000009), а не внутренний PK.
  API добавляет `device_id`; выбор оплаты по `terminal_id` сохраняется.

## Публикация и runtime evidence

- 2026-10-03, окно 20:43–20:54 UTC: 176 builder → registry → production pull.
  Backend digest `sha256:6ae064260aaadc5a35e31f6b7d17d01ca2d12a756b1f59f7039e54371a9bb749`;
  frontend final digest `sha256:bb8f0845ab1d323c994ef0b1caec923084e64095121c81ca8a3816df562a80b2`.
  Deployer подтвердил source SHA, image, health и совпадение served frontend.
- Builder rerun: backend 613 PASS, 20 SKIP; Ruff/Pyright PASS;
  frontend Vitest/TypeScript/Vite PASS. Схема 029 сохранена, миграций нет.
- Production backend container `68e4d3f85209`; остальные 11 контейнеров,
  включая ProcessingBackend и nginx-default, сохранили ID.
  Base Compose дополнительно закреплён на проверенный backend digest;
  структурная сверка показывает изменение только его image, compose config PASS.
- Landing пересобран и обновлён только service landing: `51decf70cf75`, healthy.
  Contact `6dccff6e12a7` и все соседи сохранили ID; сертификатные volumes сохранены.
  HTTPS index отдаёт новые ссылки и CSS cache version. Local Edge: header CTA
  видимы и без горизонтального overflow при 320/375/768/1024/1440 px.
- Public `/api/auth/captcha`: enabled, client key совпадает с landing, secret
  отсутствует. Production missing token / invalid token login: 400/400;
  provider POST ответил 200. Никаких пользователей/писем не создано проверкой.
- Live Edge mobile: `/login` и `/register` остаются на своих URL, реальный Yandex
  script и видимый slider widget загружены; submit до challenge disabled.
  `/register/confirm` остаётся публичным. Реальный ручной challenge, successful
  login, registration/email и авторизованный subscription API E2E не выполнялись.
- Найден и исправлен существовавший redirect анонимной регистрации в login:
  неудачный `/auth/me` → refresh 401 больше не перенаправляет публичные auth pages.
  Защищённые страницы сохраняют redirect; refresh/logout/tenant checks не ослаблены.
- Recovery: закрытые env/Compose backups в production `.etran-ci/backups/`;
  стандартные release manifests и предыдущие images сохранены. Landing имеет
  before-auth tar index/CSS и image tag `before-auth-20261003`.
- Cleanup: временные credential JSON и transfer scripts удалены; локальный
  preview server, screenshot, test output и task logs очищены. Исходный файл
  пользователя с ключами не изменён; ключей в Git нет.
