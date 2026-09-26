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

## Stop cleanup follow-up

- The stream later ran for more than 16 minutes and stopped in the browser without HTTP 500; the local `l4capture.exe` exited. The BFF sent `/sessions/{stream_instance_id}/stop` and ingress returned idempotent HTTP 200 for that absent ID, leaving the actual media session, route, and Janus mountpoint active until TTL expiry.
- The follow-up changes BFF stop to a service-authenticated ingress `/sessions/stop` after verified terminal stop. It identifies the active media session by SN but requires its ID to match the lease ID or stream instance ID, preventing a stale stop from deleting a newer session. Absence remains idempotent.
- Follow-up local checks: 13 MenuBuilder video contract tests, Ruff, Pyright, ingress C unit tests and binary build, OpenAPI JSON parsing, and `git diff --check` passed.
- Follow-up deployed after separate user approval on 2026-09-26. Two short terminal 773 start/stop cycles returned HTTP 200 with no 500; the operator confirmed visible video and clean stop. After stop, ingress reported `route_exists=false`, the 773 media Redis key was absent, and local `l4capture.exe` had exited. Another terminal's media session remained active. The earlier 16-minute stream and these stop cycles cover the observed freeze and cleanup failures; 30-minute mixed-motion and older GPU checks remain open.

## Deployment boundary

The user explicitly approved this server deployment after reviewing the affected files and private environment change. Follow the repository release flow for later updates; keep the production and local source versions aligned.
