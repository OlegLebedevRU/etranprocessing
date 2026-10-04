# Надёжность resolving/network: tools 1.10.2, leo4proxy 1.8.1

Подписанный выпуск **1.10.2 / leo4proxy 1.8.1** опубликован 2026-10-04;
[запись публикации](../artifacts/l4tools/1.10.2.json). Upgrade773 1.10.1 → 1.10.2 подтверждён операторским логом 16:36 UTC:
ready/0, proxy 1.8.1, MQTT/HTTPS/RTP TLS valid. Рабочее видео отдельно
подтверждено для 1.10.1; после Upgrade до 1.10.2 такого подтверждения ещё нет.
Ниже — расчёт по коду и локальные проверки 1.10.2,
а не результат аварийных испытаний production или оценка вероятности отказа.

## Порядок и границы времени

Одно соединение: построить кандидатов → разрешить A → TCP → TLS → проверить
embedded CA, срок/EKU и **логическое** имя → передать application data.
Повтор кандидата происходит до передачи данных; payment POST после отправки
не повторяется этим механизмом. Физический IP не отменяет TLS/admission.

| Операция | Сетевой бюджет | Последовательность / что входит |
|---|---:|---|
| Runtime MQTT/HTTPS/stream/RTP connect | 10 с | Один deadline для SRV, A, всех TCP/TLS попыток |
| Policy recovery connect | 15 с | Включает bootstrap IP после допустимых кандидатов |
| Явный bootstrap policy probe | 8 с connect | Непосредственный IP, исходное HTTPS TLS name |
| Policy request/response | 10 с | Один I/O deadline на send, recv и TLS-фрагменты; после connect |
| Policy fetch всего | ≤25 с; прямой bootstrap ≤18 с | Connect + request/response, без нового timeout на каждый фрагмент |
| TLS diagnostics | 8 с **на канал параллельно** | Медленный MQTT не расходует бюджет HTTPS/RTP |
| Дочерний diagnostic process | 9,5 с + до 1 с завершения | Жёсткий потолок установщика; отсутствие verdict → ошибка |
| Local `/_leo4/info` | 1,2 с на проверку | Literal loopback, единый connect/send/read deadline |
| Ожидание proxy при startup | 15 с | Проверки с остатком времени; при smoke-only — одна проверка |
| Mosquitto TCP probe | 3 с | Проверяется SO_ERROR, а не только writable socket |
| Ожидание l4desk | 15 с | Только активная пользовательская сессия; без перезапуска |
| FFmpeg frame probe | 10 с + до 1 с завершения | Диагностический дочерний процесс |
| Verify | Плановый сетевой бюджет ≤60 с | Максимум 3 TLS проверки, между повторными 5 с; повтор запускается только при запасе ≥15,5 с |

При обычном startup без повторов верхняя сумма ожиданий:
`15 + 1,2 + 3 + 15 + 11 + 10,5 = 55,7 с`.
Для smoke-only вместо startup wait: `1,2 + 1,2 + 3 + 15 + 11 + 10,5 = 41,9 с`.
Запас до 60 с ограничивает повторные TLS probes. Windows scheduling, локальные
SCM/CNG/SSPI операции и запись summary не являются hard real-time операциями;
эти суммы ограничивают сетевые ожидания, а не гарантируют длительность всей
установки. Stop/Start/enrollment находятся вне Verify.

SRV ждёт не больше `min(2 с, оставшийся бюджет)`. A lookup ограничен остатком
попытки и 2 с. У одного host перебираются до 8 разных A-address; дубликаты
удаляются. У канала — до 16 кандидатов без повторов host:port.
TCP select проверяет SO_ERROR и остаток deadline; partial TLS send/recv не
обновляет общий срок. Close имеет ограниченный drain.

Для `policy_ip`/`bootstrap_ip` резервируется `min(1500 мс × K, 3000 мс)`,
где K — число оставшихся IP fallback. Primary candidates делят время сверх
этого резерва; если его нет, они пропускаются. IP-кандидаты делят остаток.
Каждая endpoint-попытка ограничена 2,5 с. При одном fallback и достаточном
начальном бюджете ему остаётся минимум 1,5 с даже после исчерпания primary.
При 5 IP fallback резерв 3 с даёт по 600 мс: соединение с большей задержкой
может не успеть. Bounded timeout не гарантирует доступность при любой RTT.

## Матрица маршрутизации

Проверены 2304 комбинации на каждой архитектуре:
`SRV 2 × explicit CLI 2 × DNS 3 × cache 3 × recovery 2 × bootstrap 2 ×
verified LKG 2 × aliases 2 × channels 4`.
DNS: отсутствует / валидный SRV / `.`. Cache: отсутствует / свежий / просрочен.
Aliases включают совпадения policy/default и policy-IP/bootstrap.

| Режим / состояние | Порядок допустимых источников | Ожидаемый исход |
|---|---|---|
| Auto, валидный SRV | SRV → verified SRV LKG → fresh policy host → default → fresh policy IP | Первый успешный строгий TLS |
| Auto, SRV NXDOMAIN/ошибка/timeout | Verified SRV LKG → fresh policy host → default → fresh policy IP | IP сохраняет доступность при отказе A, если укладывается в резерв |
| Auto, SRV `.` | Пустой список | Явный отказ сервиса; default, LKG и bootstrap не обходят его |
| `--no-srv` | Fresh policy host → default → fresh policy IP | SRV/LKG не используются |
| Manual host:port | CLI | Политика адресации не заменяет указанный маршрут; admission остаётся |
| Manual + recovery | CLI → bootstrap IP | Восстановление policy; application route остаётся CLI |
| Cache отсутствует | SRV/default; в recovery — bootstrap | Без cache MQTT/RTP ещё не получают policy IP автоматически |
| Cache просрочен, application connect | SRV/LKG/default | Просроченные policy host/IP исключены |
| Cache просрочен, policy recovery | SRV/LKG/default → cached IP → bootstrap | Старые IP допустимы только для защищённого policy GET |
| Cache чужого SN/повреждён | Как cache absent | Чужая идентичность не переносит routes/admission |
| Несколько A-address | Разные IPv4 в рамках одной endpoint-попытки | Ошибка первого A не исключает следующие |
| SRV/default/policy совпадают | Один host:port с наиболее ранним source | Без повторного расхода бюджета |
| DNS cache полностью занят завершёнными запросами | Evict completed slot, новый bounded query | Свежий hostname не ждёт TTL освобождения всего cache |
| Все 24 DNS workers заняты | Быстрый отказ DNS lookup; numeric IP bypass | Число зависших DNS workers ограничено; IP не ждёт их |

SRV priority/weight сохраняются. Verified LKG живёт 300 с и содержит hostname,
а не сохранённый A-address; после истечения A cache нужен DNS или policy IP.
DNS TTL ограничен 30–300 с, negative cache — 30 с. Эти задержки учитываются
при восстановлении только через DNS. Полная недоступность DNS в Auto при
валидном certificate и достижимом bootstrap допускает policy recovery, после
которого обычные соединения получают fresh IP fallback. Manual hostname без
DNS остаётся недоступен по контракту выбранного маршрута.

## Матрица TLS, policy и диагностики

| Условие | Поведение | Setup verdict / результат |
|---|---|---|
| Канал отключён | Без TCP/TLS | `skipped`, не вызывает degraded |
| MQTT/media admission denied или grace истёк | Без media application sockets; HTTPS recovery остаётся | `policy_blocked`, не сетевой отказ |
| CA/name/EKU/time не проходит | Нет application data, включая IP route | `cert_invalid`, degraded; автоматический повтор не маскирует trust failure |
| TCP refused/reset, A/SRV failure, TLS protocol error | Следующий допустимый кандидат в пределах deadline | `probe_failed`, bounded retry diagnostics |
| Deadline исчерпан | Прекращение текущего пути | `timeout`/`probe_failed`, degraded; не `ready` |
| Diagnostic EXE/SCM config недоступен, process/pipe/thread не создан | Нет предположения о defaults | `probe_failed`, degraded |
| Certificate исчез между discovery и diagnostic process | Без mTLS | `no_certificate`, degraded; без авто-enrollment в smoke-only |
| Один канал TLS valid, другой failed | Сеть достижима, обязательный канал не готов | `network=reachable`, но degraded |
| Нет successful TLS | Достижимость не доказана | `network=unknown`; DNS не выдаётся за доступность сети |
| Нет active certificate | Upstream diagnostics не выполняются | `network=not_run`, activation required при здоровых локальных службах |
| Одна из 4 обязательных служб не RUNNING / local probe fails | Ошибка критической готовности | failed/27 независимо от успешного TLS |
| Холодный cache / policy ещё загружается | До 3 bounded probes с reload SCM/cache, паузы 5 с | Повтор только transport failure; ready после фактического success |

CLI IP в explicit remote становится также TLS identity: ему нужен IP SAN.
Policy/bootstrap IP использует исходное логическое DNS name. Проверка сертификата
не отключается ни в одном из этих вариантов. `network=reachable` доказывает
хотя бы один TLS канал, не успешный payment/video или доступность каждого сервиса.
Diagnostics берёт аргументы **из SCM ImagePath**, проверяет соответствие binary
выбранному dest и сохраняет quoting; `service-args.txt` остаётся mirror для watchdog.
Старый mirror не может скрыть manual endpoint или включённый RTP.

## Матрица установщика

| Вызов | CA/enrollment/env/firewall/files/services/state | Проверка |
|---|---|---|
| Install/Upgrade/Repair | Штатная последовательность Check → Prepare → Stop → Update → Start → Verify → Finish | Stop в обратном, Start в прямом порядке; persist version после допустимого завершения |
| Автоматический Verify той же версии | Прежний обслуживающий flow без unpack | Это не read-only режим |
| Изменение сети при той же версии (CLI/GUI) | Переход в Repair и применение SCM конфигурации | Не теряется в Verify; GUI загружает текущие SCM поля и сохраняет остальные каналы |
| `--smoke-only` при любой установленной версии, включая отсутствующую/новее setup | Без перечисленных мутаций; только discovery, SCM/local/TLS probes и diagnostic log/summary | Не превращается в Install/Repair и не блокируется downgrade |
| Smoke-only + PIN/force-reissue/repair/network/payload | Отклоняется CLI | Нет смешанного диагностического/изменяющего flow |
| Smoke-only, отсутствует certificate | Без PIN/enrollment | 10 при локально здоровой установке; 27 при отсутствии локальной готовности |
| Smoke-only, certificate store error | Без попытки исправления store | failed/20, если локальные критические проверки проходят |

В smoke-only сохраняется фактический `installed_version`, crash marker не
очищается; пишутся только log/summary. Enrollment нового certificate через PIN
является отдельным flow: bootstrap policy требует уже существующий mTLS certificate
и не заменяет доступность CA enrollment endpoint.

## Проверки и остаточные ограничения

Локальные suites: routing 2304 + 5 saturated fallback budgets; независимые
diagnostics 20; absolute I/O deadlines 12; actual loopback Schannel stalled
handshake/retired credentials; policy/grace/cancellation/cache recovery; installer
pipeline 44 (включая 30 read-only комбинаций, cold transport retry и same-version network change); local HTTP
9; pipe/process failure и exit race 7. Все suites выполняются x86/x64.
Точный итог сборки и подписи — в [task handoff](../.agent-context/tasks/completed/2026-10-04-leo4proxy-network-reliability.md).

Реальный Win64 smoke-only сборки 1.10.2 на установленном 1.10.1: ready/0, четыре
службы сохранили PID/SCM/status, installed_version остался 1.10.1; включённые
upstreams valid. Свежий proxy 1.8.1 отдельно проверил три TLS канала по 172 мс,
stream skipped, и прямой bootstrap policy GET по IP — valid/strict. Это healthy
runtime evidence; аварийные комбинации проверялись fixtures, не отключением production.
Dest с `/` и завершающим separator нормализуется перед сравнением с SCM.

Для имеющего только DNS маршрут терминала conservative recovery envelope включает
до 30 с negative cache, до 10 с connect и до 30 с RTP reconnect backoff: около
70 с, плюс scheduling. RTP retry: 3 → 6 → 12 → 24 → 30 с. Policy poll — 30 с
после окончания предыдущего запроса. Если восстановление произошло сразу после
failed fetch: следующий fetch ≤30 + 25 с; если предыдущая попытка ещё занята
мёртвым маршрутом — conservative ≤25 + 30 + 25 с, плюс цикл worker.
Это верхние расчётные envelopes, а не измеренные p95/p99.

SPOF остаются: общий bootstrap/policy/media хост, CA и private key доступность,
локальные часы, внешняя сеть. IPv6, CRL/OCSP, Win7 runtime, чистый PIN enrollment,
длительная деградация реальной сети и browser decode telemetry здесь не проверены.
Лимиты establishment/diagnostics не являются новыми timeout контрактами для всех
уже открытых application streams. Процент доступности нельзя вывести из матрицы
без RTT/loss/failure/repair статистики инфраструктуры.

## Operator Upgrade773 acceptance, 2026-10-04 16:36 UTC

Upgrade 1.10.1 → 1.10.2 занял 48 с от Check до Finish; Verify — около 1 с.
Службы остановлены в обратном порядке и запущены в прямом; четыре RUNNING,
SCM аргументы сохранены, сертификат переиспользован, rollback/1.10.1 подготовлен.
Local smoke/capture/input passed, proxy 1.8.1.0; MQTT/HTTPS/RTP TLS valid,
Stream skipped; итог ready/0. `network: not_run` в локальном smoke выводится
до TLS diagnostics и не обозначает отказ сети. Это healthy-path Upgrade;
отказ DNS, IP fallback, cold retries и outage этим запуском не проверялись.
PendingFileRenameOperations: предупреждение осталось; перезагрузка не подтверждена.
