# L4D-17F-DOCS-FIX-01 — black-box приёмка

Статус: `BLOCKED_TESTS`. Scope: `l4desk-service`.
Текущие runtime repositories, приватные env и DB не открывались.

## Task intake

- Владелец: контроллер каскада L4Desk; предмет — общий E2E через deployed
  интерфейсы после принятого 17E FIX.
- Producer → consumer: документальные экспорты 17A–C, media 17D и 17E →
  этот black-box gate → `L4D-18A-SHARED` при полной приёмке.
- Инварианты: tenant ownership и mutual exclusion; одна проводка на
  идемпотентный источник; debit=credit; подтверждённые периоды учтены,
  недоказанный хвост прощён; без реальных списаний или массовых писем.
- Критерий: текущие версии/флаги/rollback и сценарии исходного 17F
  подтверждены разрешёнными интерфейсами; каждый недоступный обязательный
  сценарий возвращает адресный blocker.

## Contract gate — 2026-09-27

Prompt/registration: `L4D-17F-DOCS-FIX-01` /
`R-L4D-17F-DOCS-FIX-01-v1`, опубликованы в
`5ea654a6234a0c60917a9cd2f0d1f0b1f15fd0ab`; удалённый ref совпал.
Три прямых входа имеют ровно один `ACCEPTED`, version `1.0.0`,
`DOCS_PUBLISHED`, непустые совместимые артефакты и отсутствие отзыва.
Адресный допуск нужен только для media export 17D. Исторический
`H-L4D-17E-MB-FIX-01-v1` принят и проверен только как sequence gate.

| Handoff | Version | Producer commit | Artifact path | Git/raw SHA-256 |
|---|---|---|---|---|
| `H-L4D-17ABC-CONTRACT-01-v1` | `1.0.0` | `9fe68c32391e0310fc857c098be7cf5e2f43cfdf` | `l4desk-service/docs/prompts/contracts/acceptance-17abc-v1/contract.md` | `07237f0d8099dd4a758becdfbecd2b8439219042313b23fecf71df031b8dd263` |
| `H-L4D-17ABC-CONTRACT-01-v1` | `1.0.0` | `9fe68c32391e0310fc857c098be7cf5e2f43cfdf` | `l4desk-service/docs/prompts/contracts/acceptance-17abc-v1/verification.md` | `0f9745abd4082f855fa640803293f4879c3d111cb9bb9bf75603f3ba1784c1e6` |
| `H-L4D-17D-MEDIA-CONTRACT-01-v1` | `1.0.0` | `77a666f17cc5cb154b6a46a8a119ed0594c087c0` | `l4desk-service/docs/prompts/contracts/media-17d-v1/contract.md` | `098fffbf11f8c404354d3680192d2f20e967d69ce79b5cd98798309cc8c64c75` |
| `H-L4D-17D-MEDIA-CONTRACT-01-v1` | `1.0.0` | `77a666f17cc5cb154b6a46a8a119ed0594c087c0` | `l4desk-service/docs/prompts/contracts/media-17d-v1/verification.md` | `56d68275e830fdb60d57f712ad338426d42b5d1431c91c06446eb1f4cf66a4ab` |
| `H-L4D-17E-CONTRACT-01-v1` | `1.0.0` | `937fd39c461ef77ef8d5400879ea6933d6c189be` | `l4desk-service/docs/prompts/contracts/acceptance-17e-v1/contract.md` | `0f70346eb3348b821ef3930da345a023043e4cf25d29870f815e8dd3976ad010` |
| `H-L4D-17E-CONTRACT-01-v1` | `1.0.0` | `937fd39c461ef77ef8d5400879ea6933d6c189be` | `l4desk-service/docs/prompts/contracts/acceptance-17e-v1/verification.md` | `d5cb97764098e894e21e593edd72972b9e286b8c2a2fce6ce3707a02fa2de0c4` |

Итог gate: `PASS`, 6/6 Git/raw SHA-256. Старые producer handoff
в экспортах являются provenance, а не дополнительными входами.

## Текущая black-box проверка

Время ниже — UTC 2026-09-27. Проверки выполнялись через обычный сайт
`https://dev.leo4.ru:3000` в `playwright-cli` и разрешённые read-only
HTTP интерфейсы контейнеров через SSH на штатный хост. Для изолированного
backend использован локальный SSH-туннель. Пароль и bearer-токены в
отслеживаемые файлы проекта и отчёт не записывались. Тестовые платежи и письма
не создавались, server files, DB и runtime repositories не открывались.

| Критерий | Наблюдение сейчас | Вывод |
|---|---|---|
| Tenant/ownership и Agent | Обычный сайт: `/api/auth/me` 200, `org_id=1000`, `role_id=5`, `user_id=654`; список устройств 200, ровно один терминал 1000005; он online, l4desk agent online, экран 1920×1080 доступен. `control/status` и `stream/state` 200. | PASS для существующего владельца и терминала; новый registration/PIN и старый пользователь не повторялись. |
| Видео, 10 минут | Старт около 13:01:30: `control/lease` 201, `session` 200, `stream/start` 200. В 13:11:35 видеоплеер: `currentTime=602` с, `readyState=4`, 1920×1080, 7332 кадра, 181 dropped; `stream/state` 200/running, `fresh_rtp=true`, RTP=78517, bytes=81628312. Зафиксирован рост RTP от 4865 и bytes от 5009628. Ни одного HTTP 500 среди 161 запросов вкладки на момент 13:09. | PASS для длительности 10 минут, без наблюдаемой заморозки или ошибки сигнализации. Видео/аудио качество субъективно не измерялось. |
| Монопольность и retry | Пока шло видео, отдельная вкладка консоли получила `POST control/lease` 409 и UI «Конфликт активной сессии»; видео продолжалось. После остановки retry консоли получил lease, MQTT/WS online; команда `ver` вернула Windows 10.0.18363.1556 и exit 0. | PASS для video→console conflict/retry и одного диагностического RPC. |
| Stop/restart | Штатный stop видео около 13:11:47 без 500. В 13:12:05 `stream.state=stopped`, `lease.active=false`, `remote-sessions/.../active=false`; media `/routes=[]`. После отключения консоли в 13:13:46 аренда и активная сессия снова false. Повторный старт видео в 13:13:59 дал 1920×1080, `readyState=4`, `fresh_rtp=true`; повторный stop в 13:14:58; в 13:15:09 active=false, lease=false. | PASS для штатного stop и повторного запуска. Повторный stop одного и того же `session_id` отдельно не проверялся. |
| Usage в открытой сессии | Production `GET /api/v1/finance/usage` 200: `source_seconds` 1232 → 1356 → 1544 → 1721 после первого stop → 1822 после консоли и второго видео. `free_seconds=source_seconds`, `billable_seconds=0`; текущий баланс 1000 коп. Секунды прирастали порциями до stop. | PASS для наблюдаемого порционного начисления и отсутствия платного списания; исходный baseline до первого старта отсутствует, точную погрешность 10-минутной сессии доказать нельзя. |
| Изолированный mock/финансы | Test backend `/api/auth/register/status` 200/enabled=true; production 200/enabled=false. Test API: login 200, entitlement 200 (`free_quota_seconds=600`, `today_usage_seconds=1481`, `can_start_sessions=true`, `balance_kopecks=1000`), balance/usage/payments/transactions 200. Ровно один существующий mock payment `id=3`, status=succeeded, amount=1000 коп., `ledger_transaction_id=5`; одна posted transaction `id=5`, debit=credit=1000 коп. | PASS только для текущего read-only состояния. Поскольку оплаченный цикл уже активен, это не повторная проверка refusal/grace/webhook idempotency. |
| Media route | Во время видео `/health` 200, routes=1; `/stats` 200, `receiving_fresh_media`, `fresh_rtp=true`, `route_exists=true`, `unrouted_packets=0`. После stop `/routes=[]`, `fresh_rtp=false`. `active_media_sessions` остался 21, а соединение с агентом осталось connected без маршрута. | PASS для освобождения RTP-маршрута; счётчик `active_media_sessions` означает сохранённые transport connections и не интерпретируется как активные viewer sessions. |

### Текущий deployment fingerprint

`sudo docker inspect` показал все пять контейнеров `running`, restart count 0.
Это **точные image ID на момент проверки**, но Docker labels не содержат
source revision, поэтому тождество source commit не утверждается.

| Компонент | Image ID / API |
|---|---|
| Agent 1000005 | online, 1920×1080; исполняемый файл и его version/hash через deployed API не выдаются |
| ProcessingBackend | `sha256:e2194a9b3341ec24d5b0d176d825f6879eff6d7c074bb107276473c28936e511`; `/api/health` 200, OpenAPI version 0.1.0, SHA-256 `2a5b579986b5476e0d17bdaeed52a02eb2f2424e73098e483a2e757ed038637a` |
| IoT app1 | `sha256:29aa88169ab8b051705044f0feb1ad8b4d5af5827bcef5294b437c7e29b9c431`; OpenAPI version 0.2.1, SHA-256 `5174ef63b22db69f484ece759405f0afc151e586cb9e0a30b1b311b3101aefbc` |
| Media ingress | `sha256:f201ff382eaa9ca127681cee16314e86deffa9d2441f6c293f3e10ff57e61c9f`; `/health` 200 |
| MenuBuilder production + isolated | оба `sha256:c70a11d88eef2697898d936278d4f8ce8066bc8cd0fa951d7ece9efda9b49e8e`; `/docs` 200, OpenAPI version 0.2.0, SHA-256 `620ea1655c81da48ca4917a996ab3db896eb0194d2ad15120ce33be08c25ccf1` |

Исторические exports сообщали 17B schema 027, прежние image и 17E
rollback image `sha256:08b6d35b...`. Текущая schema revision, Agent
binary hash, source revisions, полный набор feature flags и пригодность
rollback **не подтверждены** разрешёнными black-box интерфейсами. Нельзя
подменять их совпадением отдельных image ID или историческим экспортом.

## Непройденные обязательные критерии

1. Новая self-registration/email confirmation/tenant/terminal/PIN и старый
   пользователь не повторены сейчас. Имеется лишь историческая информация
   о test04/test05 и текущая ownership проверка. У 17F нет заранее
   одобренного нового адреса и lifecycle новой тестовой учётной записи;
   prompt запрещает создавать tenant/credentials по умолчанию.
2. Текущий tenant 1000 уже оплачен на 1000 коп. Поэтому отказ при исчерпании
   бесплатных 600 секунд, grace/block/stop и идемпотентные webhook/poll
   невозможно доказать новым безопасным циклом без отдельного тестового
   сценария. Исторический mock payment и ledger не равны новой E2E проверке.
   Точное округление в пользу потребителя и повторная доставка source event
   также не доказаны read-only ответами.
3. Для Hub correlation/mismatch и archive manifest/restore dry-run у роли 5
   нет утверждённого admin/internal test path. Реальные purge и restore
   не выполнялись. Исторические локальные fixtures 17C/17D не доказывают
   текущий deploy.
4. 120-минутная трансляция ранее сообщена оператором, но шесть прямых
   immutable артефактов не содержат её трассу и доказательство
   эквивалентности metering/TTL. Текущий 10-минутный прогон это не заменяет.
5. Нет независимой связки текущих Agent/PB/IoT/media/MB image ↔ source
   commit ↔ schema revision ↔ flags ↔ rollback. IoT API сейчас 0.2.1,
   тогда как исторический export 17C указывает 0.1.1; причина изменения
   не доказана в рамках разрешённого scope.

Итог: наблюдаемый видео/console тракт работает, критического runtime
дефекта в выполненном smoke не обнаружено. `H-L4D-17F-DOCS-FIX-01-v1`
**не принимается**: обязательные пункты выше остаются `BLOCKED_TESTS`.
Candidate и запись `ACCEPTED` не создаются, `L4D-18A-SHARED` не запускается.

## Адресные corrective scopes

- `L4D-17F-MB-TESTPATH-01` (`MenuBuilder`): утверждённый изолированный
  тестовый lifecycle регистрации, free→paid→grace/block, повторных
  webhook/poll/usage events и read-only Hub с безопасной очисткой fixture.
- `L4D-17F-MEDIA-ARCHIVE-EVIDENCE-01` (`l4media`): immutable runtime
  manifest/restore dry-run evidence и 120-minute route/TTL equivalence.
- `L4D-17F-DEPLOY-EVIDENCE-01` (`l4desk-service`): data-only, адресно
  разрешённые версии Agent/PB/IoT/media/MB с source/image/schema/flags/
  rollback и SHA-256, без чтения соседних исходников в 17F.

После этих входов требуется новый 17F black-box прогон; старый отчёт
остаётся неизменяемым историческим результатом.
