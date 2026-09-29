# L4D-18F-DOCS-FIX-01 — реестр ограниченного выпуска

```yaml
prompt_id: L4D-18F-DOCS-FIX-01
registration_id: R-L4D-18F-DOCS-FIX-01-v1
output_handoff_id: H-L4D-18F-DOCS-v1
status: ACCEPTED
activation_scope: restricted
observed_at_utc: '2026-09-29T15:02:00Z'
candidate_path: l4desk-service/docs/handoffs/L4D-18F-DOCS-FIX-01-candidate.md
```

## Основание и граница результата

Пять входов `H-L4D-18A-SHARED-v1` — `H-L4D-18E-MB-v1` встречаются в
`contract-handoff.md` ровно по одному разу со статусом `ACCEPTED`, версия
каждого контракта `1.0.0`. Регистрация `R-L4D-18F-DOCS-FIX-01-v1`
существует ровно один раз и адресует эти входы 18F. Проверка точных
Git/raw SHA-256 десяти разрешённых data-only артефактов и трёх внешних
IoT-артефактов записана в [первичном gate](L4D-18F-DOCS-FIX-01-gate-2026-09-29.md).
Принятые блоки и их исторические файлы не изменялись.

Этот отчёт фиксирует **ограниченный технический выпуск** и текущие
production-версии. Пользователь отдельно принял риск юридических и глобальных
технических проверок, которые сейчас нельзя выполнить, и вывел их за границу
каскада. Это не устраняет пять отсутствующих исторических source hashes
tenant 1000 (sessions 476–480), не разрешает общую коммерческую активацию и
не даёт доказательства production backup/restore архива. Только независимый
контроллер может принять candidate и добавить `CLOSED_ACCEPTED` в журнал.

## Фактически развернутые версии

Снимок 2026-09-29 UTC: `docker inspect` показал `Running=true`,
`RestartCount=0` для всех перечисленных контейнеров. Развёртывание владельцев
идёт через immutable registry digest; соответствие revision label проверяется
deployer. Фронтенд остаётся отдельным статическим `dist`.

| Владелец | Source revision / package | Production artifact |
| --- | --- | --- |
| ProcessingBackend | `7da3e0762fbd1f28a8a479ad64f3af9a7cec2a10`; Alembic `028 (head)` | `dev-leo4-ru.cr.cloud.ru/etran/processingbackend@sha256:bc0e5b131b4e47ec43e0695e7f87d8cc3e158bb26756bc2fb88023aa76a75a00` |
| MenuBuilder backend | `434620d5367cb59999b136dfb41eaa0afbdcaa91` | `dev-leo4-ru.cr.cloud.ru/etran/menubuilder-backend@sha256:6622d49a03570b626c2206783f0c95fbda425d5971164ab12453ba75513356d7` |
| MenuBuilder frontend | `6ddc97af056db42e2b413fb404e9947fdbfa6e78` | `dev-leo4-ru.cr.cloud.ru/etran/menubuilder-frontend@sha256:95c10e7df95f0de188390eb13cfd3b8a3b7719c9d9afae490eb3522f193655ea`; установленный и отдаваемый nginx `index.html` SHA-256 `147d95bee5cda0b279600099b30c5c07002717101d9b36f69685a50f66bcd3c8` |
| IoT app1 | отдельный репозиторий, `bb661bb1f429f01d75a2a1d016c2dc8c2a7d5621` | `dev-leo4-ru.cr.cloud.ru/etran/app1@sha256:957b08ce2b3a6ec44514f99c05d55c4e3d1ff4f9f885b86a9da7eadf3bffa36d` |
| L4mcp | `7da3e0762fbd1f28a8a479ad64f3af9a7cec2a10` | `dev-leo4-ru.cr.cloud.ru/etran/l4mcp@sha256:7dcc28d19286d8ce99aedbdd4ccd2c4492ab0f7d7163416e9d9f947813069f4e` |
| Media ingress | `801186d8699f293c70bd1e4d65f4f7e9b91a9a7e` | `dev-leo4-ru.cr.cloud.ru/etran/l4media-ingress@sha256:1b9242b290d975e769acd3a35eb5d16a286f91e71b08356458643863c58b1cd7` |
| Media nginx | `7da3e0762fbd1f28a8a479ad64f3af9a7cec2a10` | `dev-leo4-ru.cr.cloud.ru/etran/l4media-nginx@sha256:fb137647e25bed749f1cf87cc0398eb8f072a900b7dfa124222cb9655fe911bd` |
| Janus | закреплённый собственный образ | `dev-leo4-ru.cr.cloud.ru/l4media-janus@sha256:93265665a92482ec1de2c9571a08d27c87b1dee06efc000f717ed42dfe2438ae` |
| l4tools | source `4b2713a0975bc0d8ac1142907378e196de767927`, package `1.9.0`, x86/x64 | Generic Artifact Registry `l4setup.exe` SHA-256 `ddf7d930f21b56a9b03a48226d94351ecd373c07fb4011fe8a47157daf129719`; скачанный после публикации пакет совпал |

Пакет `1.9.0` **опубликован**, но массовая установка на терминалы не
объявляется. На испытательном 1000009 адресно установлен x64 `l4capture`:
SHA-256 `257c1635e2e1840298eac5da52a663dc22b41dd55fb8fdb9fdafbc3d5e913d6d`;
`L4Superv` и `L4Con` были `Running` при повторной read-only проверке.

## Контрактная совместимость и smoke

- Browser → MenuBuilder BFF → app1 REST/WS → MQTT `srv/{SN}/ctl` → l4desk;
  presence/ACK/stream events возвращаются по `dev/{SN}/ctl`. Топики и wire
  команды не менялись. l4capture → proxy → ingress/Janus → WebRTC browser.
- Общий video lease переживает закрытие input WebSocket. В `app1` `bb661bb`
  исправлен прежний `ws_disconnect_timeout`: lease с активным
  `stream_instance_id` сохраняется до TTL/явного release/stop. Для lease без
  video 10-секундный таймаут остаётся. Локально 436 тестов, builder 410 passed
  и 26 skipped; адресные тесты и Ruff/Black прошли.
- 1000009, HD, 2026-09-29 14:54–14:55 UTC: браузер получил видео
  1920×1080, управление включилось; в 14:55:01 управление отключено, scope
  вернул 200, WS завершился `browser_disconnect`. Более 40 секунд после
  этого UI оставался «В эфире», HTTP keepalive возвращал 200, ingress
  показывал `receiving_fresh_media`, `fresh_rtp=true`, `rtp_idle_sec=0`.
  Штатный stop в 14:55:59 завершил поток, UI показал «Не запущена»; после
  stop ingress сообщил `active_media_sessions=0`, `route_exists=false`.
- На динамичном 1000009 старый Medium дал около 1,95 Мбит/с суммы ingress и
  Janus TX. Старый HD дал около 4,07 Мбит/с; профиль пакета 1.9.0 снижен до
  2100/2600 кбит/с и дал около 2,03–2,07 Мбит/с на двух интервалах. Это
  выборочные измерения для одного зрителя, не гарантированный потолок WAN.
  Кадр Medium — 1280×720, HD — 1920×1080, без изменения wire ID.
- Раздаваемый frontend содержит Medium/HD, подгонку кадра и локальную паузу
  F8. Browser smoke Medium/HD, смены F8 кнопкой и клавишей, start/stop и
  управления выполнен. Alt+F4/Win+D считаются закрытыми по указанию
  пользователя без дополнительного глубокого теста. Физический cursor-stay
  на всех моделях терминалов и x86/Windows 7 остаётся вне данного evidence.
- Все семь собственных server image и Janus в указанном снимке работали без
  restart; ProcessingBackend schema `028 (head)`. `/health` ingress вернул
  `status=ok`, `routes=0`, `active_media_sessions=0` после stop.

## Текущие flags и граница коммерции

Из эффективного `settings` работающего MenuBuilder прочитаны только
несекретные значения: `l4desk_enabled=false`, registration/billing/UI=false,
policy enforcement=false, shadow=true, IoT consumer=false, entitlement и
metering-close workers=false, YooKassa=false. Onboarding, financial core,
remote control и session orchestration=true. Тестовые allowlists пусты,
clock offset=0; ожидаемая schema=`028`. Значения `.env` и ключи не читались.

Саморегистрация, PIN/ownership, ledger, grace/block, mock payment,
reconciliation и синтетический archive restore описаны в принятых 17F/18E
evidence; этот docs-шаг не воспроизводил платёж и не запускал workers.
Пять source-hash gaps tenant 1000 остаются блокером общей коммерческой
активации. `mismatch=0` для всего production здесь не заявляется.

## Мониторинг, rollback и невключённые операции

| Проверка | Контракт наблюдения / текущий результат |
| --- | --- |
| Регистрация, PIN, online | Tenant/device ownership и PIN смотреть в авторизованном Hub; текущий 1000009 `online`, `svc_online`, services `Running`. Исторические адресные примеры — принятый 17F report; нового registration/PIN mutation здесь не было. |
| Видео, активные сессии, usage/event lag | BFF stream state + ingress `/health`/`/stats`, browser decoded frames, provider lease/status и UTC cursor. Для 1000009 fresh RTP/keepalive/stop проверены; задержка финансовых event/usage при выключенном consumer/worker не объявляется нулевой. |
| Ledger/reconciliation и YooKassa | В restricted production коммерческие flags и YooKassa выключены. Сверять double-entry, source hashes, pending/errors и grace/block до отдельной активации; историческое 17F evidence не является новым глобальным `mismatch=0`. |
| Archive checksum/retention | Синтетический dry-run/restore и checksum из принятого 17F evidence; текущий `/mnt/l4desk-archive` — обычный каталог, не отдельный mount, archive worker выключен. Production backup/restore, hot purge и schedule не заявляются. Три года — процедурная политика хранения, не проведённый трёхлетний тест. |

Rollback: остановить новые затронутые операции, вернуть предыдущий
проверенный immutable image соответствующего владельца через его release
override и `--no-deps --no-build`, проверить revision/health/image ID и
соседние сервисы. Схему `028` не понижать и ledger не править вручную.
Frontend возвращается атомарной публикацией предыдущего `dist`; native
агент — из сохранённой адресной копии после остановки `L4Superv`. Для
`app1` предыдущий digest сохраняется в deployer state; инцидентный `bb661bb`
предпочтителен для shared video lease. Регламенты —
`docs/ops_run-beta-ci-cd.md` и `docs/term_run-l4mcp-binary-update.md`.

## Исключения и передача контроллеру

Пользователь исключил из каскада проверки, которые здесь и сейчас требуют
юридической процедуры или глобального технического контура. Поэтому
production archive backup/restore и реальная hot-retention, трёхлетнее
ожидание, live YooKassa, Windows 7/POSReady x86 runtime и глобальное
восстановление пяти historical source hashes **не помечены PASS**. До их
отдельного решения запрещены archive purge и общая коммерческая активация.
Установщик 1.9.0 без Authenticode; риск принят владельцем отдельно.

Результат этого шага — опубликованный restricted release registry и
кандидат `H-L4D-18F-DOCS-v1` по `DETACHED_V1`. Независимый контроллер должен
сверить текущие digest, указанные исключения и границу активации; только он
добавляет `ACCEPTED`/`CLOSED_ACCEPTED` в append-only журнал.
