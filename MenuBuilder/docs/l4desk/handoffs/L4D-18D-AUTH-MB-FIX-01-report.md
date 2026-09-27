# L4D-18D-AUTH-MB-FIX-01 — private media credential rotation

Producer verdict: ACCEPTED; controller handoff acceptance: PENDING. Runtime verified 2026-09-27T21:48:49Z.
This MB-owned step does not accept 18D or open 18E.

## Gate and ownership

Registration R-L4D-18D-AUTH-MB-FIX-01-v1, commit
04560ca396f5590a8d25542382ebd4641d8e7ccf; direct accepted input
H-L4D-18D-MEDIA-AUTH-CONTRACT-01-v1 at
1c2ad9eb1bc252908a839247036849959d7ed3b6, contract/schema/artifact 1.0.0
(media_management_openapi_1.0.0). All six packet files and three granted
artifacts matched Git/raw SHA-256. H-L4D-18C-IOT-v1 is sequence-only.
User explicitly authorized this separate rotation and host deployment.

## Consumer configuration

Source inspection: `backend/app/config.py` defines `l4media_service_token`
with empty default; `l4media_effective_token` prefers it over the shared
internal key. `services/media_orchestrator_client.py` sends it in
`X-Media-Service-Token` and Bearer headers. Actual private key:
`L4MEDIA_SERVICE_TOKEN`. No source/default change was necessary.

| Consumer | Private configuration | Action |
|---|---|---|
| menubuilder-backend | /home/user1/MenuBuilder/backend/.env, raw env_file | Token replaced, container recreated, same application image |
| l4desk-e2e-test-backend-1 | Same backend .env via E2E_BACKEND_ENV_FILE in private e2e/.env.e2e.local | Confirmed affected; recreated with same application image |

Both tokens were present and equal to provider before rotation. Both are
present and equal to new provider afterwards. New value differs from old.
No secret values or hashes are recorded. The second consumer does not have
an independent token file; neither root Compose nor e2e Compose needed edits.

Redacted private diff, exactly one existing key replaced:
`L4MEDIA_SERVICE_TOKEN=<old private value>` ->
`L4MEDIA_SERVICE_TOKEN=<new private value>`. File mode remains 600.
Provider generated the new credential locally on production; MB consumed it
through a mode-600 temporary private channel, removed after both recreations.
Production credentials were never transferred to the build host.

## Coordination and deployment

Before switching, the media owner published/tested source e6e681dcf74fb0e81da5cf1f7f0fc2d39dca7f33,
container-built both images on the authorized build host, pushed them to the
approved private registry and pulled the exact digests on production.
Active media sessions were zero. Media provider/Janus switched first, then
this MB step replaced its one private key and recreated only affected
backends using `up -d --no-build --no-deps --force-recreate`.

Preserved application images:
- production: sha256:e0d17a09092e34b206eeb313b155186c419e55ee0419a684eb5ae2ce32e50090
- isolated test: sha256:fa37ec23d4c2a807b7209440af00a123bdecbb2a862a3e3e1cc1b3cd1e091345

## Fresh verification

- Both Compose configurations validated without displaying expanded secrets.
- Both containers running, restart count 0, `/docs` HTTP 200.
- From each backend process environment, request using actual configured
  `settings.l4media_effective_token` to provider metrics returned HTTP 200.
- Provider metrics: missing token 401; wrong synthetic token 401; old token
  401; new token 200. Only an internal read-only management operation used
  for this credential check; no live PIN/stream was issued by MB auth step.
- Janus old/missing admin credentials rejected (error 403); new accepted.
- Provider/consumer equality and Janus/ingress equality verified as booleans.
- All unrelated production container IDs unchanged (including app1, PB,
  nginx, broker, Redis, mock gateway). Agent unchanged.

## Rollback and reconciliation

Private backup directory:
`/home/user1/.l4d-backups/18d-media-auth-20260927T214543Z` (mode 700).
File `3` is prior MB private env (600); `files.json` maps private backup paths.
Provider's old env, Compose and Janus config are in the same restricted set.
Images preserved by `l4d18d-rollback-<container>:pre` tags.
Rollback must restore provider and consumer credentials together, old media
images and original Janus config, then recreate the same four containers.
Rollback is prepared, not executed.

Source defaults remain empty; the normal raw env_file path retains the new
value across future deployments. Registry release instructions belong to
media owner. No MB Python code changed, so no MB pytest/Ruff/Pyright/build
was run under AGENTS conditional scope rule. This report records private
configuration reconciliation without committing secret files.

Limitations: independent controller acceptance pending; full media/archive
and browser acceptance are owned by L4D-18D-MEDIA-FIX-01 and are not claimed
by this credential handoff.

Detached candidate: MenuBuilder/docs/l4desk/handoffs/L4D-18D-AUTH-MB-FIX-01-candidate.md.
