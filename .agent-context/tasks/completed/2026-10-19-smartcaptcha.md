# Переключение публичных форм на Yandex SmartCaptcha

Дата: 2026-10-19. Разрешены переключение настройки, деплой и push затронутых репозиториев.

- Владельцы: MenuBuilder — login/register/resend; L4Desk и Platerra — contact relay.
  Leo4 направляет обращения в Platerra с `topic=leo4`, собственной CAPTCHA нет.
- Использованы существующие ключи из окружения, без вывода значений и изменения ключей.
  Серверная проверка токенов и ограничения форм сохраняются.
- Портал переключён через `CAPTCHA_PROVIDER` в действующем production Compose.
  L4Desk/Platerra: защищённые env-файлы и Compose переключены на smartcaptcha;
  в исходных Compose закреплён `${CAPTCHA_PROVIDER:-smartcaptcha}`.
- Пересозданы только menubuilder-backend и два contact relay. Использованы прежние
  образы, без сборки, смены signer, миграций и пересоздания сайтов.
  Защищённые резервные копии конфигурации сохранены для отката.
- [MCP Ops Readiness: UNAVAILABLE]; штатный SSH с проверкой host key.
  Сервер сайтов: доступная RAM 2652 MiB, root 68%, load 0.06 перед изменением.

## Выполненная проверка

- Публичные captcha endpoints портала, L4Desk и Platerra: 200,
  `provider=smartcaptcha`, `Cache-Control: no-store`; портал возвращает site_key.
- Login/register/resend без токена: 400 до credentials/записи.
- Контактные формы с корректным Origin: пустой и неверный токены дают 400.
- Edge/Playwright: реальные Yandex widget и iframe загружены на login, register,
  L4Desk contact и Platerra contact с topic=leo4; ошибок JavaScript нет.
- Все три сайта отвечают HTTPS 200; оба contact relay healthy.
- Проверены Compose через `docker compose config`, сохранение прежних образов,
  diff whitespace и отсутствие секретов в изменяемых файлах.
- Backend/frontend код не менялся; pytest, Ruff, Pyright и npm build не запускались.

## Ограничения

Ручное прохождение задания CAPTCHA, успешный login/регистрация и отправка писем
не выполнялись. Браузерная проверка подтверждает загрузку реального виджета;
negative API проверки подтверждают отказ при отсутствующем/неверном токене.
