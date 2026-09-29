# Video page, MenuBuilder step 1 — handoff

## Контекст
- Scope: MenuBuilder frontend only. The terminal encoder, media ingress and WAN budgets are unchanged.
- Owner: MenuBuilder owns the browser page and profile labels. The terminal agent owns encoded resolution and bitrate; Janus forwards the existing stream to viewers.
- Flow: UI `low`/`default` → BFF stream start → terminal stream → Janus → browser WebRTC receiver.
- Invariants: keep wire profile values and lease checks; remote input is desktop only and remains restricted to `low`; browser zoom must not alter click coordinates.

## Выполнено
- Labels changed to Medium (`low`) and HD (`default`) without claiming a fixed resolution or traffic ceiling.
- The video route collapses the main navigation, uses a compact terminal header, hides the terminal list after selection, and places source, stream and control actions next to the video.
- The browser player adapts its viewport to the decoded frame aspect ratio and exposes Fit / Native size with scrolling. Native size is disabled during remote input; the overlay recalculates geometry on activation. The player shows the actual decoded resolution.
- The fullscreen state now follows the browser `fullscreenchange` event, including Esc.

## Проверено
- `tsc -p MenuBuilder/frontend/tsconfig.json --noEmit --incremental false`: exit 0.
- Vite production build to an isolated scratch output: exit 0. The config's existing `methodCodes.json` copy hook logged a `__dirname` warning under `--configLoader runner`; bundle generation still completed.
- Local mocked browser render at 1440×900 and 390×844: navigation width 56 px, no horizontal document overflow, video area 1368×635 px on desktop, compact controls wrap on mobile.
- `git diff --check` for modified frontend files: no whitespace errors.

## Не проверено / следующий шаг
- Live browser E2E with terminal, Janus and remote input after deployment. Local browser checks used mocked APIs and did not start a stream.
- Medium ≤2.0 Mbit/s and HD ≤3.5 Mbit/s aggregate WAN for one viewer remain targets. They require terminal rate/resolution policy, transport overhead measurement and quality testing; the browser view setting cannot enforce them.
- Check 800×600, 1920×1080, 4:3, fullscreen, Native size and input coordinate mapping against an actual decoded stream. Keep the input gate fail closed when introducing a new encoded profile.

## Cleanup
- Local mocked browser sessions did not acquire a lease or create remote sessions. Temporary build and screenshots are under ignored scratch output.
