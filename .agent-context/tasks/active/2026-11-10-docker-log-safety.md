# Docker log safety rollout

## Task intake

- User requests implementation and deployment of server-side log-read timeout and
  Docker log rotation after the confirmed dockerd log-tail CPU incident.
- Owner: platform operations; no application business logic, migrations, MQTT
  client changes or terminal access-policy changes. Producer: operator diagnostic
  request/container stdout; consumer: bounded Docker CLI/Docker JSON driver.
- Skills: repository intake and verification matrix. Isolated branch from accepted
  main aabf2dc; preserve unrelated root changes. MCP Ops unavailable; SSH fallback.
- Invariants: preserve running images, full Compose chains, environment, volumes,
  broker identity and data. Sequential recreation and application health checks.
- Acceptance: forced kill within17s; no surviving reader; only logging delta in
  Compose; effective20m/3 options on recreated production containers.

## Implementation and pre-deploy evidence

- Small POSIX wrapper and logging-only Compose installer; repository production
  Compose references updated. Active diagnostic examples use the wrapper.
- Targeted installer tests4passed; Ruff/format/Pyright passed. No application code
  changed; application test suites/builds intentionally not run.
- Production Linux fixture: invalid arguments rejected4/4; TERM-ignoring fake
  Docker reader killed after17.00s, returncode-9, no surviving reader. Temporary
  fixture directory removed. No real container was touched by this test.
- Dry-run resolved comparison passed for root, media and mTLS Compose projects.
  Optional E2E project lacks its original external environment at config resolution;
  do not reconstruct secrets or recreate its mock gateway. Repository E2E policy
  is updated for future invocations, but current fixture is outside rollout.
- Runtime preflight: RAM1877MiB available, disk52%, load0.16/0.27/0.33.
- Rollout and final runtime results: pending; do not treat this intake as deploy evidence.

## Related camera4623 follow-up

- Operator reported the client-side refusal asking for one verified port2 camera.
  Read-only MB-to-IoT gauge at16:39UTC: fresh snapshot, camera status unavailable,
  no discovery, no revision. Frontend therefore rejects before admission POST.
  This is separate from broker log configuration; do not weaken binding checks.
