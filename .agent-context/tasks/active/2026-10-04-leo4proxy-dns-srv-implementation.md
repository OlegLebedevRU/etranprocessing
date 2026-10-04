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

Не выполнены: установка native release; clean 773 l4setup, Win7
GUI/manual/cancel/repair/watchdog и full cold/warm outage всех каналов; video E2E.
Существующий terminal cert не удалялся, local services не переустанавливались.
После clean l4setup оператор предоставляет summary/log для runtime acceptance.
CRL/OCSP не проверяются; IPv4/первый A-address остаются ограничением транспорта.
DNS zone не менялась; SRV alias publishing prohibition не проверен отдельно.

Контекст: [implementation packet](../../../docs/term_net-leo4proxy-dns-srv-implementation-context.md).
