# CAPTCHA providers

На 2026-10-19 портал, L4Desk и Platerra используют `smartcaptcha`.
Compose L4Desk задаёт `${CAPTCHA_PROVIDER:-smartcaptcha}`; ключи остаются
в защищённых env-файлах. После изменения провайдера пересоздаётся только
backend/relay с прежним образом. [Проверки переключения](../.agent-context/tasks/completed/2026-10-19-smartcaptcha.md).

`CAPTCHA_PROVIDER=smartcaptcha|native|off` выбирает провайдера. Пустое значение
MenuBuilder сохраняет прежнюю семантику `SMARTCAPTCHA_ENABLED`; relay по умолчанию
использует SmartCaptcha. Неизвестный провайдер закрывает проверку.

MenuBuilder: `GET /api/auth/captcha` возвращает `provider`, `enabled`, `site_key`
и для native — `challenge`. Контактные формы: `GET /api/captcha/challenge`
возвращает `provider` и native `challenge`. Ответы не кэшируются.

Challenge содержит `question`, `token`, `expires_at`. V1 token — base64url JSON
`{v:1,a,b,exp}` без padding, точка и HMAC-SHA256 в hex по точной строке base64url.
Ответ в существующем поле CAPTCHA: `<token>.<целое число>`. Числа от 1 до 20,
срок 600 секунд. Подпись сравнивается за постоянное время; неправильная подпись,
формат, ответ и истёкший срок отклоняются с 400. Отсутствие
`CAPTCHA_NATIVE_SECRET` возвращает 503 при native. Секрет — только защищённый
env-файл, отдельный случайный секрет каждого сервиса; не JWT signing key.

Native — арифметическая проверка для резервного режима, слабее SmartCaptcha
против автоматизации. Stateless challenge допускает повторное использование
до истечения срока; существующие ограничения частоты отправки сохраняются.
Login/register/resend проверяют CAPTCHA перед credentials и записью.
Контактные формы сохраняют проверку полей, consent, лимиты и идемпотентность.

Frontend выбирает провайдера по серверному ответу; при native не загружает Yandex.
Повтор и неудачная отправка получают свежий challenge. `off` отключает CAPTCHA
только при явном выборе. Для отката выставить `CAPTCHA_PROVIDER=smartcaptcha`,
сохранить прежние Yandex env-настройки и пересоздать backend/relay для перечитывания
окружения. Frontend пересобирать для переключения не нужно.

MenuBuilder выпускается вместе с JWT signer на production; L4Desk/Platerra —
своими Compose на сервере сайтов. Leo4 сохраняет CTA на
`/contacts/?topic=leo4#message`; собственная CAPTCHA ему не требуется.

`deploy/Enable-NativeCaptcha.py` генерирует секреты на сервере и сохраняет
защищённые резервные копии env-файлов без вывода содержимого.
