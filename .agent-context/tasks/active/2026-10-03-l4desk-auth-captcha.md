# l4desk.ru → MenuBuilder: вход, регистрация, SmartCaptcha

## Task intake

- Scope: HTML/CSS существующего лендинга; MenuBuilder auth backend и frontend.
- Owner: MenuBuilder владеет auth/registration; landing — ссылками перехода.
- Flow: landing → HTTPS portal → CAPTCHA widget → MenuBuilder → Yandex validate
  → существующий credential/registration service.
- Инварианты: секреты только env; CAPTCHA до auth/registration mutation;
  session refresh, JWT, tenant policy, billing и shared DB не меняются.
- Миграции не требуются. MQTT/native/legacy не затронуты.
- Source: HEAD a5ff1f2; изменения working tree; отдельный landing checkout
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
- Playwright Edge mocked CAPTCHA: 2 PASS (нельзя submit без токена; failed login
  сбрасывает токен; configuration failure блокирует регистрацию и позволяет retry).
- Existing key pair извлечена из разрешённого локального неотслеживаемого файла
  без сохранения в tracked files; production env настроен, защищённый backup есть.
- Дополнение пользователя: отдельная колонка «Номер терминала» в подписках
  показывает `Terminal.device_id` (например 1000009), а не внутренний PK.
  API добавляет `device_id`; выбор оплаты по `terminal_id` сохраняется.

## Следующий шаг

- Build/publish MenuBuilder backend/frontend на 176 → production pull.
- Landing rebuild только landing service; contact/cert volumes не пересоздавать.
- Read-only public smoke и live widget browser. Успешный human challenge и
  реальный вход/письмо не считать доказанными по mocked tests.
