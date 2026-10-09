# L4Desk — публичный лендинг (l4desk.ru)

Материалы одностраничного лендинга L4Desk: **static site + nginx + certbot**
и небольшой внутренний Python 3.14 relay для формы обращений.

Источники текста и визуала:
- [редакционный бриф](materials/briefs/l4desk-landing-plan.md)
- [визуальный бриф](materials/briefs/l4desk-landing-visual-addendum.md)
- оригиналы скриншотов и иллюстраций — [materials/](materials/README.md); оптимизированные копии в `site/images/`

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
│   └── images/           # WebP-превью и полные версии скриншотов
├── docker/
│   ├── Dockerfile        # nginx:1.27-alpine + certbot
│   ├── nginx.conf        # HTTP(ACME+301) + HTTPS
│   └── entrypoint.sh     # self-signed → LE → renew loop
├── materials/            # оригиналы, брифы и исторические патчи
├── docker-compose.yml
└── README.md
```

## Цикл изменения контента

Публикуйте из чистого checkout подтверждённого коммита. Перед доставкой:
проверка Python relay (`ruff`, `pyright`, unit tests), JavaScript и браузерная
проверка адаптивности. В дистрибутив включайте только `site/`, `contact/`,
`docker/`, `docker-compose.yml` и `.dockerignore`. Материалы и секреты не
нужны на сервере; `.env.contact` сохраняется отдельно.

```powershell
# Из каталога l4desk-landing чистого checkout
 git -c core.autocrlf=false archive --format=tar.gz --output=landing-release.tar.gz HEAD:l4desk-landing site contact docker docker-compose.yml .dockerignore
 scp -i d:\.ssh\free-tier-cloud_ru landing-release.tar.gz user1@176.108.247.249:/home/user1/landing-release.tar.gz
 ssh -n -i d:\.ssh\free-tier-cloud_ru user1@176.108.247.249 "cd /home/user1/l4desk-landing && tar -xzf /home/user1/landing-release.tar.gz && sudo docker compose up -d --build --no-deps contact landing"
```

Получатель обращений задаётся в Compose: `CONTACT_OWNER_EMAIL`, по умолчанию
`info@l4desk.ru`. Для смены переопределите переменную в окружении Compose
или `.env`; она имеет приоритет над значением внутри `.env.contact`.
Shell-скрипты закреплены с LF через `.gitattributes`; `core.autocrlf=false`
при упаковке защищает все текстовые файлы от преобразований Windows.
После публикации проверьте оба контейнера, `/healthz`, содержание страницы,
все файлы статических ресурсов и фактического получателя внутри `contact`.


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

| Раздел | Содержание |
|---|---|
| Шапка | Возможности, файлы, AI/API, подключение; написать, вход, регистрация |
| Hero | «Удалённое управление. В браузере», схема браузерного рабочего места |
| `#how` / `#browser` | Четыре компактные карточки: экран, консоль, файлы, L4MCP |
| `#files` | «Проводник для всего парка. Прямо в браузере», навигация и обмен файлами |
| `#ai` / `#roadmap` | L4MCP, API и сворачиваемые направления развития |
| `#connect` | Три шага от регистрации до рабочего сеанса |
| `#trust` | Подлинность устройств, авторизация и контроль сеансов |
| `#demo` | Форма обращения с CAPTCHA и выбором темы, включая файловый менеджер |

**Где править:** тексты и структура — `site/index.html`, оформление —
`site/css/styles.css`, меню, увеличение скриншотов и форма — `site/js/main.js`.

Превью скриншотов имеют размеры 480 и 960 px и выбираются через `srcset`.
Полные WebP сохраняют исходное разрешение и открываются по нажатию.
Без JavaScript ссылка ведёт прямо к изображению. Кадр L4MCP отмечен как
демонстрационный пример. Изображения в этой задаче не ретушировались.

Для локальной проверки: `python -m http.server 8765 --bind 127.0.0.1 --directory site`.
SmartCaptcha разрешает работу только на настроенных доменах: на localhost
проверять форму с тестовым CAPTCHA-ответом и перехватом `/api/contact`, без отправки писем.

## Важное

- **Правки только в local-репозитории**, затем scp + rebuild (принцип single source of truth).
- Порты **80/443** заняты только этим контейнером; `menubuilder-frontend` остаётся на :3000.
- DNS `l4desk.ru` и `www.l4desk.ru` проверен: оба адреса ведут на `176.108.247.249`.
- Форма обращений подключена через внутренний relay; см. CONTACT-FLOW.md.

## Обращения и изменения 2026-10-03

Форма отправляет сообщение владельцу и email-подтверждение посетителю через
существующий sender. Используется серверная SmartCaptcha с отказом при ошибке
проверки и ограничениями частоты. Контакт для обращений и публичный email — `info@l4desk.ru`. Настройка .env.contact, ограничения,
проверки и будущая политика sender: [CONTACT-FLOW.md](CONTACT-FLOW.md).

AI-иллюстрация предыдущего варианта сохранена в `materials/illustrations`;
на текущем первом экране используется схема браузерного рабочего места.
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

## Последняя проверенная публикация

[Отчёт о публикации и данные отката](materials/history/deployment-20261009.md).
