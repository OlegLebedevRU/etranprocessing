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
- Accepted release044d2d045ca145c854730a9260099efda4d529db, PR146.

## Production rollout completed

- Server-side wrapper installed root-owned0755. Applied logging-only source
  changes to production root/media/mTLS Compose files; private backups retained.
  No Docker daemon restart and no application image rebuild or pull.
- All13 production containers recreated sequentially using their full existing
  override chains, same image IDs, effective environments, commands, host settings
  and volumes. RabbitMQ hostname and persistent data volume preserved.
- camera-runtime exists only in an operator override; its policy is delivered as
  a persistent logging-only overlay generated from the accepted LOGGING constant.
- l4mcp had shared-env drift. Its effective running environment is preserved in a
  root-readable private overlay; new shared variables remain unset. This is an
  intentional compatibility artifact, not a secret copied into the repository.
  Keep it until an explicit l4mcp configuration upgrade reviews that drift.
- Rollout validation stopped on Env array order, explicitly unset Env names and
  Binds array order. Follow-up checks confirmed equivalent effective values/mounts;
  comparison normalized these representations before rollout resumed. These were
  verification failures, not application configuration/health failures.
- Recent bounded log snapshots retained privately before replacement. RabbitMQ
  and nginx snapshot reads each timed out124; the server-side deadline stopped
  them. Full old log history was not archived; container removal discards it.
- After rollout all13 report json-file/max-size20m/max-file3. Rabbit/Redis/signer
  Docker healthchecks healthy. PB health, MB OpenAPI, app1 docs, l4mcp health,
  media ingress health and Janus info all200. Three nginx config checks passed.
- Public MenuBuilder HTTPS returned200 from the workstation; localhost SNI/TLS
  probe with normal certificate verification also200. A production-host request
  through its public DNS route timed out; it is not counted as a passed check.
- RabbitMQ:39queues, ready0/unacked0,53consumers. Queue count can change with
  ephemeral MQTT subscription lifecycle; no backlog observed after reconnect.
- dockerd repeat CPU0.3% of one core, load0.75/0.60/0.55 after rollout.
- Isolated real rotation test using the existing Redis image, no network/volumes,
  read-only root,0.25CPU/32MiB:80000 synthetic lines; retained exactly3files,
  sizes20000370/7589838/20000302bytes. Probe removed after successful check.
- Runtime records, private compatibility overlay, backups of recent logs and
  results: production .etran-ci/runs/docker-log-safety/<accepted release>/.
  Backups of Compose source are adjacent *.pre-logging-* files, mode0600.
- Optional E2E mock container deliberately untouched; its future repository Compose
  policy includes rotation. No test credentials created and no DB migration.

## Related camera4623 follow-up

- Operator reported the client-side refusal asking for one verified port2 camera.
  Read-only MB-to-IoT gauge at16:39UTC: fresh snapshot, camera status unavailable,
  no discovery, no revision. Frontend therefore rejects before admission POST.
  This is separate from broker log configuration; do not weaken binding checks.
- Single read-only RPC160 refresh through the existing tenant1 service channel,
  task4790baf7-0b51-4842-8c01-8f4606422130, returned502/auth_failed at17:00:45UTC.
  Correlated gauge snapshotc676f48b-a83e-4e60-b8f6-0cc9b115dd9b has statusauth_failed,
  no discovery/contract revision. This establishes the current terminal-to-camera
  authentication failure; it does not establish which credential or target changed.
- Metadata-only query found no saved credential profile for this exact terminal,
  tenant and camera. There is no server profile to redeliver. Current UI and BFF
  require the advertised camera1.0.0 contract, method166 and revision for credential
  replacement; this auth_failed legacy snapshot provides none. Entering credentials
  through the current web form therefore is not an available recovery path yet.
  Need establish correct camera target/working credentials and terminal recovery
  support; asked user whether working access is known, without requesting a password
  in chat. No password guessing, power/binding changes or stream START performed.
- Live camera playback remains unresolved and is not claimed as deployment success.
