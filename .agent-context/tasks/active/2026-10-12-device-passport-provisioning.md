# Passport provisioning route and terminal 773 collision

## Task intake
- Owner: MenuBuilder frontend; existing SU-only admin API and app1 provisioning.
- Scope: fix passport action contract; no broker configuration, detector or native changes.
- Flow: passport device_id/org_id/SN → admin terminal lookup → internal terminal.id → existing provision-iot → app1.
- Invariants: reject missing, ambiguous or mismatched identity; retain server SU authorization; never claim unblocking from provisioning HTTP success alone.
- Validation: frontend build, unit suite, changed-file secret scan; owner-triggered live provisioning remains pending.

## Runtime findings, 2026-10-12
- Terminal 773 has current certificate serial 60928AD28DB05DE847B4FBCD1185D2881C6ADF50, matching ProcessingBackend DB and authenticated ingress.
- Renewals PIN 328 and 330 issued at 11:27:29 and 11:28:16 UTC respectively. Their certificate validities exactly match the app1 incident.
- app1 incident 77, SN_COLLISION, detected 11:28:29.336373 UTC, flapping_count=3; connection.is_blocked=true.
- RabbitMQ logs: write permission refused at 11:28:29, invalid credentials from 11:28:38. Current broker account and permissions for this SN absent.
- Fresh terminal proxy policy permits MQTT and HTTPS, stop_facts=[]; ProcessingBackend terminal.is_active=true.
- Likely rotation overlap triggered collision detector. Exact detector algorithm and provisioning reset semantics require app1 source verification; not established by this UI fix.
- User passport POST /api/admin/terminals/773/iot-provision returned 404 in MenuBuilder, without reaching app1. Correct existing endpoint is /api/admin/terminals/{terminal_id}/provision-iot, using internal ID 1 for device 773.

## Change and checks
- Reuse admin list/provision API, resolve and verify org_id/device_id/SN before POST.
- Validate success/error and refresh IoT state; report unblocking only if is_blocked=false.
- Local frontend build passed; 100 unit tests passed, including seven identity/route regression cases. Diff whitespace and changed-file credential scan passed.
- No Python/backend/shared code changes; no backend tests required for this change.
- No live provisioning, certificate issuance, service restart or incident reset performed by agent.
- Release evidence and owner live acceptance: pending.
