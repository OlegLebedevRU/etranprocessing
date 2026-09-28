# Контрольная точка каскада перед L4D-17E-MB

Дата сверки: 2026-09-26. Это индекс опубликованных результатов и незакрытых gates для контроллера, а не новый принятый контракт. Источник статуса приёмки — только append-only [журнал](contract-handoff.md); сведения о runtime требуют повторной проверки на момент следующей приёмки.

Обновление 2026-09-26: corrective `H-L4D-17C-VIDEO-WATCH-MB-v1` принят в §51 журнала после публикации отчёта `5020aa3` и detached candidate `51137b9` с независимой сверкой шести Git/raw digest и работающего образа. Browser подтвердил живые кадры, Stop/Start и watch WS на терминале 773; fallback при реальном сетевом отказе WS остался не проверен в браузере, локальная проверка текущего эффекта прошла. Ниже сохранена историческая инвентаризация до приёмки; отчёты 17E и 17F по-прежнему имеют `BLOCKED_CONTRACT` и требуют новых проверок по своим заданиям. Принятие 17C не означает приёмку 17E или разрешение 17F.

## Состояние входов до приёмки 17C MenuBuilder

| Вход | Журнал | Что передать дальше |
|---|---|---|
| `H-L4D-17A-TOOLS-v1` | §44, `ACCEPTED` | Agent artifact и совместимость из принятого блока. |
| `H-L4D-17B-PB-v1` | §45, `ACCEPTED` | Certificate/PIN, Alembic `027`, error-path smoke; см. исходный отчёт для границ проверки. |
| `H-L4D-17C-VIDEO-WATCH-IOT-01-v1` | §43, `ACCEPTED` | Read-only watch v1; один worker app1, cross-worker fanout отложен. |
| `H-L4D-17C-IOT-v1` | §46, `ACCEPTED` | Основной IoT provider 17C; принятый commit `7c6f75f`. |
| `H-L4D-17D-MEDIA-v1` | §47, `ACCEPTED` | Media/archive smoke и отсутствие orphan после stop. |
| `R-L4D-17C-VIDEO-WATCH-MB-v1` | §48, `AUTHORIZED` | Регистрация corrective **не является** его приёмкой. `H-L4D-17C-VIDEO-WATCH-MB-v1` пока отсутствует. |

`L4D-17E-MB.md` уже перечисляет `H-L4D-17C-VIDEO-WATCH-MB-v1` в `required_handoff_ids`. Поэтому запуск 17E как принятой последовательности требует сначала закрыть corrective по §9 `PROMPT-STANDARD.md`. Отчёт MenuBuilder сейчас имеет `status: READY_FOR_BROWSER_VALIDATION`; отдельный `L4D-17C-VIDEO-WATCH-MB-candidate.md` не опубликован. Не создавать `ACCEPTED` из этой контрольной точки.

## Изменения после принятых блоков

| Контур / владелец | Принятая граница | Более поздний результат и evidence | Состояние передачи |
|---|---|---|---|
| app1 watch / IoT | Provider §43 привязан к `f58dfb5`; основной 17C §46 — к `7c6f75f`. | `d7b604a` подавляет `invalidate` при изменении одного `last_seen_at`, сохраняя сигналы значимых изменений. Provider report в отдельном репозитории `iot-rpc-rest-app/docs/l4desk/handoffs/L4D-17C-VIDEO-WATCH-IOT-01-report.md` обновлён в `166094f`: 423 tests passed, app1 пересобран/перезапущен, один worker, между WS reconnect heartbeat больше не вызывает status GET. | `PENDING`: после принятого provider commit был runtime change; сверить фактический deployed image, digest, совместимость и связать с corrective/consumer acceptance. Исторический §43 не переписывать. |
| MenuBuilder BFF/UI watch | Corrective зарегистрирован в §48, handoff ещё не принят. | Реализация `0b60a53`, локальные проверки и browser trace описаны в [MenuBuilder report](../../../MenuBuilder/docs/l4desk/handoffs/L4D-17C-VIDEO-WATCH-MB-report.md). `5fac8ed` заменил опрос `/session/status` на локальный WebRTC `getStats()`. `dfb959b` убрал дубли REST на штатном 60-секундном WS reconnect. После обновления страницы в 09:47:29 и 09:48:31 UTC каждый reconnect дал одну пару status GET, между ними — только lease `POST /control/keepalive` около каждых 5 секунд. Отчёт об этих наблюдениях опубликован в `7a36e48`. | `PENDING`: подготовить финальный `ACCEPTED` report и `DETACHED_V1` candidate, проверить и принять `H-L4D-17C-VIDEO-WATCH-MB-v1` до 17E. Browser trace доказывает снижение polling, не полный E2E. |
| MenuBuilder deployment integrity | Принятый 17D не фиксирует текущий MenuBuilder image. | При первой browser проверке `/session` вернул 500 из-за отставшего `Settings` в deployed image. По разрешению пользователя активный образ получил `app/config.py` из `0b60a53`; последующие `/session` и `/stream/start` вернули 200. Frontend assets из `dfb959b` развёрнуты. Полная хронология и rollback — в MenuBuilder report. | `PENDING`: перед 17E сверить все файлы/версии активного backend image и frontend с release commit. Следующий выпуск из старого `main` может вернуть ошибку 500; нужен штатный воспроизводимый release из принятого кода. |
| Video/lease semantics | Watch только read-only, invalidation требует REST resnapshot. | Пользователь сообщил, что трансляция жива и после обновления вкладки виден только keepalive. Серверные логи подтверждают WS upgrade и HTTP pattern; свежие кадры, stop/reconnect, auth/tenant negative cases по всему corrective ещё не оформлены как принятие. | `PENDING`: выполнить оставшиеся критерии `L4D-17C-VIDEO-WATCH-MB.md`; keepalive не считать status polling и не устранять без отдельного изменения lease-контракта. |

## Порядок закрытия

### Повторная сверка 2026-09-26 перед 17E/17F

- [17E report](../../../MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-report.md) создан со статусом `BLOCKED_CONTRACT`: backend 469 tests passed, Ruff/format/Pyright clean; frontend build и 55 tests passed. Активные `config.py`, два video routes и frontend watch assets совпадают по SHA-256 с локальными файлами. Активный IoT watch handler совпадает с `d7b604a`. Коммерческие флаги в текущем MenuBuilder runtime выключены. Image и работающий сервис не менялись.
- Пользователь подтвердил отсутствие утверждённого test tenant и процедуры для email/YooKassa sandbox. Production-safe commercial smoke не выполнялся; full image/release и финансовые инварианты E2E не подтверждены. Совпадение выбранных файлов не доказывает тождество всего backend image.
- [17F inventory/report](../handoffs/L4D-17F-DOCS-report.md) создан со статусом `BLOCKED_CONTRACT`: прямой вход `H-L4D-17E-MB-v1` отсутствует, black-box E2E не запускался. Перечень тестовых ресурсов и cleanup для безопасного прогона находится в этом отчёте.
- Заблокированные отчёты не добавляются в единый журнал как `HANDOFF`; контрольная точка §49 остаётся индексом, а не разрешением перейти к 17F.

1. Завершить corrective `L4D-17C-VIDEO-WATCH-MB`: отчёт `ACCEPTED`, опубликованные байты отчёта, отдельный candidate, независимые SHA-256/deploy/sequence проверки, append-only запись handoff. Если обязательная проверка невозможна — зафиксировать blocker и не подменять его наблюдением из браузера.
2. Перед 17E сверить latest deployed IoT/MenuBuilder commits с принятыми версиями и документировать post-acceptance delta. `L4D-17E-MB` должен проверить commercial contour по своему полному списку, включая версии/flags/rollback и финансовые инварианты.
3. В 17F перенести матрицу фактически принятых версий и E2E evidence; в 18F — эксплуатационные риски и точный deployed registry. При оставшемся `PENDING`/`DRIFT` по обязательному контракту cascade close не объявлять.

Код сервисов, архивы, тестовые данные и runtime в рамках этой контрольной точки не изменялись.
