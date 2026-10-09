# Публикация L4Desk: браузерное рабочее место

## Область работ

Пользователь подтвердил публикацию согласованного дизайна, перенос материалов,
коммит и push. Затронуты только лендинг, его relay и ссылки на перемещённые
материалы. Архитектура l4desk-service и остальные сервисы не менялись.

## Источник и доставка

- Код: `cbea547ba1b6128af95ca6755f79e65a7010cc94`, опубликован в origin/main.
- Хост: 176.108.247.249, проект /home/user1/l4desk-landing.
- Сборка из чистого sparse worktree; пакет — git archive с core.autocrlf=false.
- SHA-256 пакета: `682f9755e962af0b4c5f47620cc5bcc898f17778a0ba3bed16c623c24fce3e70`.
- Landing image: `sha256:375b2eb3d2fe28dfa395e14bf7d53f27304e7770fad287a6d285f22a4e04bbcf`.
- Contact image: `sha256:eef1cccc365d02c23b1892292f12941a51dab37ce6378e84fa5a7be6368acaf2`.
- Пересозданы только contact и landing; сертификаты и секреты env_file сохранены.
- Эффективный CONTACT_OWNER_EMAIL внутри contact: info@l4desk.ru.

## Проверки

- MCP Ops Readiness: UNAVAILABLE; выполнена проверка через SSH.
- Preflight: доступная RAM 2756 MiB, диск 61%, load average 0.01/0.02/0.00.
- Ruff check/format, Pyright contact: PASS; 9 unit tests: PASS.
- Все темы HTML-формы приняты серверным validate_form; JavaScript syntax: PASS.
- nginx -t: PASS; оба контейнера healthy.
- HTTPS и /healthz на l4desk.ru и www.l4desk.ru: 200.
- SHA-256 всех 29 файлов site, скачанных по HTTPS, совпал с исходным коммитом.
- Hash index.html, CSS, JS, entrypoint и relay внутри контейнеров совпал с Git.
- Browser widths 320/390/768/1024/1440: без горизонтального переполнения.
- Полный скриншот открылся с исходной шириной 1586 px; Escape возвращает прокрутку.
- SmartCaptcha отобразила slider для l4desk.ru; пустой запрос с правильным Origin
  отклонён 400, без Origin — 403. Письма посетителям не отправлялись.

На первом старте LF был преобразован Windows-упаковкой в CRLF, shell entrypoint
не стартовал. Предыдущий образ временно восстановлен; итоговый пакет проверен
на LF и повторно опубликован. .gitattributes и инструкция упаковки закрепляют
формат LF для следующих выпусков.

В iframe SmartCaptcha наблюдались provider React hydration errors 418/423;
виджет после восстановления отрисовал slider. Реальная доставка письма на новый
ящик и полное прохождение CAPTCHA пользователем не проверялись.

## Откат и материалы

На сервере сохранены архив l4desk-landing-before-cbea547.tar.gz и образы
l4desk-landing:rollback-cbea547 / l4desk-contact:rollback-cbea547.
Архив не содержит .env.contact. Для отката восстановить исходники из архива,
вернуть эти образы под штатные latest-теги и пересоздать оба сервиса без build.

Оригиналы, брифы и патч собраны в materials; SHA-256 изображений при переносе
сверен. Планы развития — редакционные материалы, не подтверждение реализации.
Локальные preview-кадры сохранены в игнорируемой .preview для просмотра.
