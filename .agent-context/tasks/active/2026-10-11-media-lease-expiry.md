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
Not yet completed. Release ingress first, then MenuBuilder; old clients remain
compatible. No schema migration, frontend/nginx/Janus or neighboring container
recreation. Check resource readiness and no active media before ingress restart.
Record source SHA, immutable digests, behavior evidence and cleanup here after
the actual release. Use the existing builder/registry/production launcher.
