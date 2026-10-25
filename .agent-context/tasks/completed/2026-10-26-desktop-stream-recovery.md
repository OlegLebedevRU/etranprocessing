# Desktop streaming recovery — deployed and live acceptance passed

## Scope and freshness

- Owner requested code, then authorized execution of the remaining release and
  owner-visible live browser gate. Legacy binary acceptance was explicitly
  replaced with code/contract verification because an old agent is unavailable.
- Local date2026-10-26; runtime timestamps below are UTC2026-10-25.
- Freshness check found main795edd5 unchanged since patch preparation, while
  production app1 had advanced to6aafd0e and gained a separate camera-runtime.
  That image/cohort was preserved. Original root dirty files were not reset.
- BFF owns codec/protocol choice; IoT owns lease/CTL; media owns ingress/Janus.
  No agent installation, MQTT schema/topic, DB migration or camera gate changes.

## Accepted and deployed revisions

- [PR128](https://github.com/OlegLebedevRU/etranprocessing/pull/128): desktop code,
  compatibility tests, UI diagnostics and scoped deployment package.
  Application merge: `3b59799829a90980862178127529f579a591bb2c`.
- [PR129](https://github.com/OlegLebedevRU/etranprocessing/pull/129): exact Compose
  dollar escaping for values read from the existing engine environment.
  Accepted helper merge: `a449ec0a06f87f06820065fed65b5fe7b1c13734`.
  This helper-only fix used the already built application revision, not a rebuild.

| Artifact | Immutable registry digest |
|---|---|
| menubuilder-backend | sha256:f829381138bec17a1ffa6cc11495fa72189d242318b51ec4a4827a01eeac4617 |
| menubuilder-frontend | sha256:b86cb11a55954e007e1a2546759c14dd58601533879ef1ab11e5c0e15b586df5 |

Builder used the existing launcher with explicit component/build-only selection,
then production pulled the verified digests. Installed launcher was not changed.
The scoped repository-owned helper acquired the common deploy lock, preserved
every running target Compose overlay, and checked effective environment and
mounts before/after. Backend and app1 alone were recreated. app1 kept source
`6aafd0e4f9403b176c0f425b1ed3ddc1fbf998f6` and image ID
`sha256:7e04c6789cb08132c0c69bfe34f281346fd1943f898a3679b915f9211b3aa4c9`.

Confirmed effective settings: BFF proxy ports5004/5005, native video and session
orchestration enabled; app1 desktop video-v2 and HEVC-chain flags true.
Media independently reported contract3, dependencies_ready=true,
hevc_runtime_verified=true and codecs H264/H265 before enabling the policy.
All12 neighboring container IDs remained unchanged after the gate, including
signer, camera-runtime, media, broker, Nginx and PB. Both targets remained running,
restart_count0. No Nginx restart or signer/camera-runtime recreation.

## Guarded failures and recovery

- One unrelated USB-camera lease was active before rollout. Its owner completed
  the stream normally after notification. We did not stop or revoke that lease.
  The helper checked absence of active slots again before each target restart.
- An initial registry DNS timeout was transient; the same digest pull succeeded
  on retry. DNS/infrastructure was not modified.
- The first deployment attempt stopped before replacing services because the
  rendered AUTH_USERS differed from the current engine value. PR129 fixed
  Compose escaping of existing literal dollar values. No credential was changed
  or exposed. The subsequent runtime check required exact environment equality
  except the reviewed desktop flag/port additions.
- Existing browser documents retained older JS bundles. The target773 tab was
  refreshed with cache disabled/cache-busting to load video-surveillance-kwWe0sqO.js.
  The authenticated camera tab was left untouched. No cookies/tokens were copied.

## Owner-visible browser acceptance

The owner signed in normally and directed us to the already open Chrome.
Playwright CLI attached to that Chrome and selected only the existing773 tab.
All control actions used normal UI buttons and dropdown keyboard navigation.
No API response, capabilities, SDP, frames or codec was spoofed.
Read-only PeerConnection observers collected only selected statistics; no PIN,
JWT, password, clipboard, keyboard output or screenshot was retained.

Six successful new-frontend sessions ran between22:09:46 and22:14:06UTC:

| Policy | Profile | START | Negotiated codec | Decoded size | Recorded framesDecoded | Dropped |
|---|---|---|---|---|---|---|
| H265 | low / Medium |200 | video/H265 |3000x2000 |652 |0 |
| H265 | default / HD |200 | video/H265 |3000x2000 |216 |0 |
| Auto | default / HD |200 | video/H265 |3000x2000 |249 |0 |
| Auto | low / Medium |200 | video/H265 |3000x2000 |105 |0 |
| H264 | low / Medium |200 | video/H264 |1620x1080 |53 |0 |
| H264 | default / HD |200 | video/H264 |1620x1080 |237 |0 |

Native HEVC descriptors retained source3000x2000, Main8bit420, level150 and
declared10/15FPS for low/default. Actual instantaneous decode FPS varied; this
gate proves fresh decoded frames, not guaranteed achievement of nominal FPS.
H264 used the retained legacy adapter and its existing size/cadence policy.

HEVC low advanced57→520 frames while the same stream remained active from
22:09:46 through22:11:03, beyond the original60s lease. Keepalive stayed200,
including22:11:00. A separate H264/Medium check in visible Edge also ran beyond
TTL, advancing60→282→416 frames. Every deliberate restart had a new stream UUID.

Final STOP completed; six Chrome lease releases returned204. The authoritative
BFF status returned200 with lease.active=false and stream.state=stopped.
No open video PeerConnection remained, and the Redis active-slot check for773
returned false. All captured Chrome773 write responses were successful.
The existing STOP handlers issue an idempotent duplicate stop; both responses
were200, with no duplicate START. This task did not redesign those handlers.

## Browser distinction and legacy limit

The separate Edge156 instance did not advertise H265. Its explicit HEVC request
correctly returned browser_codec_unsupported and released the lease; H264 worked.
The owner's Chrome advertised H265 and actually decoded it. Server source/quality
restrictions were not relaxed to disguise missing browser codec support.

An actual old agent was not installed/tested. Compatibility evidence remains
the retained app1 HTTP/CTL-v1 producer, targeted BFF tests, absence of native
headers/descriptor in legacy HTTP requests, and successful legacy adapter usage
by the unchanged installed modern agent. This is not old-binary E2E evidence.

## Verification and operational notes

- Exact builder backend revision:1180 collected,1155 passed/25 skipped; source
  compile, Ruff check/format and Pyright passed. Local final focused88 passed.
- Frontend builder130 tests and TypeScript/Vite build passed. Existing vendor
  chunk warning and npm dependency-audit notices were not changed by this task.
- Six release-helper tests and Ruff passed; original four fake-API browser cases
  and local playwright-cli smoke had passed before release.
- Read-only selected tracked-source scan:360 files, zero private-key/token/key-ID
  findings. No sensitive values were added to Git or returned by diagnostic reads.
- Release artifacts and guarded before/after records remain in the standard
  private CI task directories. Application source revision3b59799 and helper
  revisiona449ec0 are distinct, both accepted ancestors of main.
- Build-only did not advance the launcher's automatic release checkpoint. There
  is no automatic monitoring. Future manual releases must preserve the complete
  running Compose file list and these desktop settings, as the runbook requires.
- Owner's Chrome stays open on773, stopped; unrelated camera/calibration tabs
  were not modified. Temporary observers/own browser sessions are cleaned up
  after saving sanitized evidence. No test credentials were created.

[Patch handoff](../active/2026-10-25-desktop-stream-compat-patches.md) and
[deployment instructions](../../../MenuBuilder/deploy/desktop-streaming.md).
