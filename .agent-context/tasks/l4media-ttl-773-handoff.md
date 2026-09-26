# Terminal 773: media route expires during active video

## Observed on 2026-09-26

- `l4capture` and the local RTP tunnel kept sending while the browser frame was frozen.
- Ingress reported `route_exists=false` and increasing `unrouted_packets`.
- Ingress logged expiration of the active media session after its 600-second TTL, followed by deletion of the terminal's route.
- The browser kept renewing the app1 control lease successfully. Stopping and restarting only the stream restored moving video.

## Change

- Add service-authenticated ingress media renewal for an active session and matching SN. Reject stopped, expired, or mismatched sessions.
- Count media TTL from the most recent renewal; retain expiry cleanup when keepalive stops.
- After successful app1 lease keepalive, MenuBuilder renews the active media session for the terminal SN. Console leases do not renew media. The ingress session ID differs from the terminal stream instance ID in the current deployed flow, so renewal by the local provider ID caused a 404/500 and was replaced with SN lookup.
- Preserve the server's existing Redis ownership and Janus mountpoint fixes in local source. Persist `renewed_at` in Redis and accept old 12-field records.
- Document the flow and API contract.

## Verification

- Ingress C unit tests and binary build passed in WSL.
- MenuBuilder 13 video contract tests passed; Ruff and Pyright passed.
- OpenAPI JSON parses and `git diff --check` passes.
- Deployed to ingress and MenuBuilder on 2026-09-26 after explicit user approval. First build caused immediate 404/500 because of the ID mismatch above; the corrected build was redeployed and both services restarted.
- The corrected stream passed 626 seconds: ingress reported `route_exists=true`, `fresh_rtp=true`, 27,900 RTP packets, and zero unrouted packets. Redis session TTL refreshed to 659 seconds, and ingress renewal plus browser control keepalive both returned HTTP 200. The operator moved a window after the 10-minute mark and confirmed that the browser frame still moved without a 500 error or noticeable delay.
- The local Compose file requires `L4MEDIA_REDIS_URL` from the private server `.env`; the approved release set that value privately so Redis persistence remains enabled.

## Deployment boundary

The user explicitly approved this server deployment after reviewing the affected files and private environment change. Follow the repository release flow for later updates; keep the production and local source versions aligned.
