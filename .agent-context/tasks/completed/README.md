# Completed handoffs

Краткие завершённые результаты по [шаблону](../handoff-template.md).
Это история задачи, не замена актуальным компонентным карточкам.
Логи/длинные исследования — docs/history; secrets и connection profile запрещены.

## Индекс

- [2026-10-04 — tools 1.10.1 docs audit](2026-10-04-tools-docs-1101.md) — актуальные версии, установка/сеть/TLS, operator video evidence; документированы smoke-only и DNS-probe ограничения.

- [2026-10-04 — leo4proxy DNS/SRV/IP release](2026-10-04-leo4proxy-dns-srv-implementation.md) — policy/media внедрены, signed tools 1.10.1 опубликованы; Upgrade773 ready/0, strict MQTT/HTTPS/RTP TLS valid, видео подтверждено оператором; enrollment/outage и расширенные E2E сценарии не проверены.

- [2026-10-04 — leo4proxy DNS/SRV review](2026-10-04-leo4proxy-dns-srv-review.md) — статическая проверка концепции, решения на согласование и контекст внедрения.

- [2026-10-03 — landing login/register, SmartCaptcha и номера терминалов](2026-10-03-l4desk-auth-captcha.md):
  опубликовано; исправлен anonymous registration redirect; manual challenge/auth E2E не выполнялся.

- [2026-10-03 — подписки L4Desk, production release](2026-10-03-l4desk-terminal-subscriptions-release.md):
  builder/registry/pull, единая ревизия 8bb0359, schema029, YooKassa OFF,
  новый UI и duration без денег; hardware/provider E2E не выполнялся.
  [Реализация](2026-10-03-l4desk-terminal-subscriptions-handoff.md),
  [исторический baseline](2026-10-03-l4desk-billing-simplification-context.md).

- [2026-10-02 — l4tools1.9.7 quick actions](2026-10-02-l4tools-197-shortcuts.md):
  supervised F12/Win+D/Alt+F4 enabled; signed registry release, other EXEs unchanged, live deferred.

- [2026-10-02 — L4 Tools1.9.6](2026-10-02-l4superv-service-dependencies.md):
  SCM dependencies/status/log ACL, clean Install773 ready/0, signed registry release; reboot not tested by user choice.

- [2026-10-01/02 — server stack consolidation](2026-10-01-server-stack-consolidation.md):
  l4media source/registry flow, IoT75 contract/billing and final MB auth records.

- [2026-10-01 — l4mcp900–999](2026-10-01-l4mcp-user-events.md):
  структурированная отправка, read-only tenant history, корреляция и E2E.

- [2026-10-01 — MenuBuilder history900–999](2026-10-01-menubuilder-user-events.md):
  tenant-scoped MCP reader, internal IoT integration и план следующего E2E.

- [2026-10-01 — l4con user events](2026-10-01-l4con-user-events-handoff.md):
  native IPC/remote Job, теги446–448, ограничение потока и реальная приемка773.
- [2026-09-30 — Git/release и временные локальные копии](2026-09-30-git-release-local-residue-review.md):
  профили GitHub, условный builder-маршрут и проверяемый cleanup manifest.
- [2026-09-30 — L4 Tools 1.9.2 and remote control deployment review](2026-09-30-l4tools-192-deploy-review.md):
  source and registry provenance, deployment evidence, browser trace and remaining terminal visual check.
- [2026-09-27 — 17F grace runtime](2026-09-27-l4d-17f-grace-runtime.md):
  monthly charge, blocked stop живого видео и mock recovery; общая
  приёмка 17F остаётся отдельным gate.
- [2026-09-11 — агентская рабочая память](2026-09-11-agent-context-bootstrap.md):
  документальное внедрение, проверка ссылок/формата, runtime/E2E и автоматизация отложены.
- [2026-10-01 — L4 Tools 1.9.3](2026-10-01-l4tools-193-release-handoff.md):
  leo4proxy 1.7.2, l4con 1.9.3, подпись всех EXE с timestamp и проверенная публикация в registry.

- [2026-10-02 — L4 Tools 1.9.4 stabilization/UI/signed release](2026-10-02-l4tools-stabilization-ui-release.md):
  NO_PROXY, new-CA standby, lifecycle/credentials safety, accepted UI, real policy/Upgrade/Repair and verified registry publication.

- [L4 Tools 1.9.5: fresh-install correction and signed publication](2026-10-02-l4setup-fresh-install-fix.md) — Upgrade773 ready/0; full registry bytes verified; fresh x86 E2E remains untested.

- [Media TLS, 2026-10-04](2026-10-04-media-tls.md) — canonical CA-пара, startup/deploy gate, image digest и runtime-проверки.
