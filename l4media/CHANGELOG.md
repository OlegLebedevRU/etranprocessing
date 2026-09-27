# Changelog — l4media

## Unreleased

### Added

- Janus собирается из закреплённых исходников upstream; ingress и Janus проходят изолированные проверки на сборочном хосте перед публикацией в registry. Production использует проверенные digest образов; инструкции и скрипты находятся в `deploy/`.
- Первая опубликованная пара зафиксирована как baseline. Janus пересобирается только по отдельной команде пользователя; обычная сборка меняет только ingress. Резервная локальная сборка ingress также обязательно публикуется в registry перед pull на production.

- Перенесены в репозиторий уже работавшие на сервере Redis-сохранение медиасессий и маршрутов, а также проверка существующего Janus mountpoint перед повторным использованием.
- Авторизованный сервисный `POST /api/v1/media/sessions/renew` продлевает TTL единственной активной медиасессии по SN после подтверждения lease keepalive.
- Авторизованный сервисный `POST /api/v1/media/sessions/stop` штатно удаляет активную медиасессию по SN после остановки потока.

### Fixed

- Management credentials must be explicitly configured in private environment files; missing or invalid credentials prevent startup. Janus renders its private admin credential at startup instead of storing it in the image or tracked configuration.
- Watchdog отсчитывает TTL от последнего успешного продления; Redis-запись получает тот же срок и восстанавливает его после рестарта ingress. При прекращении keepalive маршрут и mountpoint удаляются автоматически.
