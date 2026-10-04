# leo4proxy DNS/SRV/IP: выпуск 1.10.0 и runtime acceptance

## Scope / ownership
Пользователь подтвердил внедрение, policy deploy и tools release; отдельно разрешил
builder → registry → production и media CA-пару. ProcessingBackend владеет policy,
leo4proxy transport/routing, l4setup SCM args, l4superv watchdog. MQTT-клиенты suite
не изменялись; FRONT/BACK и SQL-примеры не исследовались.

## Выполнено / evidence 2026-10-04
- Policy endpoints/TTL producer в main (PR #5), deployment configuration PR #10.
  Ruff/format/pyright, 156 backend tests, health и настоящий mTLS GET прошли.
- Media сертификат/guard в main и production: [handoff](../completed/2026-10-04-media-tls.md).
- Native source в main (PR #11): leo4proxy 1.8.0, l4superv/setup 1.10.0.
  SRV/policy/default/IP, pinned CA/name validation, numeric bootstrap, routing cache,
  local info, Auto/manual settings, сохранение SCM options и watchdog mirror.
- x86/x64 real upstream TLS: MQTT/RTP source=srv, HTTPS source=default, valid;
  disabled stream skipped. Прямой bootstrap policy GET valid в обеих архитектурах,
  strict=true; DNS/cache не используются, application media трафика не отправляли.
- Endpoint, admission, credential ownership, certificate selection, loopback TLS,
  setup pipeline (11), certificate phase (2), custom SCM args и startup tests passed.
- Full tools/build_dist.cmd 1.10.0 exit 0; x86/x64/default outputs; 61 embedded files
  каждой архитектуры совпадают со staging. Build profile содержит provisioned IP.
- Первый full build failed: missing OpenH264 SDK; восстановлены заголовки и 6 static
  libraries из локального worktree релиза 1.9.3, hashes совпадают. Повторный gate passed.
- Unsigned setup SHA256: 62864c1becb0e510fffbc08e49213a1784b72eb8c12beb3cc2b959c4076c569b.
  После подписи setup SHA256: d06a430e34dd38b8cb2cef05d8156e6d52471b7096291f7253ed9485fc5e1595.
- Оператор подтвердил подпись; 19 EXE (18 staging + setup) Valid, timestamp present;
  повторный payload gate 61/61 per arch passed. Signed outputs синхронизированы.
  Manifest из clean main 68ad0ccd28bc2ffae239aa5a38512daf19475cab, dirty=false.
- Registry 1.10.0 опубликован: все 3 uploads HTTP 200, HEAD digest match.
  Python GET усечён (28825629 вместо 29686840); полный curl GET setup/manifest/
  SHA256SUMS подтвердил sizes/hashes, скачанный setup Authenticode Valid.
  [Release record](../../../artifacts/l4tools/1.10.0.json).

## Runtime acceptance / remaining
Подпись и публикация завершены. Не rebuild components после подписи.
Процедура: tools/release/README.md и native-windows-tool-change skill.

Install773/Windows10 x64 13:58 UTC: four services RUNNING, local smoke passed,
cert reused (not reissued), MQTT/HTTPS TLS valid, stream disabled. Degraded/12
с RTP probe_failed оказался pipe exit race в setup reader: live reader 5/5
теряет финальную строку, исправленный 5/5 valid; прямой installed proxy probe
MQTT/HTTPS/RTP strict valid. Setup-only 1.10.1 исправляет drain after exit и
upstream error_reason для всех каналов. Детерминированные pipe tests (3) и
pipeline tests (12), certificate phase/startup/custom args passed x86/x64.
1.10.1 подписан и опубликован из clean main 7259be4bf2f7ed90ebdc3c61e7901392020df8d2.
19 EXE signatures/timestamps Valid; 61/61 payload gate passed. Оператор повторно
подписал компоненты: полные hashes изменены, все 18 PE code/data/resources
совпадают с 1.10.0. Штатный publisher полностью скачал все 3 файла, sizes/SHA256
matched. Setup SHA256 092d8e338a0d0fa30bc7578816fc6d21370b8592b49e90cb3f294815e4da343f,
29687864 bytes. [Release record](../../../artifacts/l4tools/1.10.1.json).
Оператор выполнил полный Upgrade 1.10.0 → 1.10.1 (не smoke-only), 14:18:05–46 UTC:
drainage/rollback directory/payload swap прошли, SCM options сохранены, четыре
службы RUNNING. Сертификат reused, hardware fingerprint matched. Local smoke,
ffmpeg capture, l4desk и remote_input available; MQTT/HTTPS/RTP TLS valid, stream
skipped (disabled). Итог ready/0. Pending reboot warning сохранился; reboot не проверен.
Предоставленный /info: routes_active=true, policy flags allowed, MQTT source=srv,
HTTPS source=policy. RTP counters=0 и endpoint пустой: живой media session не доказан.
Release 1.10.0 не перезаписывается.

Оператор дополнительно подтвердил работающий видеопоток после Upgrade773.
Это operator runtime evidence; агент не измерял browser decode stats/first-frame
и отдельные stop/reconnect/late-join сценарии в 1.10.1.

Не выполнены: clean enrollment через PIN, Win7
GUI/manual/cancel/repair/watchdog и full cold/warm outage всех каналов;
расширенная video E2E matrix.
Существующий terminal cert не удалялся. Оператор установил службы 1.10.0 и обновил 1.10.1;
после этого агент не переустанавливал и не останавливал local services.
Основная установка/upgrade/TLS приёмка 773 подтверждена предоставленным логом и /info.
CRL/OCSP не проверяются; IPv4/первый A-address остаются ограничением транспорта.
DNS zone не менялась; SRV alias publishing prohibition не проверен отдельно.

Контекст: [implementation packet](../../../docs/term_net-leo4proxy-dns-srv-implementation-context.md).
