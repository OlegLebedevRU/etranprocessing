# leo4proxy

## Назначение / владельцы
Native Win32 proxy для MQTT/HTTPS/Stream/RTP. ProcessingBackend владеет admission
policy; leo4proxy — transport и routing cache; l4setup — SCM settings;
l4superv — запуск/watchdog и local diagnostics. БД не меняется.

## Контракты
- DNS SRV → bounded single-flight resolver → channel candidates.
- mTLS GET /api/leo4proxy/policy → admission + optional endpoints/TTL.
- l4setup → SCM ImagePath + leo4proxy/service-args.txt → watchdog/repair.
- /_leo4/info endpoints.channels → l4superv HTTPS source/target diagnostics.

## Инварианты
Strict CA/name/time/serverAuth во всех исходящих каналах, включая IP. Numeric TCP
сохраняет logical TLS/SNI/HTTP Host. Bootstrap используется только для GET policy;
неверифицированный TLS и SPKI emergency не реализованы. No redirects/admission bypass.
Routing cache отделён от deny/72h grace, bound к SN/TTL; stale IP только recovery GET.

## State / реализация
Explicit CLI → SRV → verified SRV LKG → fresh policy host → default → fresh policy IP.
Для recovery GET добавляется provisioned bootstrap IP и допускается stale policy IP.
SRV `.` fail-closed, 24 DNS slots, caller wait ≤2s, TTL 30–300s/negative 30s.
Cert standby/hot-swap и credential ownership сохраняются.

## Исходники
- [endpoints.c](../../tools/leo4proxy/src/endpoints.c), [TLS](../../tools/leo4proxy/src/schannel_tls.c)
- [policy](../../tools/leo4proxy/src/policy.c), [main](../../tools/leo4proxy/src/main.c)
- [policy producer](../../ProcessingBackend/backend/app/services/leo4proxy_policy.py)
- [setup services](../../tools/l4setup/src/services.c), [smoke](../../tools/l4setup/src/smoke.c)
- [watchdog](../../tools/l4superv/src/orchestrator.c), [config](../../tools/l4superv/src/config.c)

## Проверка / статус 2026-10-04
Backend 156 tests + quality passed, policy deployed; direct mTLS confirms endpoints.
Media CA pair and guarded image deployed; [media evidence](../tasks/completed/2026-10-04-media-tls.md).
Native x86/x64 build, admission/credential/certificate/loopback tests passed;
strict numeric bootstrap GET valid in both architectures. Signed tools 1.10.0
published, payload gate 61 files per arch. Install773/Windows10 x64: four services
running, reused valid certificate, local smoke passed. Installer reported false
RTP probe_failed: child pipe exit race reproduced 5/5; direct installed proxy RTP
TLS valid. Fixed reader returns valid 5/5; signed setup 1.10.1 published.
All 19 signatures/timestamps valid; complete HTTPS downloads matched sizes/hashes.
Components re-signed, all 18 PE code/data/resource sections match 1.10.0.
Operator Upgrade773 1.10.0 → 1.10.1, 14:18 UTC: ready/0, SCM args preserved,
four services running, local smoke/capture/input available; MQTT/HTTPS/RTP TLS valid.
Certificate reused. /info перед запуском видео: MQTT source=srv, HTTPS source=policy,
RTP counters zero. После обновления оператор подтвердил работающий видеопоток;
browser decode telemetry и расширенная outage/E2E matrix агентом не измерялись.

## Ограничения / следующие проверки
Setup1.10.1 разбирает/logs --smoke-only, но engine не учитывает флаг: нельзя
считать его гарантированно диагностическим. Проба network — DNS iot.leo4.ru,
может дать degraded при работающем IP recovery. Source help proxy о insecure устарел.
IPv4 и первый A-address; CRL/OCSP не проверяются. Локальное время влияет на trust.
Cache: HKLM Endpoints REG_BINARY + ProgramData/Leo4Proxy/endpoints.cache, timestamp
и policy JSON; ACL SYSTEM/Administrators. File storage degradation оставляет snapshot
в памяти. Public root embedded; private keys remain in CNG/server mounts.
Нужны clean certificate enrollment через PIN, GUI/manual/cancel/watchdog/repair, Win7/full outage всех
каналов и расширенная video E2E matrix. Не считать TLS probe доказательством decoded frames.

## Источники
[Implementation context](../../docs/term_net-leo4proxy-dns-srv-implementation-context.md),
[admission](../../docs/term_arch-leo4proxy-server-permission.md).
Первичное ревью HEAD c58f607: [historical handoff](../tasks/completed/2026-10-04-leo4proxy-dns-srv-review.md).
