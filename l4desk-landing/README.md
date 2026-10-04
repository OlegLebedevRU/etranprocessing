# L4Desk — публичный лендинг (l4desk.ru)

Материалы одностраничного лендинга L4Desk: **static site + nginx + certbot**
и небольшой внутренний Python 3.14 relay для формы обращений.

Источники текста и визуала:
- `l4desk-service/docs/l4desk-landing-plan.md`
- `l4desk-service/docs/l4desk-landing-visual-addendum.md`
- скриншоты `l4desk-service/docs/l4desk-landing-*.png`

## Где что лежит

| Где | Путь |
|---|---|
| **Исходники (local)** | `D:\repo\platerra\Public\etranprocessing\l4desk-landing\` |
| **Деплой (сервер)** | `user1@176.108.247.249:/home/user1/l4desk-landing` |
| **SSH** | `ssh -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249` |
| **Контейнер** | `l4desk-landing` (nginx + certbot + статика, один образ) |
| **Сайт** | https://l4desk.ru · https://www.l4desk.ru |

## Структура проекта

```
l4desk-landing/
├── site/                 # статика (править здесь)
│   ├── index.html        # весь лендинг
│   ├── css/styles.css
│   ├── js/main.js
│   ├── favicon.svg
│   └── images/*.png      # 6 скриншотов UI
├── docker/
│   ├── Dockerfile        # nginx:1.27-alpine + certbot
│   ├── nginx.conf        # HTTP(ACME+301) + HTTPS
│   └── entrypoint.sh     # self-signed → LE → renew loop
├── docker-compose.yml
└── README.md
```

## Цикл изменения контента

```powershell
# 1. правка локально
notepad D:\repo\platerra\Public\etranprocessing\l4desk-landing\site\index.html

# 2. копия на сервер
scp -i d:\.ssh\free-tier-cloud_ru -r `
  D:\repo\platerra\Public\etranprocessing\l4desk-landing `
  user1@176.108.247.249:/home/user1/l4desk-landing

# 3. пересборка + рестарт (сертификаты в volume — не теряются)
ssh -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 `
  "cd /home/user1/l4desk-landing && sudo docker compose up -d --build"
```

Статика (`site/`) копируется внутрь Docker-образа и не подключена через bind mount.
После её обновления выполните `sudo docker compose up -d --build --no-deps landing`.
Один `restart` не обновляет содержимое сайта.

## Типовые операции

| Действие | Команда на сервере |
|---|---|
| Статус | `sudo docker ps --filter name=l4desk-landing` |
| Логи (nginx + certbot) | `sudo docker logs -f l4desk-landing` |
| Проверка health | `curl http://127.0.0.1/healthz` |
| Перевыпуск серта вручную | `sudo docker exec l4desk-landing certbot renew --webroot -w /var/www/certbot` |
| Остановка | `sudo docker compose down` |
| Полный снос (вкл. серты) | `sudo docker compose down -v` |
| Смотреть сертификат | `sudo docker exec l4desk-landing openssl x509 -in /etc/letsencrypt/live/l4desk.ru/fullchain.pem -noout -subject -dates -ext subjectAltName` |

## Переменные окружения

| Переменная | Смысл | Значение |
|---|---|---|
| `DOMAIN` | базовый домен | `l4desk.ru` |
| `EMAIL` | контакт для Let's Encrypt | пусто (без e-mail, `--register-unsafely-without-email`) |
| `STAGING=1` | staging Let's Encrypt (тест без лимитов) | `0` |
| `TZ` | часовой пояс | `UTC` |

## Поведение сертификатов

| Шаг | Что происходит |
| --- | --- |
| Старт | Если нет сертификата — self-signed, чтобы nginx поднялся |
| ACME | `certbot certonly --webroot` (HTTP-01 через `/.well-known/acme-challenge/`) |
| Успех | Копия в `/etc/nginx/ssl/live/`, флаг `enabled`, HTTP→HTTPS redirect, reload |
| Обновление | Цикл `certbot renew` каждые 12 часов + reload |

Сертификаты и ACME-аккаунт персистятся в named volumes
`letsencrypt` и `certbot-webroot` — переживают `compose up --build`.

Текущий выпущенный сертификат: Let's Encrypt, SAN `l4desk.ru` + `www.l4desk.ru`,
действует до **2026-12-29**.

## Локальная проверка

```powershell
# только статика
python -m http.server 8080 --directory site

# полный контейнер (HTTP-only, без сертификата)
$env:STAGING="1"; $env:EMAIL="you@example.com"
docker compose up --build
# http://localhost/
```

## Краткий справочник разделов лендинга

| # | Раздел / `id` | Суть | CTA / визуал |
|---|---|---|---|
| — | **Шапка** | Якоря + кнопка «О preview» | sticky |
| 1 | **Hero** (`#top`) | «Не гадайте, что с устройством. Откройте и проверьте.» | схема: выбор → проверка → решение |
| 2 | **Проблема** (`#problem`) | Сбой виден пользователю раньше инженера | — |
| — | **Реальный интерфейс** (`#evidence`) | Консоль, выбранное устройство, L4MCP | крупные исходные кадры с увеличением |
| 3 | **Три роли** (`#how`) | Табы **Оператор / AI-агент / Моя система** | скриншоты + код API |
| 4 | **Браузер** (`#browser`) | ПК и смартфон; «Вижу → управляю» | dual-screen + control-кадр |
| 5 | **Подключение** (`#connect`) | 5 шагов: создать → PIN → `l4setup` → online → сеанс | нумерованные карточки |
| 6 | **AI & API** (`#ai`) | L4MCP: команды и сценарии на удалённых машинах vs API | 2 карточки + timeline |
| 7 | **Доверие** (`#trust`) | Схема: устройства → L4Desk → операторы / агенты | 2 границы (mTLS, JWT) |
| 8 | **Роли** (`#roles`) | Продуктолог / саппорт / DIY / DevOps | 4 карточки «его вопрос → ценность» |
| 9 | **Preview** (`#demo`) | «Есть задача для удалённого устройства?» | выбор одного сценария; форма обращения с CAPTCHA |
| — | **Футер** | Копирайт, экспериментальный preview | — |

**Где править:** заголовки секций — `site/index.html` (теги `<h2>`),
визуальные хуки 1–4 — CSS в `site/css/styles.css`
(`hook-dual`, `control-frame`, `hook-timeline`, `trust-flow`),
логика табов, просмотра изображений и формы — `site/js/main.js`.

## Важное

- **Правки только в local-репозитории**, затем scp + rebuild (принцип single source of truth).
- Порты **80/443** заняты только этим контейнером; `menubuilder-frontend` остаётся на :3000.
- DNS: `www` → `176.108.247.249` корректен; apex `l4desk.ru` в момент деплоя указывал
  на `95.163.244.138` — **выровнять A-запись** на `176.108.247.249`.
- Форма обращений подключена через внутренний relay; см. CONTACT-FLOW.md.

## Обращения и изменения 2026-10-03

Форма отправляет сообщение владельцу и email-подтверждение посетителю через
существующий sender. Используется серверная SmartCaptcha с отказом при ошибке
проверки и ограничениями частоты. Контакт владельца временно заменён по решению
пользователя после теста доставки. Настройка .env.contact, ограничения,
проверки и будущая политика sender: [CONTACT-FLOW.md](CONTACT-FLOW.md).

AI-иллюстрация подключена в WebP с адаптивными размерами и отложенной загрузкой.
Описание L4MCP дополнено историей событий; планы развития вынесены отдельно.

## Редакционные границы

Текст соблюдён по плану: без вымышленных цифр, отзывов и SLA;
«без отдельного клиента на рабочем месте оператора»;
результат действия определяется отдельным событием устройства там, где оно доступно.
**L4MCP позиционируется как harness удалённого управления для AI-агента** —
атомарные команды и целые сценарии на удалённых компьютерах.
Не использовать формулировки, умаляющие L4MCP («ограниченный набор»,
«не универсальный доступ» и т.п.).
Скриншоты — адресная проверка устройства 1000009, не общая коммерческая доступность.
Тональность: ясный, уверенный, конкретный — без магии и неподтверждённых обещаний.
