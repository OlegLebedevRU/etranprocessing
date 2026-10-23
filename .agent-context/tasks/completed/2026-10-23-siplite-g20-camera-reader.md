# Siplite G20 camera reader release

Accepted source `daee8732aab9f705f0e2f86ccf87e762b75700fd`, PR95–98.
Production ingress image:
`dev-leo4-ru.cr.cloud.ru/etran/l4media-ingress@sha256:ed9e89876673b8b993f6077b260027f48bff769766a38b706ca1afddb6194ed3`.
Source archive SHA256 `a2de1204d88ff51277428b2428d44ca93409af071dc3dad8902463807401a2c4`.

Only ingress recreated using the accepted scoped activation utility. Existing
Compose overrides and HEVC=1 preserved; camera contract flag=1. Frozen Janus
reused, neighbouring container identities unchanged. Health ok, zero active
media sessions after test cleanup. One existing legacy transport route
reconnected after activation; it was preserved.

PASS: C build/tests; real isolated Redis restore/fail-closed;24 deployment/scanner
tests; Ruff/format/Pyright; container lifecycle/regression smoke; ingress and
Janus all-layer scans; immutable registry publication; running image/source
identity; H264 baseline/main/high and HEVC Main exact Janus format probes.
Release utilities have a scoped Python3.12 Ruff target and grammar regression
checks because the Ubuntu system interpreter differs from development Python.

Production isolated test epochs preserved exact descriptors for1920x1080@20
HEVC level123 and2592x1944@20 level153. START201, repeated START200,
health/renew/stop200. Missing negotiation422, unknown revision422, missing
service authentication401, schema-invalid B-frames400. All test epochs stopped.
Legacy capabilities preserve their projection and H264/H265 availability.

No camera settings, GPIO or RTP producer commands were sent. No RTP was sent
by the lifecycle probe; Janus format verification is not browser decoding.
Camera-to-siplite relay and browser acceptance remain TASK-60/70. Current
camera scope is stock OpenIPC only; host-only illumination simulation is not a
camera backend and physical LED commissioning is excluded.

G20 releases the TASK-30 app1 deployment gate, subject to its own required
checks. New device writers remain off until server-ready negotiation. Frozen
hevc-contract/2.0.0 and siplite-camera/1.0.0 package bytes are unchanged.

Evidence is in the coordinator workspace `D:/.codex/siplite-hevc/evidence/task20`
and `releases/task20/daee8732aab9f705f0e2f86ccf87e762b75700fd`.
Previous failed build logs are retained; those images were never deployed.

Rollback first disables extended writers and stops their epochs, then uses the
scoped activator with camera-enabled0, previous image digest
`sha256:210420e41d6870b45f4558e1441d51dc2e1cbc8b06402303434cd6f28f995ed2`
and source `dcd3ea1c04a6be1e3fe3047f3d5a3bec8747081d`. Preserve overlay order
and HEVC policy. The activator stores previous release metadata and automatically
rolls back on failed readiness; do not recreate the whole media stack.
