# leo4proxy: проверка DNS/SRV-концепции и контекст реализации

Статус: policy и media TLS внедрены; подписанный tools/setup 1.10.1 опубликован, Upgrade773 завершён ready/0 с valid MQTT/HTTPS/RTP TLS.
Подписанный опубликованный 1.10.2 / proxy 1.8.1 исправляет обнаруженные сетевые/diagnostic defects;
[новая матрица](term_net-leo4proxy-resolving-reliability-matrix.md) отделяет локальные
проверки от production evidence. [Запись выпуска 1.10.2](../artifacts/l4tools/1.10.2.json); Upgrade773 до 1.10.2 подтверждён операторским логом 16:36 UTC: ready/0, proxy 1.8.1, MQTT/HTTPS/RTP TLS valid; видео после этого Upgrade отдельно не подтверждено.
Дата: 2026-10-04. Код сверялся в worktree на HEAD `c58f607`, leo4proxy `1.7.3`.
Исходное ревью ниже относится к HEAD `c58f607`; текущий статус внедрения и runtime приведены в отдельном разделе.

Уточнение пользователя: последний bootstrap для policy — TCP к публичному IP
`87.242.100.34`; рассмотреть IP fallback для каналов в policy. Это включено в
план ниже. Пользователь разрешил внедрение, policy deploy и выпуск tools; builder и media CA-пара подтверждены отдельно.
Дополнительное предложение пользователя: на последнем IP-уровне рассмотреть
отключение server certificate validation и оценить риск подмены IP. Варианты
и рекомендация приведены ниже; автоматический insecure downgrade не согласован.

Исходная концепция: `D:\repo\platerra\Public\etranprocessing\docs\term_net-leo4proxy-dns-srv-endpoints.md`.
SHA-256 прочитанной версии: `F8CA892F87215D7BF158E0A8D5FACCCCC2A940C3EFB391D2B2D00A9EEBAD8725`.
В текущем worktree исходного документа нет. Настоящий packet самодостаточен для
планирования; исходную концепцию не считать согласованной спецификацией.

## Текущая реализация и evidence (2026-10-04)

- Выбран strict bootstrap/IP fallback с логическим TLS-именем и встроенным CA.
  SPKI emergency и unverified TLS остаются рассмотренными альтернативами.
- ProcessingBackend policy: optional endpoints и TTL, env-only конфигурация;
  old-client JSON совместимость сохранена. Ruff/format/pyright и 156 тестов прошли.
  Выпущен источник `3ad77552fba49b56722829da1b4277339b3c021c`, image digest
  `sha256:264cc0588ba6e20712b4811bf9649db73940e8515cf04cdaa4d8f91288fa325b`.
  Прямой mTLS GET подтвердил 4 канала, fallback IP и TTL 86400.
- Media теперь предъявляет CA-issued `dev.leo4.ru`. Canonical mounts и startup/
  pre-deploy gate проверяют цепочку, serverAuth, имя, срок и ключ. Выпуск и
  отрицательные тесты: [media handoff](../.agent-context/tasks/completed/2026-10-04-media-tls.md).
- leo4proxy 1.8.0: SRV → policy → default → policy IP; explicit CLI сохраняет
  приоритет. SRV `.` означает недоступность без обхода. Verified SRV LKG хранится
  до 300 секунд; negative DNS cache 30 секунд, positive TTL 30–300 секунд.
  24 single-flight DNS slots ограничивают фоновую работу, caller wait ≤2 секунд.
- Policy routing cache отделён от admission: HKLM `Endpoints` REG_BINARY и
  `%ProgramData%\Leo4Proxy\endpoints.cache` (8-byte receive time + исходный JSON).
  SN/TTL проверяются; expired IP разрешён только для recovery policy GET.
  Admission deny и прежний 72h grace не меняются.
- Все 4 канала используют numeric TCP target с исходным logical TLS identity.
  Chain строится с exclusive embedded root и без AIA/network retrieval.
  CRL/OCSP не проверяются; неверное локальное время приводит к отказу.
  IPv4/первый A-address остаются ограничением текущего транспорта.
- Native x86 и x64 `--check-policy-bootstrap --policy-bootstrap-ip 87.242.100.34`
  получили valid policy: strict SNI/name/Host, numeric TCP, без DNS и записи cache.
  Это проверяет cold policy recovery с существующим client cert, не выпуск cert.
- l4setup 1.10.0: диалог Сеть, Auto/manual/--no-srv, build-time bootstrap profile;
  сохранение SCM args и atomic service-args.txt для l4superv 1.10.0. При изменении
  сетевых настроек сохраняются остальные CLI options. TLS diagnostics выполняются
  в verify worker, budget 8 секунд (+ process shutdown), verdicts входят в summary.
- Source/target diagnostics добавлены в proxy info и l4superv. Свежие x86/x64
  native builds/tests и полный packaging gate 1.10.0 прошли; по 61 embedded файлу
  совпадают со staging. OpenH264 SDK восстановлен из локального release cache.
  Runtime MQTT/RTP используют SRV, HTTPS — default; strict TLS valid в обеих архитектурах.
- Не проверены на terminal Win7: чистая установка/удаление сертификата, Auto/manual
  GUI, cancel, полный cold/warm outage всех рабочих каналов и расширенная video E2E matrix.
  Пользователь выполняет clean l4setup 773; сертификат не удалялся.
- Tools 1.10.0 опубликованы из clean main `68ad0ccd28bc2ffae239aa5a38512daf19475cab`.
  19 EXE имеют Valid Authenticode и timestamp; payload gate 61/61 per arch passed.
  Полные HTTPS downloads setup/manifest/SHA256SUMS через curl совпали по размерам
  и SHA-256 после усечённого Python GET. Setup SHA-256:
  `d06a430e34dd38b8cb2cef05d8156e6d52471b7096291f7253ed9485fc5e1595`.
  [Установщик](https://l4tools-generic.ar.cloud.ru/l4tools/1.10.0/l4setup.exe),
  [release record](../artifacts/l4tools/1.10.0.json).
- Install773/Windows10 x64: службы и local smoke прошли, сертификат reused.
  False RTP probe_failed воспроизведён в setup pipe reader 5/5; после drain
  финальных bytes на process exit — 5/5 valid. Прямой installed proxy strict
  MQTT/HTTPS/RTP TLS valid. Setup-only 1.10.1 собран x86/x64, regression tests
  passed, 61/61 payload files совпадают. Оператор повторно подписал компоненты:
  полные hashes изменились, все 18 PE code/data/resource sections совпадают с 1.10.0.
  19 signatures/timestamps Valid; 1.10.1 опубликован из clean main 7259be4,
  полные HTTPS downloads всех трёх файлов совпали по sizes/SHA256.
  [Установщик 1.10.1](https://l4tools-generic.ar.cloud.ru/l4tools/1.10.1/l4setup.exe),
  [release record](../artifacts/l4tools/1.10.1.json). Релиз 1.10.0 остаётся immutable.
- Оператор выполнил полный Upgrade773 1.10.0 → 1.10.1 (не smoke-only),
  14:18:05–46 UTC: ready/0; drainage, payload swap, сохранение SCM options и
  четыре службы RUNNING. MQTT/HTTPS/RTP TLS valid, stream disabled/skipped;
  local smoke, capture и remote_input available. Сертификат reused, PIN enrollment
  не проверен. /info: MQTT source=srv, HTTPS source=policy, RTP counters=0 —
  в этом снимке передача видео ещё не шла. После обновления оператор подтвердил
  работающий видеопоток; agent decode telemetry/first-frame не измерялись.
  Pending reboot остаётся предупреждением.

## Исходный task intake ревью

- Цель / тип задачи: проверить концепцию, подготовить документационный контекст.
- Scope будущего внедрения: leo4proxy, l4setup, l4superv, ProcessingBackend policy.
- Владелец API: ProcessingBackend; DNS и сертификаты — владельцы инфраструктуры;
  аргументы установки — l4setup, исполнение и диагностика — leo4proxy/l4superv.
- Producer → transport → consumer: DNS → resolver → leo4proxy;
  ProcessingBackend → mTLS HTTPS policy → leo4proxy;
  l4setup → SCM/config → служба/watchdog; leo4proxy info → l4superv.
- Миграции/изменение БД для env-конфигурации endpoints не нужны.
- В рамках ревью: только чтение исходников и запись Markdown, без тестов,
  сборок, установки, изменений DNS, брокера, production или deploy.
- Готовность документа: расхождения, решения на согласование, точки изменения,
  порядок внедрения и позитивные/негативные проверки перечислены.

## Вывод

Разделение logical identity и connect target и аддитивное поле `endpoints`
обоснованы. Реализация исходного текста без уточнений не обеспечивает заявленные
гарантии доступности и TLS-безопасности. Решения ниже — предложения на согласование,
не разрешение ослабить существующие инварианты.

## Расхождения и необходимые решения

| В концепции | Результат проверки / предлагаемое уточнение |
|---|---|
| Policy/default спасают при полном отказе DNS, включая холодный старт | Исходные DNS-имена этого не обеспечивают. По уточнению пользователя добавить независимый policy bootstrap на `87.242.100.34:443` и числовые fallback IP каналов в policy. Холодный старт без DNS/кэша работает при доступном bootstrap, действующем terminal cert и успешном strict TLS; если bootstrap недоступен, без заранее полученных IP восстановление не гарантируется. |
| SRV-ошибка запускает fallback | Нужно обрабатывать также недоступный TCP target и исчерпание всех SRV-кандидатов. DNS-ошибка, TCP-ошибка, ошибка сертификата и application отказ — разные классы. HTTP 401/403 и server deny не обходить failover. |
| Strict только для SRV, policy/default остаются insecure | Переключение источника позволяет downgrade; policy, полученная через insecure TLS, не аутентифицирована. Предложение: безопасность задаётся режимом канала, а не источником; в Auto strict для всех источников и policy fetch. Legacy совместимость выделить явно и согласовать отдельно. |
| «Доверять» автоматически/вручную после пробы | Противоречивые значения: trust=no одновременно описывает безопасный unattended default и отключение strict. Предложение: показывать результат проверки отдельно; default strict, сбой пробы его не отключает. Проба не закрепляет доверие к будущим target/сертификатам. |
| Проверка после handshake предотвращает передачу клиентского сертификата | После mTLS-handshake сертификат уже мог быть передан. Можно гарантировать отсутствие application payload до проверки; непередача клиентского сертификата требует отдельного дизайна handshake и доказательства на стенде. Приватный ключ при mTLS не передаётся. |
| Additional trust-anchor + второй CredHandle без manual validation | Additional store сам по себе не задаёт эксклюзивное доверие корню. Предложение: отдельный chain engine с `hExclusiveRoot`, SSL name/EKU/time и согласованной revocation policy; automatic SChannel не получает такой корень автоматически. Не смешивать эти два механизма без доказательства. |
| Файл CA рядом с exe как fallback | Произвольный заменяемый файл меняет trust anchor. Предложение: встроенный публичный корень/набор пинов, проверенный источник, процедура ротации; внешний файл только при проверке против встроенного пина. Точный CA/пин ещё не проверен. |
| `DnsQuery_A` гарантирует 2 секунды | У API нет параметра deadline. Нужен ограниченный resolver worker, single-flight на имя, ограничение числа зависших запросов, безопасное завершение и запрет позднему результату менять новую generation. `getaddrinfo` также вызывается до TCP timeout. |
| Policy через WinHTTP: достаточно заменить Host | TCP target и TLS identity должны разделяться и здесь. Изменение HTTP Host не доказывает правильность SNI/SSL name. Предложение: единый SChannel transport для policy GET, с ограниченным HTTP parser; либо доказанный WinHTTP вариант на Win7 до основной интеграции. |
| `--no-srv` полностью равен 1.7.x | Отключение SRV оставляет новый policy fallback и TLS-режим. Предложение: флаг выключает только SRV, а полный rollback выполняется прежним бинарником и сохранёнными SCM args/config. Отдельный legacy mode — только по согласованию. |
| Hot-swap не разрывает сессии | `service_mgr.c` при смене сертификата перезапускает MQTT/Stream/RTP и reverse proxy. Не менять это в DNS-задаче; сохранение сессий обещать только при обновлении endpoint cache. |
| l4superv уже читает `leo4proxy_args` и запускает с ними | В src поле только объявлено и обнулено; SCM start использует `StartServiceW(..., 0, NULL)`. l4setup и suite service installer записывают фиксированные команды. Требуется контракт чтения/сохранения/repair с сохранением SCM ImagePath, а не простое добавление полей. |
| l4setup — единственный писатель SCM args | Также существуют `leo4proxy --install` и `l4superv` service install. Нужен единый формат опций и правило сохранения при update/repair; исключить перезапись выбранного режима другими путями. |
| Packaging не меняется | Имена могут остаться прежними, но обновлённые бинарники и CA resource нужно проверить в обоих package/installer путях. Решение «изменение не требуется» допустимо только после проверки состава и smoke. |
| Backend env defaults равны production | Конфликт с AGENTS: defaults пустые/localhost. Предложение: endpoints выключены без env; production значения приходят из env, неверный блок опускается без нарушения admission policy. |
| 3/4 DNS подтверждены, NXDOMAIN значит кэш исключён | Это сведения исходного документа, не runtime evidence этой задачи. Два рекурсивных резолвера не исключают negative cache; перед rollout проверять authoritative DNS, TTL и записи всех каналов. |

`/_leo4/info` имеет ветвление по `is_local`; новые подробности добавлять только в
локальную ветку, сохраняя существующие ограничения и формат для l4superv.
Текущий TCP helper использует `AF_INET` и первый addrinfo: полноценный IPv6
не подтверждён. Не обещать AAAA-поддержку без дополнительной реализации/приёмки.

## Предлагаемый контракт первой версии

1. Выбор кандидатов: explicit CLI → SRV set → authenticated policy host → default host;
   после исчерпания DNS/transport вариантов — policy fallback IP из свежего ответа/кэша.
   Если IP ещё нет, единый policy worker получает их через независимый bootstrap.
   CLI authoritative: не переходить на другой источник при ошибке manual target.
   Для Auto перебрать SRV по priority/weight и transport failover в общем бюджете.
   TLS rejection не переводит канал в insecure; допустим только другой кандидат
   с той же обязательной проверкой. Повтор payment/body после отправки не вводить.
2. Endpoint snapshot: channel, logical_name, connect_host, port, source,
   generation, expiry, validation status; сессия удерживает snapshot до закрытия.
   Last-known-good обновляется после TLS validation; stale retention ограничена.
3. SRV: TTL cache и negative cache независимы от candidate health backoff;
   нормализовать trailing dot, port и bounds, ограничить число records.
   RFC target `.` означает отсутствие сервиса, не обычный NXDOMAIN: поведение
   fail-closed/fallback требует явного решения. SRV target не должен быть CNAME.
   Текущие `_tls` service owners оставить как проектную договорённость;
   это не согласование переименования DNS records.
4. Policy `v=1`: optional `endpoints` с ключами mqtt/https/l4rtp/l4stream,
   каждый host + port 1..65535 и optional `fallback_ips`; подробности ниже.
   Logical TLS name не приходит из DNS/policy.
   Отсутствие поля — совместимость; invalid semantic block не ломает permissions.
   Синтаксически неверный JSON/duplicate keys по-прежнему отклоняют ответ целиком.
   Старый parser ограничен 16 KiB/512 tokens/depth 16: проверить новое тело на старом клиенте.
5. Endpoint cache отделить от 72-часового admission record: expiry endpoints
   не продлевает offline permission; cached deny сохраняет запрет MQTT/RTP.
   Формат хранения versioned, атомарная запись, ACL, SN/generation isolation;
   решение preserve/clear при исчезновении поля и смене SN принять до кода.
6. `--check-upstream`: versioned JSON, per-channel verdict/exit codes,
   общая отмена/deadline; только enabled каналы, disabled=skipped.
   Не отправлять MQTT CONNECT/publish или медиапоток, не обходить admission deny;
   blocked_by_policy отличается от cert_invalid/probe_failed.
7. UI probes асинхронны; network failure не меняет trust mode. Args сохраняются
   в SCM/config/summary согласованно, restart/repair сохраняет выбор пользователя.

## IP bootstrap и резервные адреса в policy

Предложение: разделить получение policy и подключение рабочих каналов. Bootstrap
IP используется только для получения конфигурации; из его доступности нельзя
выводить, что на этом же IP доступны MQTT/Stream/RTP.

### Получение policy: последний независимый уровень

Обычный GET policy использует HTTPS SRV/policy/default host, затем известные
policy HTTPS fallback IP, затем отдельный bootstrap `87.242.100.34:443`.
Bootstrap не вызывает общий resolver снова и не требует предварительного GET policy.
Единственный worker объединяет запросы каналов, учитывает retry backoff и отмену;
служба и локальная диагностика запускаются без ожидания ответа.

| Параметр bootstrap | Значение / контракт |
|---|---|
| TCP destination | `87.242.100.34`, порт 443; numeric address напрямую, без DNS |
| TLS SNI и проверяемое имя | `iot-processing.ru`; имя сохраняется, даже если TCP идёт по IP |
| HTTP Host и path | `iot-processing.ru`, `GET /api/leo4proxy/policy` |
| Аутентификация | существующий terminal client cert + strict проверка server chain/name/EKU/time |
| Ответ | 200, JSON v=1, SN совпадает; redirect запрещён; deny сохраняется |
| Назначение | получить policy/endpoints; не универсальный IP для всех сервисов |

Не использовать IP как имя проверки сертификата и не требовать SAN с IP при
сохранении логического TLS-имени. До rollout подтвердить, что IP:443 с таким SNI
доходит до терминального mTLS virtual host и policy route; это пока требование,
не выполненная проверка. Простая замена WinHttpConnect host на IP не принимается
без доказательства SNI/validation на Win7; предпочтителен общий SChannel transport.

Адрес provisioned до первой попытки policy: предложение — опция
`--policy-bootstrap-ip` и порт, сохраняемые l4setup в SCM/config через production
install profile. Defaults исходного кода/config остаются пустыми по AGENTS;
profile задаёт указанный публичный адрес. Он должен попасть на терминал с первым
обновлением даже при отсутствии policy cache. Публичный IP не является секретом.
Изменение IP в policy не заменяет независимый начальный bootstrap: смена этого
адреса требует заранее доставленного нового install profile/списка bootstrap IP.
Ротацию bootstrap через подписанную конфигурацию можно выделить следующим этапом.

### Предлагаемое расширение v=1

Пример одного канала, остальные имеют ту же структуру:

```json
{
  "v": 1,
  "sn": "<SN>",
  "mqtt_rtp_allowed": true,
  "outgoing_https_allowed": true,
  "stop_facts": [],
  "endpoints_ttl_seconds": 86400,
  "endpoints": {
    "https": {
      "host": "iot-processing.ru",
      "port": 443,
      "fallback_ips": ["87.242.100.34"]
    }
  }
}
```

- `fallback_ips` — optional ordered array числовых IPv4 для первой версии;
  порт берётся из того же endpoint. Если нужны отдельные порты на резервных
  адресах, заменить массив на объекты `{ip, port}` до фиксации контракта.
- Для mqtt/l4rtp/l4stream IP и порты заполняются только после проверки реальной
  доступности сервисов; адрес bootstrap автоматически в эти каналы не подставлять.
- Не больше четырёх IP на канал; проверять numeric формат, дубликаты и bounds.
  Произвольные DNS-имена, loopback, unspecified, multicast и private/link-local
  адреса не принимать в публичном профиле без отдельного согласования.
- Backend получает hosts/ports/IP lists/TTL через env, defaults пустые;
  field unset исключается из JSON. Неверный fallback_ips опускается отдельно:
  валидные host/port и admission flags остаются пригодны. Для остальных частей
  endpoints сохраняется правило semantic validation выше.
- Предлагаемый срок endpoint cache: `endpoints_ttl_seconds`, default 24 часа,
  допустимый диапазон 300..604800 секунд. Клиент хранит received_at/expires_at,
  monotonic deadline в процессе и не продлевает срок одним рестартом.
  Значение и верхний предел — предложения для согласования.
- При 200 без endpoints или без fallback_ips очищать соответствующие старые
  записи, чтобы сервер мог убрать адрес; при network/invalid response сохранять
  предыдущие до expiry. При смене SN cache изолировать/сбросить.
- Admission обрабатывается независимо от IP данных: malformed IP не отменяет
  server deny и не сбрасывает grace record; cached IP не разрешает запрещённый media.
- Policy IP — административно заданные адреса из аутентифицированного ответа,
  не случайные A-результаты. Automatic persistent A-cache в первую версию не нужен.
- Диагностика различает `policy_ip` и `bootstrap_ip`, active/selected target,
  expiry, verdict и last_error; подробности только в local info branch.

### Поведение при отказах

| Ситуация | Результат |
|---|---|
| DNS работает, SRV отсутствует | Policy/default host, затем IP при transport failure |
| DNS полностью недоступен, есть действующий policy IP cache | Каналы подключаются к IP с прежней logical TLS identity; GET policy может обновить cache по IP |
| DNS недоступен, cache пуст | Worker делает GET policy по bootstrap IP; после valid 200 каналы используют полученные fallback_ips |
| DNS недоступен, cache expired | Сначала bootstrap/known IP для recovery GET; expired данные не разрешают рабочие channel connects. Использование stale HTTPS IP допускается только для strict recovery GET, аутентифицированный ответ нужен заново |
| DNS и bootstrap недоступны, cache пуст/expired | Локальная диагностика работает; каналы не имеют гарантированного upstream, bounded retries |
| Policy доступна, вернула deny | Применить deny, IP fallback не обходит admission |
| Сертификат IP target неверен | Reject, никакого insecure downgrade; другие кандидаты только с той же strict validation |
| Manual endpoint указан | Канал сохраняет manual target; bootstrap может восстановить admission policy, но не заменяет manual endpoint |

Для cold/no-DNS теста также проверить отсутствие скрытой DNS-зависимости в
TLS chain building: доступность intermediates локально/в server chain, AIA/CRL
fetch и выбранная revocation policy. Возможность полного восстановления зависит
от доступного IP маршрута и terminal cert; при отказе самого сервера гарантий нет.

## Порядок реализации после подтверждения

### Риск подмены IP и аварийное доверие

Фиксированный destination IP защищает от подмены DNS, но не аутентифицирует
удалённый сервер. Для перехвата TCP/TLS недостаточно простой подстановки source IP
в случайный пакет: атакующему обычно нужен контроль маршрута/сети или сервера.
Однако BGP hijack, компрометация шлюза/NAT/локальной сети, серверного ingress
или переход старого IP к другому владельцу делают этот риск практически значимым.
Количественная вероятность именно для этого IP не исследована; тяжесть последствий
для неподписанной policy оценивается как высокая.

| Вариант последнего IP-уровня | Подмена IP / последствие | Предложение |
|---|---|---|
| Полная chain/name/time/EKU проверка, pinned CA | Перенаправление без подходящего server cert отклоняется; DoS остаётся возможен | Default и первая попытка bootstrap |
| Проверка встроенного server SPKI pin вместо полной PKI-проверки | Чужой endpoint без соответствующего private key отклоняется; защита не зависит от DNS/online chain retrieval. Истечение/отзыв сертификата и компрометация pinned key требуют отдельной политики | Предпочтительный аварийный вариант, если причиной отказа является PKI/chain availability, с заранее доставленным набором pins и ротацией |
| TLS без какой-либо проверки server identity | Атакующий может завершить TLS своим cert, прочитать запрос, узнать SN/client cert, выдать собственные permissions/endpoints и отравить cache | Рассмотренный вариант «последний шанс»; не рекомендован для автоматического включения |
| Непроверенный TLS, но независимо подписанный policy payload | Подпись позволяет подтвердить policy, но transport identity/privacy не восстанавливает; для рабочих каналов проверка сервера всё равно нужна | Отдельное расширение протокола, если полный PKI bypass действительно необходим |

Client mTLS не заменяет server authentication: клиент доказывает владение своим
ключом собеседнику, но не устанавливает его личность. Клиентский private key не
передаётся; утечка публичного cert сама по себе не даёт возможность клонировать
терминал. Опасность здесь — поддельные ответы policy и данные последующих каналов.
Совпадение SN в ответе и allowlist IP не защищают от атакующего, который видит
запрос/сертификат и контролирует путь к разрешённому адресу.

Рекомендация: оставить строгий bootstrap по IP, отдельно предусмотреть
`ip_emergency_trust=spki_pin` для заранее provisioned bootstrap IP. Pins нельзя
загружать из непроверенной policy или получать TOFU в момент аварии. Нужны минимум
процедура доставки текущего/следующего pin и recovery при ротации ключа. Этот
режим ослабляет полную PKI-проверку, но сохраняет криптографическую идентичность.
Обычный DNS failure сам по себе не является причиной отключать cert validation.

Если при утверждении плана выбран полный `ip_emergency_trust=unverified`, явно
зафиксировать, что это принятие риска MITM, а не безопасный эквивалент strict:

- Disabled по умолчанию, отдельная сохранённая опция; не глобальный `insecure`
  для всех каналов. Применять только к заранее provisioned bootstrap IP, после
  исчерпания проверяемых recovery путей, с ограниченным временем и диагностикой.
- Не разрешать response endpoints менять этот режим, trust anchors/pins или
  список допустимых emergency bootstrap IP. Не следовать redirects.
- Без независимой подписи считать ответ unverified: не смешивать его с trusted
  cache, не сбрасывать известный authenticated deny и не продлевать admission
  grace. Если принимать такие endpoints временно, каждое рабочее соединение
  всё равно проверяет strict server identity; доверять permission grant нельзя.
- Для получения доверенной policy через unverified transport добавить отдельную
  подпись полного payload: SN, flags, endpoints, TTL, issue/expiry, anti-replay
  generation/nonce. Публичный verification key доставляется заранее; signing key
  отделён от TLS, проверяются replay, restart и неверное время на cold start.
- Если отключить server identity также для MQTT/HTTPS/media IP каналов, риск
  включает чтение/изменение их полезной нагрузки. Это отдельный выбор scope;
  разрешение на IP bootstrap не означает отключения validation во всех каналах.

Ограничения режима unverified не дают защиты, равной strict TLS. Без подписи
невозможно безопасно доверять admission policy при подмене маршрута. В первой
версии рекомендуется strict IP bootstrap; SPKI emergency — после согласования
pin lifecycle. Полный unverified режим остаётся оценённой альтернативой.

| Шаг | Изменение и точки входа | Проверка |
|---|---|---|
| 1 | Выбрать last-IP trust: strict / SPKI emergency / unverified с отдельными последствиями; согласовать TTL, IP format/deadlines; подтвердить IP:443 → mTLS policy с logical SNI, CA/SAN/EKU и IP доступность каждого сервиса | Contract review, pin lifecycle при emergency и разрешённая read-only инфраструктурная проверка; no-DNS chain requirements |
| 2 | Backend: schema/router/service/config; optional endpoints + fallback_ips + TTL, env-only значения, omit unset/invalid | Policy pytest old/new, invalid IP isolation, auth/deny/no-store; ruff check/format и pyright изменённого backend |
| 3 | Единый TLS transport с раздельными IP target/logical name; policy GET bootstrap без DNS и resolver recursion | Bootstrap numeric-connect, SNI/Host/name, wrong-cert, redirect/SN rejection и bounded HTTP parsing |
| 4 | Endpoint registry/resolver: SRV failover, policy hosts/IP, versioned cache/expiry/SN; интеграция четырёх каналов и diagnostics | Offline DNS/clock/connect tests, cold/no-DNS bootstrap, warm IP cache, denied/72h regressions; x86+x64 `/MT` |
| 5 | l4setup/l4superv: bootstrap profile/args, async probes, options/summary, SCM update/repair, info parser | UI/silent/cancel, restart/upgrade/repair parity и provisioned bootstrap без policy cache; x86+x64 builds |
| 6 | Проверить pack_zip/installer состав, docs/changelog; затем beta | Packaging smoke, staged E2E и проверяемый rollback; production отдельно |

Backend-first требуется до использования policy endpoints, но не до offline
resolver разработки. Никакого изменения shared/БД/MenuBuilder для этого scope.
leo4proxy здесь прозрачный MQTT transport, не новый MQTT publisher: presence и
топики не меняются. Если scope расширится на MQTT-клиент, сначала обязательный
вопрос о типе клиента по AGENTS; текущая концепция не даёт ответ на него.

## Матрица приёмки и оставшиеся проверки

- [x] Прочитан источник концепции, сверены API producer, C parser/transport,
  service install/restart consumer; проверены SDK/RFC первичные документы.
- [ ] Happy SRV и смена host/port без разрыва существующих endpoint-сессий.
- [ ] NXDOMAIN/timeout/invalid record/target `.`; primary down → secondary;
  negative cache, single-flight, bounded worker, deadline и shutdown.
- [ ] Полный DNS отказ cold/no-cache: GET policy напрямую по bootstrap IP,
  strict SNI/name/Host; получение IP всех enabled каналов и успешные connections.
- [ ] Полный DNS отказ warm/cache: IP connect без A/SRV запросов; TTL, рестарт,
  обновление/удаление IP сервером, SN switch, recovery-only stale HTTPS.
- [ ] DNS+bootstrap failure: свежий cache работает до expiry; cold/no-cache
  сохраняет local diagnostics и bounded retries, не обещает невозможный recovery.
- [ ] Неверный/отсутствующий IP block не теряет permissions/deny; старый клиент
  принимает valid расширенный JSON в прежних token/size limits.
- [ ] Wrong name/root, expired, wrong EKU, revocation и CA rotation;
  ни одного insecure downgrade или payload до проверки.
- [ ] Подмена IP route на тестовый чужой TLS server: strict/pin отклоняет;
  если выбран emergency mode — bounds, диагностика, отсутствие trust-cache
  pollution/admission bypass, pin rotation или signature/replay проверки.
- [ ] Old client/new server и new client/old server; malformed cache,
  duplicate JSON, missing/invalid endpoints, stale result после смены SN.
- [ ] Server deny/subscription deny/offline 72h; diagnostic probes не обходят deny.
- [ ] TLS identity отличается от connect target и в proxy HTTPS, и в policy GET.
- [ ] Standby без cert, hot-swap с прежним поведением, active endpoint snapshots.
- [ ] l4setup Auto/manual/mixed/silent/cancel; watchdog/repair сохраняет args.
- [ ] x86/x64 builds, Win7 terminal integration, package/rollback/beta E2E.

Тесты, builds, DNS probes, CA inspection, broker sessions и production verification
в ревью не выполнялись. Проверка ссылок и Markdown выполняется локально отдельно.

## Первичные технические источники

- [DnsQuery_A](https://learn.microsoft.com/en-us/windows/win32/api/windns/nf-windns-dnsquery_a): интерфейс без deadline; async DnsQueryEx указан для Windows 8.
- [Chain engine config](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/ns-wincrypt-cert_chain_engine_config): exclusive root отличается от additional stores, поддержка с Win7.
- [WinHttpConnect](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpconnect): задаёт начальный target; сам по себе не доказывает разделение TLS identity.
- [RFC 2782](https://www.rfc-editor.org/rfc/rfc2782): candidate priority/weight, target `.` и запрет alias для target.
- [RFC 5246](https://www.rfc-editor.org/rfc/rfc5246): порядок TLS 1.2 handshake, включая передачу клиентского сертификата до завершения.
- [Microsoft: manual Schannel validation](https://learn.microsoft.com/en-us/windows/win32/secauthn/manually-validating-schannel-credentials): отключение automatic validation требует собственной проверки server identity.
- [RIPE: routing incidents](https://ripe80.ripe.net/wp-content/uploads/presentations/3-202005-MANRS-RIPE80.pdf): prefix/route hijack способен перенаправить трафик и привести к interception/DoS.
- [Policy permission contract](term_arch-leo4proxy-server-permission.md): admission не смешивать с endpoint cache.
- [Компонентный контекст](../.agent-context/components/leo4proxy.md) и [handoff ревью](../.agent-context/tasks/completed/2026-10-04-leo4proxy-dns-srv-review.md).
