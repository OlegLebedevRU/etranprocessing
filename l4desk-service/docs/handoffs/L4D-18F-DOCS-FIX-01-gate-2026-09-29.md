# 18F corrective: промежуточный gate перед итоговым реестром

```yaml
prompt_id: L4D-18F-DOCS-FIX-01
registration_id: R-L4D-18F-DOCS-FIX-01-v1
status: BLOCKED_DEPLOY
observed_at_utc: '2026-09-28T22:05:42Z'
scope: l4desk-service documentation and read-only production inventory
cascade_closed: false
```

## Входной контракт

Принятый контроллером 18E block из `72647cb` перенесён в `main` как
`dfc7fe7`; Git-байты самого блока совпали. Регистрация 18F опубликована
в `b826477`. Все пять входов 18A–18E уникальны, имеют `ACCEPTED`, версии
`1.0.0`, совпадающие producer commits и точные пары списков artifact paths /
SHA-256. Десять выбранных в регистрации data-only документов проверены по
Git/raw digest; три IoT-файла сверены по указанному commit в отдельном
репозитории. Старые handoff-блоки не редактировались. Это доказательство
допуска к аудиту, не приёмка выпуска 18F.

## Текущий production snapshot

Хост `87.242.100.34`: root disk 57%, available RAM 2058 MiB, load average
0.09/0.18/0.22. Docker inspect показал running и restart count 0 для
перечисленных контейнеров. HTTP `/api/health` ProcessingBackend и
`/openapi.json` MenuBuilder вернули 200; Alembic `028 (head)`.

| Сервис | Фактически запущенный образ / источник | Вывод для выпуска |
| --- | --- | --- |
| MenuBuilder backend | `dev-leo4-ru.cr.cloud.ru/etran/menubuilder-backend@sha256:0a67f0ffd0326c2ed968b3e42d2cdd7728b4dd4df5c71582e059c061edbf5c89`; OCI revision `2dd1473b37b545ae8caa745f81ff2d6c33bb426f` | Registry digest и базовый Compose без build проверены; это post-18E пакет. |
| ProcessingBackend | локальный `user1-processing-backend`, image ID `sha256:70e9a060b0ae65403408b6a14187afe87fa3e0d065bc612ac928998508515e33`; label revision `551f5c7998bc20f5361cbdf10709e6d0b1c7f995` | Не развёрнут pull из registry. |
| IoT app1 | локальный `user1-app1`, image ID `sha256:d2540c3e7c5da54077a0543491a6b0bb0782244c9837d7430b218bbf730568ba`; label revision `35f1fce054e388a2206af45f1416b9572b0614a9` | Не развёрнут pull из registry; другой репозиторий и владелец. |
| Media ingress | `dev-leo4-ru.cr.cloud.ru/l4media-ingress@sha256:3e0e0341f37702bedef06ca87538c494c7e6bdf8f87152e6d5901e4d792322fd` | Registry digest подтверждён. |
| Janus | `dev-leo4-ru.cr.cloud.ru/l4media-janus@sha256:93265665a92482ec1de2c9571a08d27c87b1dee06efc000f717ed42dfe2438ae` | Registry digest подтверждён; пересборка только по прямой команде пользователя. |
| L4mcp | локальный `user1-l4mcp:d4712c9`, image ID `sha256:a1180dbe90aef5c41744f9b9c4df299c96c772f8865abada3260d1084744295a` | Тоже остаётся локальным образом, если требование registry относится ко всем L4D-сервисам. |
| Media nginx | локальный `l4media-nginx`, image ID `sha256:0af718db7f617e063c1ae465d3bae1e080bf512af3e78c7c7e8a892674bd8e07` | Уточнить границу требования registry для вспомогательного proxy. |

Ingress `/health`: `status=ok`, `routes=0`, `active_media_sessions=16`.
Без контрактного объяснения счётчика это наблюдение не трактуется как утечка
или как норма. Текущего live video start/stop на новом MenuBuilder digest
этот docs-аудит ещё не подтвердил. Эффективные коммерческие feature flags
не выведены безопасным контрактным endpoint; исторические значения 18E
не подставляются как текущие.

## Что блокирует итоговый `CLOSED_ACCEPTED`

1. Требование пользователя о развёртывании крупных серверных артефактов
   строго pull из registry ещё не выполнено для ProcessingBackend и IoT app1;
   границу для L4mcp и media nginx нужно закрепить в release matrix. Это
   отдельные owner steps, не правки внутри 18F docs.
2. Обязательный свежий smoke версии MenuBuilder `2dd1473` ограничен HTTP
   200 и чтением страницы/терминалов; движущееся видео и штатный stop на
   этом image в текущем 18F evidence не проверены.
3. В принятом 18E известны пять исторических отсутствующих source hashes
   tenant 1000 (sessions 476–480). Общая коммерческая активация остаётся
   заблокированной; нельзя приписать ей нулевой reconciliation mismatch.
4. Для эксплуатационного реестра нужно уточнить смысл media-счётчика 16 при
   нуле активных RTP routes и получить проверяемое текущее состояние flags,
   archive backup/restore и hot retention. Трёхлетний срок — процедурная
   политика, а не обязательный трёхлетний технический тест.

18F output candidate и запись `CLOSED_ACCEPTED` не создавались. После
адресных owner-выпусков нужно повторить текущую матрицу, короткий smoke и
независимый controller review итогового report/candidate.
