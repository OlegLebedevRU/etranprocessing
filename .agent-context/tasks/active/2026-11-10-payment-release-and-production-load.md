# Payment release and production load investigation

## Task intake and scope

- User authorized push/deploy of payment-report changes, followed by investigation
  of heavily loaded production components. Owners: PB payment ingestion, MB report
  API/UI/export, platform operations for Docker and RabbitMQ.
- Preserve tenant permissions, payment data, active terminal sessions and broker
  access policy. No changes to terminal clients or blocked-device status.
- Applied repository intake, verification, incident-triage, performance/resilience
  and API/data-ownership skills. MCP Ops unavailable; used SSH fallback.
- Observation: 2026-11-10, approximately 14:30–15:37 UTC. Evidence below is runtime
  observation, not authenticated browser E2E on production. No secrets or customer
  parameter values retained here.

## Release completed

- PR: https://github.com/OlegLebedevRU/etranprocessing/pull/145
- Accepted revision: aabf2dc4eb54a2a664c3a716bcba35b5c0c95188.
- Builder launcher built PB, MB backend and MB frontend; standard gates passed.
  Production received immutable images through the standard release helpers,
  preserving each service's current Compose override chain.
- PB image digest: sha256:023e659f3864a2c756700072684b4b6669d988b78415302211e7cf454714113d.
- MB backend digest: sha256:1c8fbab2c04a4246696f77916057e9e812e9911673310f3fa6eef46d5f95fe1c.
- MB frontend digest: sha256:75c0036366b604910bc1efbf2b7ec6a027417828fd9bdd8b4c477e4c499094df.
- Release metadata: production .etran-ci/runs/payment-reports/<accepted revision>/.
  Release records contain image metadata only. Neighboring containers, including
  signer, IoT, camera runtime and broker, were not recreated.
- Alembic upgrade completed at existing revision 034; no schema/shared-model delta.
- Production read-only report check, org475/date2026-11-10: all and cash each
  returned one payment with four parameters; card returned zero. OpenAPI filter
  default/enum verified. Deployed PB decoder passed synthetic UTF-8/CP1251 checks.
- Original transaction parameters already contain U+FFFD; historical restoration
  requires original terminal/provider input. No guessed backfill performed.
- Implementation and local/browser validation evidence:
  [payment handoff](2026-11-10-payment-report-details.md).

## Confirmed CPU anomaly and remediation

- Two logical CPUs. Actual /proc CPU deltas showed dockerd consuming 161.7% of
  one core; camera runtime about 7.8%, app1 2.9%, MB 1.9%, RabbitMQ 1%, PB 0.6%.
  Lifetime ps percentages are unsuitable for attributing current load.
- Docker 29.1.3 / Go1.24.4. Daemon goroutine profile showed two busy JSON log-tail
  paths: jsonfilelog.getTailReader -> loggerutils.getTailFiles/tailFiles -> pread.
- One orphaned docker logs reader for nginx had PPID1 and age about 13.7 days.
  A second RabbitMQ log reader was created during this investigation and also
  hung, amplifying/reproducing the anomaly. Stopping its SSH/probe parent alone
  had left the sudo/docker child alive.
- Sent SIGTERM only to the two readers after checking their exact PID/arguments.
  No daemon/container restart, log truncation or service configuration change.
- Controlled measurement: dockerd 138.9% before, 0.3% after; both log-tail stack
  frames disappeared. Final independent repeat remained 0.3%; load averages
  0.42/1.03/1.83 at approximately 15:36 UTC.
- This establishes stuck log retrieval as the observed CPU cause. Exact internal
  Engine defect and a version containing its fix have not been established.

## Other findings

- RabbitMQ queues: 40 inspected, ready0/unacked0. Two AMQP connections running,
  send_pending0. This is not a count of MQTT connections.
- In a ten-minute broker window, failed connection attempts from blocked devices:
  SN ending6209:88, 6305:45, 6307:44, 290:10. Their broker users are absent and IoT
  is_blocked=true. All four device records exist and are not deleted. This is
  consistent with enforced blocking; repeated reconnects generate warning noise.
  Historical svc_connect=true fields are not proof of current connectivity.
- Unblocked SN ending372 has a broker user, but supplied client ID appends
  `_extra` to the certificate SN. Broker rejects certificate/client-ID mismatch:
  101 attempts in the same window. Final bounded log check confirmed suffix.
  Do not weaken certificate binding or automatically create/unblock users.
- app1 roughly66-minute log window: no connection-reset/DB-connection-loss/timeout
  signatures found, ordinary polling requests dominate, no request flood.
  This is a bounded window, not proof of absence of every historical failure.
- camera-runtime had two startup log lines and no logged errors; its approximately
  8% CPU is consistent with its 100ms DB polling and is secondary to dockerd.
- RabbitMQ JSON log approximately612MB; nginx approximately6.4MB. Both inspected
  logging configs had no rotation options. Unlimited JSON-file growth is a risk,
  but file size alone does not prove the exact daemon busy-loop trigger.

## Follow-up plan and limits

Follow-up2026-11-10: PR146 accepted044d2d0 deployed the server-side15s+2s
log-reader deadline and json-file20m/3 rotation to all13 production containers.
Isolated real rotation test passed; optional E2E mock was excluded. See the
[runtime handoff](2026-11-10-docker-log-safety.md). Camera4623 refresh RPC160
subsequently returned auth_failed; this requires separate camera access recovery.

1. Apply server-side timeout/kill-after to diagnostic docker logs calls, including
   cancellation cleanup; terminating the local SSH process is insufficient.
2. Add conservative JSON log rotation through the repository's deployment config
   in a separate reviewed change; preserve existing logs and plan recreation.
   Official defaults: https://docs.docker.com/engine/logging/drivers/json-file/.
3. Correct device372 client identity in the owning terminal client configuration;
   inspect blocked-device retry/backoff policy without removing the block.
4. Investigate a tested Docker Engine fix if the tail-reader spin recurs; do not
   claim a particular release fixes this without reproducer/version evidence.

- No MQTT client implementation or access-policy changes made in this task.
- No new tests for this read-only diagnosis/documentation. Existing release gates
  and application health checks passed; production authenticated UI E2E not run.
- Cleanup: both stuck diagnostic log readers terminated; no test credentials or
  payments created. Durable release metadata retained for rollback/audit.
- Existing root working-tree changes were preserved. This handoff is a local
  follow-up artifact and was not included in the already deployed release.
