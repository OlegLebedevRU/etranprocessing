# FM on demand: implementation, deployment and pilot773

## Scope and source ownership

User authorized implementation/deployment, selected MQTT extra_service and pilot773,
and approved UAC. Original dirty workspaces were preserved. Native source belongs
to OlegLebedevRU/l4tools; app1 belongs to OlegLebedevRU/iot-rpc-rest-app.
Owner subsequently reported successful manual suite66 installations on119 and55.
No agent-controlled terminal update beyond773 was performed.

## Accepted and deployed result

- etranprocessing PR158, source a2b50284e87c450b103b989f5b33d25e6c7544fa:
  PB/shared migration035 and package0.1.2, MB034/035 bridge, selected-only UI.
- IoT PR171, source879a9e654fbad9269cc9ee03443e2d7926ba437b: bounded probe on
  existing fmc, no lease/task/persistent message allocation.
- Native PR13/14, signed66 sourcec0bc689ef25613dc18a7f79e3c1b6ed8220d0187:
  L4Con1.12.5;18 unchanged signed EXEs reused from55; x86/x64/default built.
- Private config-preserving55->66 x64 kit PR15,
  source2380217a61d119a342f7d9dc7aa9a98034ef4d8e. No stable/remote admission.
- Actual production digests and sanitized measurements:
  [machine evidence](../../../docs/history/2026-11-12-fm-on-demand-evidence.json).

MB bridge was deployed before PB migration035; then app1 and frontend. Every
server replacement verified identical environment/mounts/network and neighbor
container IDs. Nginx was not restarted for static publication. Durable Compose
overlays in the verified rollout directories are running configuration, not
throwaway files. Keep them until a reviewed future deployment replaces them.

## Contract

Explicit UI -> MB admission -> PB pending -> IoT fmc -> native metadata helper
-> current pinned mTLS PB result -> actor/view/cert/instance-bound single-use
proof -> existing lease/start/ACK. Probe expiry10s, BFF wait7s, proof15s; helper
idle grace30s. No idle/startup hello, eager child or idle respawn in new agents.
Active freshness uses validated start/renew ACK only. No new topics/client IDs,
retained commands, auth cache, file-byte relay, file-write privileges or payment
pool sharing. One pending/proof row per terminal, no periodic sweeper.
Legacy selected-terminal compatibility remains capability-based; legacy hello
is retained until an explicitly authorized fleet cutover. Allow old45s freshness
to expire after a native upgrade before interpreting cached legacy readiness.

## Checks and evidence levels

- PB full399 passed/2 skipped; final identity regression91 passed. Isolated real
  PostgreSQL18 tested034->035, rollback/reapply and concurrent proof single use.
- MB local full1232 passed/25 skipped; final bridge/contract checks passed;
  standard builder full1258 collected and passed without failures.
- Shared65 passed; schema035 and normalized source provenance captured.
- IoT local1218 passed/13 skipped; standard builder full tests passed.
- UI161 unit and13 browser model tests passed, production build passed.
  Final idle-label-only correction uses existing unchecked reason; build passed.
- Modified Python owners passed Ruff/format/Pyright; scoped rollout helper5 passed.
- Actual native elevated build_all/native_x86/native_x64 all0 at15:41:47Z,
  including ACL/SCM, physical-token and owned-Job isolation checks. Full native
  inputs were rebound before release; unchanged tools were not rebuilt/retested.
- Release controller277 tests passed after private kit support. Installer follow-up
  PR16/17 adds the former fixed embedded CA bootstrap and effective-admin gate;
 284 controller tests and x86/x64 isolated trust/token tests passed. Published68
  uses identical agent EXEs; only setup changes. Its offline x86 preflight with
  default launch verb returned0; normal Windows UAC was exercised.

## Real pilot773

- Native update operation c612e229-40d6-4df2-8e1e-bd9d69c9226f:
  preflight0, update0, durable status0. All four services run from66.
  Ten actual installed x64 EXEs match signed layout; arguments/accounts unchanged.
  One certificate with private key unchanged; four ordinary configs unchanged,
  nine launcher selectors changed as expected.
- API: malformed UUID422, wrong view404 before lease allocation, correlated probe
  returns1.12.5, actual start ACK and42-entry navigation response.
- Live Chrome on production: exactly one probe and one session for a click;
  four renewals each matched a terminal applied_expires_at ACK; active past the
  original deadline; normal close/stop. Probe24327a04-5c34-4a01-ae89-7bad9ff6f419,
  session7ce9f2f4-da4b-4d41-b642-9bec36db22df;16:25:09Z..16:26:16Z.
- Real unselected fleet page16:11:10Z..16:21:10Z,600015ms:0 FM requests.
- Native WMI ProcessStartTrace16:27:19Z..16:58:22Z,1863s: no initial FM child,
  no L4Con process starts, service remained running. FM WinHTTP is confined to
  that owned child, so source plus event evidence establishes no idle FM HTTP;
  no packet capture was taken.
- Read-only DB samples16:28:02Z..16:58:00Z,1801s/31 samples: last_seen_at and
  xmin of both fm_agents and fm_readiness_probes unchanged. This is scoped773,
  not a claim that the entire legacy fleet stopped sending hello.
- One earlier live probe2d69db67-0d89-4934-b69b-43535f165e89 received no agent
  result:14 bounded polls then timeout, no session. A later explicit probe
  succeeded. Precise transport/cold-start cause was not proven; no automatic
  retry or legacy fallback after timeout was introduced.

## Limits and next agent

No full OS reboot, live certificate rotation, Redis restart or every physical
crash/rollback permutation was injected. Local/integration negatives are not
those hardware tests. Further native rollout and legacy endpoint removal need
the next explicitly identified cohort. [Agent assignment](../active/2026-11-12-fm-on-demand-agent-task.md)
contains the contract and remaining scope. A15s legacy schedule corresponds to
5760 hello/day/device; the measured result here is30-minute zero idle, not a
whole-day fleet traffic comparison.

## Cleanup and retained records

Temporary PostgreSQL container fm-demand-pg-20261112 was removed on builder;
owned SSH tunnel, preview server and browser contexts were stopped. Read-only
idle samplers exited and WMI subscription was unregistered. No ad hoc JWT was
minted; provided test token stayed in memory, expired naturally and its original
user-owned file was preserved. Signed releases, failed65/67 evidence, protected
native operation/publisher records and live Compose overlays are retained.


## Final static publication

PR159 accepted final idle-label correction and this evidence. Standard builder
161 tests and production build passed on13a246869ebd83f9ad7732cec25d5ca8e49cbc7a.
Published frontend digest
sha256:513d5405a41636014749344fb7d422067978698c5dcb1ca0df7d9f74a2c94452
passed the standard served-index comparison. PB/MB/app1/nginx container IDs
remain identical and running; no nginx restart. Native release audit records
accepted in l4tools PR18, main62b187c04cadc5983c7167b530f6d1a8573ed80b.
