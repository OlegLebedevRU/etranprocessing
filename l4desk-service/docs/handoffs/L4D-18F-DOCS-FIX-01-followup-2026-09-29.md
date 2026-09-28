# 18F corrective: контроль после перевода владельцев на registry

```yaml
prompt_id: L4D-18F-DOCS-FIX-01
registration_id: R-L4D-18F-DOCS-FIX-01-v1
status: BLOCKED_DEPLOY
observed_at_utc: '2026-09-28T23:27:15Z'
cascade_closed: false
scope: l4desk-service documentation and read-only production inventory
```

Этот документ дополняет [первичный gate](L4D-18F-DOCS-FIX-01-gate-2026-09-29.md).
Он не меняет принятые handoff 18A–18E и не является итоговым report или
candidate. В исходном gate зафиксированы пять принятых входов и успешная
проверка их конечных data-only артефактов.

## Принятая контрактная база

Это версии из append-only журнала, не утверждение о текущих OCI revision:

| Вход | Producer commit | Версия артефакта | Исторический статус |
| --- | --- | --- | --- |
| `H-L4D-18A-SHARED-v1` | `537a1e493c83d1fa8e8cb765228be8d1b24a1d62` | `0.1.1` | `ACCEPTED`, опубликован |
| `H-L4D-18B-PB-v1` | `67827b607e64d771e368a0a43e1b6c0f09087dd8` | `0.1.0`, Alembic `027` | `ACCEPTED`, deployed |
| `H-L4D-18C-IOT-v1` | `35f1fce054e388a2206af45f1416b9572b0614a9` | `0.1.1`, IoT `0008_org_reservations` | `ACCEPTED`, deployed |
| `H-L4D-18D-MEDIA-v1` | `e6e681dcf74fb0e81da5cf1f7f0fc2d39dca7f33` | `1.0.0` media API | `ACCEPTED`, deployed |
| `H-L4D-18E-MB-v1` | `4f72dc6dbe03acc7975463bd4fa92832c4684638` | `18E-restricted-1` | `ACCEPTED`, restricted/deployed disabled |

Текущая production schema после 18E уже была `028 (head)` на момент
первичного gate. Поэтому итоговый реестр обязан показывать отдельно версии
принятых контрактов и версии работающих образов/схемы.

## Изменение после первичного gate

Операторский шаг доставки владельцев перевёл ProcessingBackend, MenuBuilder,
L4mcp, app1, media ingress, media nginx и Janus на образы с неизменяемыми
registry digest. По результату того шага оба production Compose прошли
`docker compose config`, контейнеры имели restart count 0, а HTTP health
ProcessingBackend, MenuBuilder, L4mcp, app1 и ingress отвечали успешно.
Janus сохраняет правило пересборки только по отдельной команде пользователя.
Frontend остаётся отдельным статическим `dist`.

Новый read-only `docker inspect` production подтвердил следующие digest.
У всех семи контейнеров `RestartCount=0`, image ID совпадает с digest:

| Сервис | Текущий registry digest |
| --- | --- |
| ProcessingBackend | `bc0e5b131b4e47ec43e0695e7f87d8cc3e158bb26756bc2fb88023aa76a75a00` |
| MenuBuilder backend | `0a67f0ffd0326c2ed968b3e42d2cdd7728b4dd4df5c71582e059c061edbf5c89` |
| L4mcp | `7dcc28d19286d8ce99aedbdd4ccd2c4492ab0f7d7163416e9d9f947813069f4e` |
| app1 | `ab09311d2bfbcefd774ef283c0e8f6142c42b479ed378eb2a9be6df6da1a292b` |
| media ingress | `1b9242b290d975e769acd3a35eb5d16a286f91e71b08356458643863c58b1cd7` |
| media nginx | `fb137647e25bed749f1cf87cc0398eb8f072a900b7dfa124222cb9655fe911bd` |
| Janus | `93265665a92482ec1de2c9571a08d27c87b1dee06efc000f717ed42dfe2438ae` |

На хосте available RAM 2172 MiB, root disk 59%, load average
0.50/0.28/0.20, swap 0. Внутриконтейнерные HTTP-запросы вернули 200 для
ProcessingBackend `/api/health`, MenuBuilder `/openapi.json`, L4mcp `/health`
и app1 `/docs`; ingress `:9100/health` вернул
`{"status":"ok","routes":1,"active_media_sessions":0}`. Alembic — `028 (head)`.

Runtime-конфигурация MenuBuilder подтверждает: `l4desk_enabled=false`,
`l4desk_registration_enabled=false`, `l4desk_billing_enabled=false`,
`l4desk_policy_enforcement_enabled=false`, `l4desk_policy_shadow_mode=true`,
`l4desk_terminal_onboarding_enabled=true`, `l4desk_financial_core_enabled=true`,
`l4desk_entitlement_worker_enabled=false`,
`l4desk_metering_close_worker_enabled=false`, `is_yookassa_enabled=false`.
Считаны только булевы поля, без приватных значений.

OCI revision из контейнерных labels: ProcessingBackend и L4mcp
`7da3e0762fbd1f28a8a479ad64f3af9a7cec2a10`, MenuBuilder
`2dd1473b37b545ae8caa745f81ff2d6c33bb426f`, app1
`a50c57130eca694b704b3b8634acf8b3a59cefeb`, ingress
`801186d8699f293c70bd1e4d65f4f7e9b91a9a7e`, media nginx
`7da3e0762fbd1f28a8a479ad64f3af9a7cec2a10`, Janus
`e6e681dcf74fb0e81da5cf1f7f0fc2d39dca7f33`. SHA-256 активных
`/home/user1/compose.yaml` и `/home/user1/l4media/compose.yaml`:
`e95a9dfcc94fddb1a5b6b83d53a61700fe1fec375659a027fdfec3f412d7bde2`
и `e7b0d11391e3dc11f3be1e283db6133422902cab9b13b915b0a4d894141b8a97`.
Текущий frontend `dist/index.html`:
`47b040afc24689c3d388d68d2f4868933fd06e9f343c7dc1138997a9d7641e58`.

Старый вывод gate о локальных образах ProcessingBackend/app1/L4mcp/media nginx
заменён этим наблюдением.

## Попытка текущей проверки

Инструменты server-ops в данном исполнении недоступны:
`[MCP Ops Readiness: UNAVAILABLE]`. Первый SSH-вызов был ограничен локальными
правами к ключу `D:\.ssh\id_ed25519`; разрешённый read-only запуск с
повышенным доступом восстановил SSH. Проверка host key не отключалась;
production-файлы и сервисы не менялись.

Текущий snapshot образов, schema, flags и health получен. Свежей сверки
archive backup/restore и browser video start/stop в этом проходе пока нет.
Их нельзя подменять успешной проверкой прошлого образа.

Попытка browser smoke на доступном тестовом терминале `1000011` остановилась
при входе на сайт: `POST /api/auth/login` вернул 502. Логи работающего
MenuBuilder показывают две попытки вызова внешнего JWT issuer и ответ
`HTTP 502` на обе; отдельный GET того же issuer с production-хоста также
вернул 502 примерно за 1 секунду. `nginx-default` и MenuBuilder остаются
running с restart count 0, `/openapi.json` внутри backend отвечает 200.
Причина текущего отказа лежит на пути внешней выдачи JWT; точное состояние
функции за API Gateway без доступа к её журналу не установлено. Видео/stop
на текущем образе из-за этого не проверены; `BLOCKED_DEPLOY` сохраняется.

Archive paths `/mnt/l4desk-archive` и `/var/lib/l4media/telemetry` существуют
на корневой файловой системе; на верхнем уровне обоих каталогов сейчас 0
обычных файлов. Отдельный archive volume и systemd timer для него не найдены.
Принятый 18D data-only report описывает успешный синтетический dry-run,
backup в tar, извлечение в отдельный каталог и повторную проверку digest;
это подтверждает процедуру для образца, но не работающий off-host backup.
Archive worker и purge остаются выключенными. Фактическое выдерживание
трёх месяцев hot retention на production не подтверждено пустыми каталогами.

## Остаток до итогового report R и candidate C

1. Зафиксировать эксплуатационный план archive backup/restore и hot retention
   отдельно от уже проверенной синтетической процедуры; текущий read-only
   version/health inventory завершён.
2. После восстановления внешнего JWT issuer на текущих образах подтвердить
   коротким browser smoke `1000011`: движущиеся кадры и штатный stop без 500;
   сверить `active_media_sessions=0` после остановки.
3. Зафиксировать проверенные archive backup/restore и фактическую hot
   retention; трёхлетний срок описать только как процедурную политику.
4. Сверить эксплуатационные показатели регистрации, PIN, online, usage/event
   lag, ledger/reconciliation, pending/error payments и grace/block.
5. Не объявлять общую коммерческую активацию: пять исторических отсутствующих
   source hashes tenant 1000, sessions 476–480, остаются отдельным блокером.
6. После зелёного текущего smoke выпустить итоговый report R, затем отдельный
   candidate C в формате `DETACHED_V1` и передать контроллеру для независимого
   решения `CLOSED_ACCEPTED` об ограниченном выпуске.

До выполнения пунктов 1–4 сохраняется `BLOCKED_DEPLOY`; окончательные report,
candidate и accepted block не создаются.
