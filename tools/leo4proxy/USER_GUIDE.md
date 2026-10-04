# Leo4Proxy — руководство оператора

Опубликованный выпуск: leo4proxy **1.8.1** в signed tools **1.10.2**, 2026-10-04.
Выпуск **1.8.1 / tools 1.10.2** уточняет network deadlines, IP fallback reserve
и параллельную диагностику; [матрица](../../docs/term_net-leo4proxy-resolving-reliability-matrix.md).
Подписи, timestamps, payload и полные HTTPS downloads проверены.
Upgrade терминала 773 до 1.10.2 подтверждён оператором: ready/0, proxy 1.8.1, MQTT/HTTPS/RTP TLS valid.
Установка всего suite: [руководство инженера](../../docs/term_tool-user-guide.md).
Полный CLI и модули: [README](README.md).

## Установка и сертификат

Запустите подписанный `l4setup.exe` из релиза 1.10.2. Существующий действующий
сертификат переиспользуется; при отсутствии сертификата можно ввести PIN или
отложить активацию. Без действующего terminal cert proxy остаётся в ожидании,
локальная диагностика доступна; внешние MQTT/media не активируются.
Клиентский сертификат находится в `LocalMachine\MY`, private key — в Windows CNG.

Для отдельного администрирования proxy сохранены `leo4proxy_install_start.cmd`,
`leo4proxy_start.cmd`, `leo4proxy_stop.cmd`, `leo4proxy_restart.cmd`,
`leo4proxy_status.cmd`, `leo4proxy_stop_uninstall.cmd`. Они управляют службой;
используйте l4setup для согласованной установки/обновления всего комплекса.

## Локальные и внешние адреса

| Канал | Локальный адрес | Логический upstream по умолчанию |
|---|---|---|
| MQTT приложения | Mosquitto `127.0.0.1:1883` | bridge через proxy |
| MQTT proxy | `127.0.0.1:18883` | `dev.leo4.ru:8883` |
| HTTP proxy / metadata | `127.0.0.1:18443` | `iot-processing.ru:443` |
| Reverse HTTPS | `0.0.0.0:443` | локальный `127.0.0.1:8000` |
| RTP/RTCP tunnel | UDP `127.0.0.1:5004/5005` | `dev.leo4.ru:8443` |
| TCP Stream (optional) | `127.0.0.1:8554` | `dev.leo4.ru:8443` |

RTP включён штатными service args l4setup; отдельный запуск leo4proxy требует
`--rtp-tunnel`. Stream по умолчанию выключен; `--stream` включает его отдельно.
Не считайте отсутствие локального reverse backend на :8000 отказом MQTT/RTP.
Discovery: имя из SAN DNS либо `leo4-<sn>.local`; фиксированное `terminal.local`
не является общим именем всех устройств.

## Auto, policy и IP recovery

Auto: SRV → verified SRV LKG → fresh policy host → default → fresh policy IP.
Ручной `--mqtt-remote`, `--http-remote`, `--stream-remote` или `--rtp-remote`
приоритетнее Auto. SRV owners по умолчанию:
`_mqtt._tls.dev.leo4.ru`, `_https._tcp.iot-processing.ru`,
`_l4stream._tls.dev.leo4.ru`, `_l4rtp._tls.dev.leo4.ru`.
`--no-srv` отключает только SRV, target `.` запрещает обход канала.
Bootstrap IP задаётся `--policy-bootstrap-ip`, порт — `--policy-bootstrap-port`
(default 443); он используется только для recovery GET policy.

При TCP к fallback IP сохраняются логическое имя TLS/SNI и HTTP Host.
Проверка CA/name/time/serverAuth обязательна, встроенный CA — exclusive trust
для исходящих каналов. `--secure` сохранён для совместимости; strict TLS уже
действует по умолчанию. Текст старой CLI help про lax/insecure устарел.
Сценарий отключения server certificate validation в релиз не включён.
CRL/OCSP не проверяются, IPv4/первый A-address остаётся ограничением.

## Диагностика без перезапуска служб

```cmd
C:\l4tools\leo4proxy\leo4proxy.exe --get-sn
C:\l4tools\leo4proxy\leo4proxy.exe --test-cert
curl.exe --noproxy "*" http://127.0.0.1:18443/_leo4/info
C:\l4tools\leo4proxy\leo4proxy.exe --check-upstream --rtp-tunnel --policy-bootstrap-ip 87.242.100.34
C:\l4tools\leo4proxy\leo4proxy.exe --check-policy-bootstrap --policy-bootstrap-ip 87.242.100.34
```

`--get-sn` возвращает только SN. `--test-cert` проверяет выбранный сертификат и
получение Schannel credentials, не доказывает соединение с upstream.
`--check-upstream` делает TLS handshakes включённых каналов, учитывает cached
admission и выдаёт JSON channel/verdict/host/port/source/logical_name/strict.
Для отключённого канала — skipped. Диагностика не отправляет MQTT CONNECT или
видеоданные; ручные service options следует передать также в probe.
`--check-policy-bootstrap` выполняет настоящий mTLS GET policy через numeric IP,
без DNS и записи cache. Admission deny остаётся обязательным.

`/_leo4/info` показывает identity, listeners, логические upstreams, policy и
`endpoints.channels`: выбранный target/source последнего служебного соединения.
Media endpoint может быть пуст до первого потока; diagnostic процесс не обновляет
endpoint работающей службы. RTP lazy connect открывает туннель по первому UDP.
`routes_active=true` и TLS valid отдельно не доказывают показ видео.

## Проверки релиза 1.10.1

Upgrade773/Windows10 x64 прошёл ready/0; MQTT/HTTPS/RTP TLS valid,
Stream disabled/skipped. Оператор подтвердил работу видео после обновления.
При отказе диагностики читайте `C:\l4tools\install_summary.json` и
`C:\l4tools\l4setup.log`: неверный сертификат — cert_invalid, сетевой/другой
отказ — probe_failed, timeout — исчерпан бюджет. 1.10.1 исправляет потерю
последней строки verdict из pipe. Cold/warm DNS outage, новое PIN enrollment
и Win7 этим подтверждением не проверены.
