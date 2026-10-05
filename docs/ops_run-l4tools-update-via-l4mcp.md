# Обновление L4 Tools Suite через l4mcp

Проверенный маршрут: **подписанный пакет → приватный S3 → Leo4Proxy на терминале →
проверка файла → одноразовая задача Windows → l4setup → проверка результата**.
Команды и диагностика идут через l4mcp/L4Con. Байты установщика не проходят через
PB, MB, IoT или MQTT. Прямой HTTP с терминала в обход Leo4Proxy запрещён.

## Статус и границы атомарности

Runtime evidence: 2026-10-05, терминал **1000007**, Windows 10 Home x64,
suite **1.13.0 → 1.13.1**, L4Con **1.12.0 → 1.12.1**.
Установка завершилась `ready`, `exit_code=0`, `rollback=none`; четыре службы
работали, PB получил свежую регистрацию FM v2. Рабочий стол был заблокирован;
интерактивный FM после этого обновления не проверялся.
[Запись выпуска](../artifacts/l4tools/1.13.1.json),
[подробное evidence](../.agent-context/tasks/active/2026-10-05-l4fm-improvements.md#signed-publication-and-remote-installation-1131).

«Атомарный» здесь означает отделённые подготовку и запуск: **непроверенный файл
не запускается**, а установщик работает независимо от останавливаемого L4Con.
Замену компонентов, дренаж служб и предусмотренный откат выполняет штатный
`l4setup`, а не удалённый PowerShell-скрипт. Это **не транзакция всей машины**:
отключение питания, остановка Task Scheduler, лимит времени задачи или ошибка
отката могут оставить неопределённое состояние. Потеря консоли не доказывает ни
успех, ни откат. `install_summary.json` сверяется с реальными версиями и службами.

## Точный промпт для повторения

Скопировать целиком. Для другого выпуска заменить номер терминала и версию;
пути обозначают профили оператора, а не содержат секреты.

```text
Обнови только терминал 1000007 до подписанной L4 Tools Suite 1.13.1 через l4mcp.
Разрешаю работу с tools и выполнение обновления на этом терминале.
Следуй docs/ops_run-l4tools-update-via-l4mcp.md.
Токен l4mcp: D:\repo\platerra\Public\etranprocessing\l4mcp_token.env.
Профиль S3: D:\repo\platerra\Public\etranprocessing\s3_cloud_ru.env.
Адрес MCP возьми из действующей конфигурации подключения; не используй чужой tenant.
Пакет и ожидаемые хэши: artifacts/l4tools/1.13.1.json и подписанный manifest.
Если требуется публикация, используй штатный deploy/publish_l4tools.py и профиль
D:\repo\platerra\Public\etranprocessing\.env; подписанный пакет не пересобирай.

Сначала проверь preflight, точный device_id/SN, текущую версию и наличие задачи
этого обновления. Если 1.13.1 уже установлена и службы исправны, только подтверди
результат: не применяй --repair и не запускай установщик заново.
Проверь подписи, timestamp, SHA256 и payload. Доставь файл через приватный S3
и явный localhost Leo4Proxy, без прямого HTTP и без серверного relay.
Проверь SHA256 и Authenticode на терминале. Запусти l4setup --silent отдельной
одноразовой задачей Windows под SYSTEM через 30 секунд, вне Job L4Con.
Не меняй сертификат, policy, MQTT-маршруты вручную и не отключай защитные проверки.

После восстановления связи проверь задачу, свежий install_summary, версии,
подписи, четыре службы и свежую регистрацию FM. При потере ответа сначала
проверь существующую задачу и итог установки, не создавай повторный запуск.
Удаляй задачу только после подтверждённого завершения; удали точную временную
версию объекта S3 и локальные временные файлы с grant. Подписанный установщик
можно оставить в защищённом кэше терминала. Секреты и presigned URL не выводи
в чат и не записывай в Git. В конце сообщи проверенные версии и непроверенные шаги.
```

Для 1000007 этот выпуск уже установлен. Повторение приведённого промпта должно
закончиться проверкой состояния, без повторной установки исправной 1.13.1.

## Входные данные и владельцы

| Значение | Источник и правило |
|---|---|
| `device_id`, ожидаемый tenant/SN | Задание оператора + `console_preflight`; не подменять другим онлайн-терминалом |
| Версия, размер, SHA256 установщика | Проверенная запись `artifacts/l4tools/<version>.json` и manifest опубликованного выпуска |
| Версии компонентов | `component_artifacts` manifest; версия L4Con обычно отличается от версии suite |
| MCP endpoint, токен | Настроенный connector либо SDK; endpoint передать как `L4MCP_URL`, `L4MCP_SECURE_TOKEN` загрузить из указанного `.env` в память |
| S3 endpoint/bucket/region | Действующий профиль и FM policy; authority должен быть разрешён Leo4Proxy |
| S3 ключ | Для текущего Cloud.ru профиля: `S3_TENANT_ID + ':' + S3_KEY_ID`; secret — `S3_KEY_SECRET` |
| Proxy | Проверенный loopback Leo4Proxy, в наблюдавшемся запуске `http://127.0.0.1:18443` |
| Каталог установки | Из текущего состояния терминала; в проверенном сценарии `C:\l4tools` |

В проверенном запуске bucket — `l4desk-fm-temp`, region — `ru-central-1`;
endpoint берётся из `S3_ENDPOINT`, а не из придуманного URL. Для SDK адрес
портального MCP заканчивается `/api/mcp/proxy`; hostname — из действующей
конфигурации подключения, не из этого документа.

Владелец изменения на терминале — `l4setup`; l4mcp только доставляет команды.
PB подтверждает регистрацию FM, но не переносит установщик. Обновление
существующего MQTT-клиента здесь не разрабатывается: повторный выбор его типа
не требуется. Публикация и подпись описаны в [release runbook](../tools/release/README.md).

## Порядок выполнения

1. **Локальная проверка выпуска.** Проверить версию, полный SHA256 и размер,
   Authenticode + timestamp всех EXE и соответствие embedded payload staging.
   Не перестраивать подписанные бинарники. Если выпуск ещё не опубликован —
   использовать штатный publisher, включая полное контрольное HTTPS GET.
   Нельзя заменять байты уже опубликованного semver.
2. **Preflight и сверка.** `console_preflight(device_id=...)` должен вернуть
   `ready=true`, правильные SN/tenant, `online=true`, `svc_online=true`.
   Прочитать установленную версию, службы, summary и задачу с ожидаемым именем.
   При занятом общем сеансе не отнимать чужую lease. При `l4con_offline` установка
   через этот маршрут недоступна; работающий L4Superv сам по себе её не заменяет.
3. **Подготовка доставки.** Загрузить проверенный файл с машины оператора в
   приватный versioned bucket, в уникальный ключ `operator-upgrades/<device>/<uuid>/...`.
   Сохранить возвращённый `VersionId`; выдать GET grant на **эту версию**, TTL 600 с.
   Не менять ACL bucket, CORS или allowlist proxy ради доставки.
4. **Скачивание и проверка на терминале.** Отдельный `console_run`,
   `shell=powershell`, `timeout_sec=20`: защищённый каталог вне заменяемых папок
   suite, явный proxy, SHA256, размер, подпись и версия установщика.
   При ошибке или таймауте не переходить к запуску.
5. **Запуск.** Второй короткий `console_run`: повторная проверка файла и
   регистрация одноразовой SYSTEM-задачи с задержкой 30 с, действием `--silent`.
   Не использовать дочерний `Start-Process` как замену Task Scheduler:
   `command_runner` использует `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`.
   Ни принятие команды MCP, ни регистрация задачи ещё не означают установку.
6. **Ожидание и сверка.** Допускать штатное исчезновение L4Con/брокера.
   Через 30 с проверять preflight, затем с интервалом около 15–30 с до 12 минут.
   После готовности читать задачу и итог, а не запускать установку снова.
   Если связи нет после этого окна — остановить попытки запуска и передать
   оператору имя задачи/каталог/последний подтверждённый этап.
7. **Очистка и запись результата.** После проверки завершения удалить задачу.
   Временную версию S3 удалить сразу после подтверждённого скачивания и проверки
   файла, либо при отказе; не ждать окончания установки. Удалить локальный grant.
   В Git сохранять только версии, хэши, время, статусы и безопасное evidence.

Промежуток между отдельными console-командами не является непрерывной общей
lease на всё обновление. На время операции нужен согласованный режим обслуживания:
не открывать параллельно FM, видео или другую консоль.
Не занимать терминал FM во время этих console-команд. Новую проверку через FM
начинать после освобождения console lease. Не снимать блокировку рабочего стола
и не менять пользовательскую сессию ради проверки обновления.

## Детали вызовов и скриптов

Ниже шаблоны для агента, а не автоматически исполняемый release helper.
Подстановка параметров и дополнительная защита примеров не являются отдельным
E2E-тестом: runtime подтверждён для выпуска и терминала, указанных выше.

### MCP и секреты

Использовать connector с нужным tenant либо SDK в окружении `MenuBuilder/l4mcp`:

```python
async with Client(os.environ["L4MCP_URL"], auth=token, timeout=70) as client:
    preflight = await client.call_tool("console_preflight", {"device_id": device_id})
    # Проверить preflight.data перед каждым новым этапом.
    if len(command) > 4096:
        raise ValueError("Command exceeds l4con limit")
    result = await client.call_tool("console_run", {
        "device_id": device_id, "shell": "powershell",
        "timeout_sec": 20, "command": command,
    })
```

`token` и `command` собрать в памяти, `.env` не печатать и не передавать токен
аргументом shell. Проверять `status`, `exit_code`, `truncated` и содержимое результата.
SDK call success не равен `exit_code=0` команды. L4Con может укоротить отображение
prompt; это не разрешает игнорировать `truncated` или отсутствие конечного результата.

**Не печатать `result.data` целиком при скачивании:** output содержит echo команды
с URL. Редактировать/удалять echo и grant перед любым выводом. URL всё равно может
попасть в штатную историю команды на сервере: это краткоживущий bearer grant,
не долгоживущий S3 ключ. Ограничить одной версией объекта и удалить её после
подтверждения доставки; не утверждать, что grant вообще не попадает в историю.

### Приватный S3 staging на машине оператора

```python
s3 = boto3.client(
    "s3", endpoint_url=endpoint, region_name=region,
    aws_access_key_id=tenant_id + ":" + key_id,
    aws_secret_access_key=key_secret,
    config=Config(signature_version="s3v4", s3={"addressing_style": "path"},
                  retries={"total_max_attempts": 1}),
)
with installer.open("rb") as body:
    uploaded = s3.put_object(Bucket=bucket, Key=unique_key, Body=body,
                             ContentType="application/octet-stream")
object_version = {"Bucket": bucket, "Key": unique_key,
                  "VersionId": uploaded["VersionId"]}
grant = s3.generate_presigned_url("get_object", Params=object_version, ExpiresIn=600)
# После подтверждённого скачивания или отказа; только созданная этой задачей версия:
# s3.delete_object(**object_version)
```

`InvalidAccessKeyId` при отдельном `S3_KEY_ID` — проверить составной идентификатор
по действующему профилю; не ротировать ключ автоматически. S3 staging — доставка
того же подписанного файла; постоянным источником выпуска остаётся registry.

### Скачивание: отдельная короткая команда на терминале

Агент подставляет литералы `$stage`, `$grant`, `$expectedHash`, `$expectedSize`,
`$expectedVersion`, `$proxy` из проверенных входных данных в **эту же команду**.
Переменные между разными `console_run` не сохраняются. При формировании строк
PowerShell одинарную кавычку в значении удваивать; не применять shell-интерполяцию
к секретам. Каталог — абсолютный новый путь под `C:\ProgramData`, например
`L4Tools-Update-<version>-<hash-prefix>`, вне `C:\l4tools`.

```powershell
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if (Test-Path -LiteralPath $stage) { throw 'Staging exists: reconcile first' }
New-Item -ItemType Directory -Path $stage | Out-Null
& icacls.exe $stage /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Staging ACL failed' }
$exe = Join-Path $stage 'l4setup.exe'
$env:NO_PROXY = '' # Только окружение этой команды; исключить обход явного proxy.
& curl.exe --fail --silent --show-error --max-time 14 --proxy $proxy --output $exe $grant
if ($LASTEXITCODE -ne 0) { throw 'Proxy download failed' }
$file = Get-Item -LiteralPath $exe
$sig = Get-AuthenticodeSignature -LiteralPath $exe
if ($file.Length -ne $expectedSize -or
    (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $expectedHash -or
    $sig.Status -ne 'Valid' -or -not $sig.TimeStamperCertificate -or
    $file.VersionInfo.ProductVersion -ne $expectedVersion) { throw 'Verification failed' }
@{ verified=$true; version=$file.VersionInfo.ProductVersion } | ConvertTo-Json -Compress
```

Перед командой подтвердить HTTPS в grant и loopback proxy. Не добавлять
`--insecure`, direct fallback, `--location` или обход policy. В проверенном запуске
на Windows 10 был системный `curl.exe`; на Windows 7 наличие curl не гарантировано.
Без него нужен отдельно проверенный proxy-capable downloader, а не импровизированный
обход. Если 14 с недостаточно, частичный файл не запускать; сначала разобрать
доставку в пределах поддерживаемого MCP timeout. Наличие staging не доказывает успех.

### Независимый запуск и защита от дублирования

Во втором `console_run` заново задать `$stage`, `$expectedHash`, `$taskName`.
Имя задачи стабильно для версии и хэша. Перед созданием проверить существующую
задачу; не применять `-Force`, `Start-ScheduledTask` или новый случайный task name
в ответ на потерю ACK. Ниже пример только для отсутствующей задачи:

```powershell
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$exe = Join-Path $stage 'l4setup.exe'
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $expectedHash) {
    throw 'Installer changed'
}
$action = New-ScheduledTaskAction -Execute $exe -Argument '--silent' -WorkingDirectory $stage
$trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddSeconds(30)
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 10) `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
    -Settings $settings -User 'SYSTEM' -RunLevel Highest |
    Select-Object TaskName,State | ConvertTo-Json -Compress
```

Не передавать `--repair`, `--force-reissue`, PIN или network overrides при обычном
обновлении. `--silent` переиспользует действующий сертификат. Лимит задачи 10 минут
ограничивает зависание, но принудительное завершение по нему **не гарантирует откат**.

## Приёмка и восстановление после неизвестного результата

После возвращения console отдельно прочитать `Get-ScheduledTask`,
`Get-ScheduledTaskInfo`, `C:\l4tools\install_summary.json`, версии и подписи
компонентов, `Get-Service L4Con,L4Superv,Leo4Proxy,mosquitto`.

| Наблюдение | Действие |
|---|---|
| Задача ещё `Running` | Продолжать ограниченное ожидание, не удалять и не дублировать |
| Ответ на регистрацию потерян | Сначала найти задачу с тем же именем и проверить её action/trigger/principal |
| `LastTaskResult=0`, но summary старый | Успех не подтверждён; сверить `LastRunTime`, timestamp и фактические версии |
| `ready/0/finish`, нужные installed/target/installer версии, свежий timestamp, службы Running | Установка подтверждена; дополнительно сверить компоненты и подписи |
| `standby/10` либо иная ошибка | Не называть успешным обновлением готового терминала; исследовать summary/log |
| `rollback=restored` | Проверить реальные прежние версии и службы; целевая версия не установлена |
| Консоль не восстановилась за окно ожидания | Не обходить preflight через другой терминал/топик; нужна диагностика оператора |
| Policy отказала download/command | Не обфусцировать команду и не обходить запрет; сохранить безопасное evidence |

Дополнительно проверить явные маршруты Mosquitto, правильный SN, отсутствие
неожиданного перевыпуска сертификата. Для FM — свежий `fm_agents` heartbeat,
ожидаемый `agent_version`, protocol 2 и `filesystem_ready`; это проверка регистрации,
а не доказательство полной работоспособности UI/передачи файлов.

Задачу удалять только при подтверждённом завершении и сверенном результате:
`Unregister-ScheduledTask -TaskName $taskName -Confirm:$false`.
Не удалять общий кэш/backup suite и не выполнять рекурсивную очистку терминала.
Если удаление тестовых файлов блокируется console policy, не заменять его обходным
API. Установщик допустимо оставить в защищённом каталоге как кэш этого выпуска.

## Источники и проверка документа

- [L4 Tools user guide](term_tool-user-guide.md) — параметры, summary, коды возврата.
- [L4 Tools architecture](term_tool-architecture-guide.md) — роли и каталоги.
- [Release/signing](../tools/release/README.md), [publisher](../deploy/publish_l4tools.py).
- [L4Con command runner](../tools/l4con/src/command_runner.c) — Job и command policy.
- [Setup engine](../tools/l4setup/src/engine.c) — условия rollback, не общая транзакция ОС.
- [Leo4Proxy CONNECT](../tools/leo4proxy/src/fm_connect.h) — policy-bound tunnel.

Документационная задача: команды установки, подключение к терминалу/брокеру,
публикация и тестовые сборки при написании этого runbook **не выполнялись**.
Проверяются относительные ссылки и согласованность с приведённым runtime evidence.
