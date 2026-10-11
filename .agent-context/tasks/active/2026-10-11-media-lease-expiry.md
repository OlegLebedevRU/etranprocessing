# Media watchdog bounded by its app1 lease

## Task intake
- Goal/type: fix stale managed video sessions after browser authentication or
  keepalive loss, with minimal code changes; backend/media recovery task.
- Scope: MenuBuilder video session/keepalive integration and ingress lifecycle;
  no frontend, MQTT client, app1 ownership, database schema or Janus changes.
- Owners: app1 owns exclusive control leases; ingress owns media resources;
  MenuBuilder forwards the verified provider deadline and epoch identifiers.
- Flow: browser → authenticated BFF → app1 lease result → private ingress API.
- Invariants: no eviction of an unexpired session, no resurrection after expiry,
  no old keepalive extending a different epoch, no new independent lease owner.
- Unknowns: live auth-expiry recovery and long-running media after release need
  verification; a reachable ingress transport does not prove decoded frames.
- Sources: lease/video contract cards, video.py, video_control.py,
  media_orchestrator_client.py, media_lifecycle.h and media Redis persistence.
- Ready criteria: local quality/tests, exact deployed revisions, lease-expiry
  followed by successful new session before the old600-second timeout, plus
  successful keepalive beyond the original lease window and ordinary cleanup.

## Change
Session start and successful matching-video keepalive forward app1 expires_at as
lease_expires_at (whole Unix seconds). Ingress encodes that absolute deadline
in its existing ttl_sec/renewed_at pair; Redis serialization is unchanged.
HTTP transit and Janus setup do not extend it. Rounding may stop media less
than1second early. Optional internal callers without this field keep legacy TTL
semantics. Deadline-aware renew requires a matching lease or stream ID.
Start and health retire expired sessions before reuse/conflict checks, using
the existing stop operation. Active session conflict remains409. Invalid/missing
provider deadlines fail closed; BFF returns an explicit media-session error.

## Local checks
- Full MenuBuilder suite:754 passed,22 skipped; skips are the existing tests
  requiring an external database, not recovery acceptance.
- Ruff app check/format and pyright app: passed (0 errors/warnings).
- Builder container compiles actual ingress with Wall/Wextra/pedantic and runs
  existing unit/feedback tests plus new deadline/identity/expiry handler tests.
- Local WSL unavailable: distro VHD path is inaccessible; Linux C validation
  instead ran in the isolated standard Docker build stage on the builder.
- Original full test run failed4 outdated provider-response mocks; these now
  include actual expires_at and assert the forwarded deadline/epoch. Final
  full suite passed. No production secrets used in unit tests.

## Release / live acceptance
PR60 merged as a05eb955ee13bf5c62f76e5e134f5d030980ae24. The standard builder /
registry / production launcher released ingress first, then MenuBuilder from
that exact revision, including their build/test gates. Production readiness:
available RAM1982MiB, root56%, load0.15; active media0 before ingress restart.
MCP Ops unavailable, standard authenticated SSH used. No schema migration or
frontend/nginx/Janus release.

| Component | Verified immutable digest | Production container |
|---|---|---|
| ingress | sha256:4ae0ef4e4080abb3ce8fd06f16ac3283d7a731111a5d5e8961f40fb3a370c057 | af757cb20e67 |
| MenuBuilder | sha256:bae3f1801702a98b09167ae22422ed0033cc2965f8d6caa9c46ffeb9f520fca3 | 172e369c0648 |

Live4619: an intentionally abandoned30-second owned stream lease expired,
provider/terminal state became stopped/lease_expired, and the new session
succeeded1.409seconds after its request (3.042seconds after old lease expiry,
including polling/acquire/start). Media start returned29seconds TTL. The new
stream stayed running/transport-connected through nine5-second keepalive cycles
(45seconds, beyond its original lease window), with fresh RTP, then stop/release
left no active test lease. This is real lease-loss recovery, not a mock or forced
mountpoint deletion. A first run proved the same quick takeover but later hit
an external TLS handshake timeout; retained as failed evidence, not acceptance.

Deployed browser verification:90seconds,576 presented callbacks, maximum gap
300.14ms, zero packet loss/dropped frames/freeze/NACK. Stop, new decoded frame
on restart and route-unmount release passed. Different software-reported versus
configured frame cadence remains a separate camera observation, not this fix.
Final ingress health200, active media0. Neighboring container IDs unchanged,
including app1 e238dc3ca6ed, Janus1cd35cea4177 and nginx8927aebcc8cc.
Evidence retained in the siplite workspace's ignored build/acceptance directory:
media-lease-expiry.json, video-browser-media-expiry.json, release logs and the
original TLS-failed run. No PIN/cookie appears in the expiry evidence or logs.
Runtime deadlines retained the provider ownership boundary and existing storage
format. Whole-second expiry may be less than1second early; it never adds time.
