# Desktop streaming patches — ready for release review

## Task intake

- Owner direction: implement the audited correction; legacy agents are verified
  by code/contracts, with owner-visible live browser acceptance on773 after a
  separately agreed deployment. Do not deploy or run a real terminal now.
- Scope: MenuBuilder backend/frontend and explicit desktop release overlay.
  No agent/MQTT client, app1 source, PB/shared, camera firmware or broker changes.
- Branch: `fix/desktop-stream-compat`, isolated worktree from current origin/main
  `795edd53d5850af1adff7a1c3d5652012c5c5480`. Original root working changes remain.
- Owners: BFF selects protocol/codec and prepares route; app1 owns lease/CTL;
  media owns route/Janus. No migrations.
- Preserve tenant/owner/session/scope, lease/command TTL, source/epoch identity,
  route-before-START and no fallback after an uncertain producer START.

## Implementation

- Fresh authorized presence without `video_contract_v2` advertises the retained
  CTL-v1 path. Auto/H264 return to the existing H264 route-before-start adapter;
  explicit HEVC gets `legacy_h264_only`. Missing old presence fields are supported.
- Modern advertised H264 tracks remain on the exact-descriptor path. H264 can use
  the retained adapter when new negotiation is explicitly disabled/unavailable,
  but malformed/mixed/stale capability, wrong epoch or an HTTP error never becomes
  that decision. A negative negotiated_v2 is distinguished from an invalid envelope;
  Auto with positive browser HEVC and explicit HEVC still refuse unknown negotiation.
- Codec capability no longer depends on BFF delivery ports. Native START checks
  those ports before media prepare/producer command and returns a configuration503.
  Legacy delivery still uses its existing configured route and agent transport.
- Legacy browser support uses SDP when getCapabilities is unavailable; explicit
  H264 non-support and malformed browser reports remain errors. Native browser
  freshness is evaluated at request admission, so a cold probe does not invalidate
  the same request's report. Lease expiry is still checked after prepare.
- Legacy START refusal/timeout removes only its lease-bound prepared media route.
- UI preserves the refusal code and explains transport/negotiation/legacy/probe
  failures separately; encoder_busy is not misclassified as another user's lease.
- Explicit overlay restores terminal-local proxy ports5004/5005 by default and
  desktop HEVC gates in app1. Existing image pins/operator overlays must be retained.
  It is not applied automatically and does not enable camera runtime policy.
- Frozen media3 schema, descriptor equality and strict envelope validation remain.
  There was no evidence justifying a blanket relaxation of those contracts.

## Contracts

| Producer → consumer | Path / version | Compatibility / guarantees |
|---|---|---|
| UI → BFF | stream/start; existing codec_policy/browser fields | No required API field added; old requests without codec_policy unchanged |
| BFF → app1 legacy | Existing stream/start; mode/source_id/profile, no v3/camera header | Old CTL-v1 producer retained; one START, lease and scope checked |
| BFF → app1 native | Existing media3 HTTP / CTL2 | Positive source/profile/epoch evidence, exact descriptor and READY retained |
| BFF → media | Legacy route API or descriptor media3 | Route exists before producer; cleanup checks lease identity |
| app1 → old agent | Existing CTL v1 | No MQTT schema/topic/QoS/retain changes; old command TTL unchanged |

No retry/downgrade after native producer failure or uncertain result. No source
dimensions are clamped and no camera descriptor is promoted into desktop.

## Verification

| Check | Result |
|---|---|
| Backend full pytest |1151 passed,25 skipped, exit0; pre-existing mock/deprecation warnings |
| Final focused native/legacy/media rerun |88 passed, exit0, includes four additional negative tests added after full collection |
| Ruff check --fix app + changed tests; Ruff format | Passed; final format and check clean |
| Pyright app |0 errors/warnings; existing newer-tool-version notice only |
| Frontend TypeScript/Vite build | Passed; existing vendor chunk-size warning |
| Frontend unit suite |130 passed |
| Browser fake-API regression |4 passed, exit0; remembered HEVC, busy, configuration refusal + lease cleanup, negotiation outage |
| playwright-cli local smoke | Passed;0 writes,0 page errors, translated outage displayed |
| Overlay | YAML parsed; explicit two-service desktop scope reviewed |
| git diff --check | Passed |
| Legacy agent runtime | Not run, unavailable by owner's instruction; HTTP payload/route tests and preserved app1 producer code used |
| Production deploy / live773 frames | NOT RUN; next owner-visible gate |

Commands: `uv sync --frozen`, `python -m pytest -q --tb=short`, focused pytest
`test_desktop_legacy_compat`, `test_hevc_contract`, `test_media_v3`;
`uv run --no-sync ruff check --fix app` (also changed tests),
`uv run --no-sync ruff format app`, `uv run --no-sync pyright app`;
`npm ci --ignore-scripts --no-audit --no-fund`, `npm run build`, `npm test`,
`MB_PLAYWRIGHT_PORT=5197 npm run test:e2e -- e2e/desktop-hevc-memory.spec.ts`;
own `playwright-cli -s=desktop-compat` against localhost5198 with all API mocked.

## Release boundary and cleanup

Runtime audit before implementation found BFF proxy ports0/0 and both app1
desktop video flags false. Code alone does not activate them: include the reviewed
[desktop configuration overlay](../../../MenuBuilder/deploy/desktop-streaming.compose.yaml)
through the standard release, preserving current images/configuration and verifying
media readiness before asserting chain-ready. Recreate only approved MB/app1;
signer may share MB image but must remain unchanged. No remote files edited here.

Next, with the owner watching the browser: deploy approved images/config, verify
effective settings, then773 H264/H265/Auto at Medium/HD, fresh framesDecoded,
keepalive and explicit STOP. This acceptance is deliberately pending.

Port5182 belonged to another process and was left alone. Tests used5197/5198.
Own browser closed and own previews terminated after successful checks; Windows
Playwright webServer teardown needed the verified preview PID stopped explicitly,
after which runner exited0 with4 passed. Temporary local smoke JS removed.
No production credentials, leases, broker clients or terminal commands created.
