# Owner-visible desktop performance study

These scripts run only when explicitly invoked against an authorized terminal.
They use a normal, manually authenticated visible Chrome profile and the portal's
START/STOP controls. They neither read/export authentication credentials nor change
leases, source defaults, jitterBufferTarget or browser installation settings.
The overlay lives only in `study-overlay.mjs`, injected by the explicit study
script and disposed afterward. It is never imported from production `src`, is
absent from ordinary builds, and has no URL flag to enable it in the application.

Build the frontend first. Open a separate study Chrome profile with an explicit
local debugging port and log in normally. Keep the user's ordinary browser open.
Run from `MenuBuilder/frontend`:

```text
node e2e/diagnostics/desktop-study.mjs --cdp http://127.0.0.1:9336 --url <portal-video-URL-with-explicit-device_id-and-study=desktop> --display DISPLAY1 --phase A1 --seconds 90 --out .pytest_tmp/desktop-performance
```

Repeat DISPLAY2, then restart only the study browser with
`--disable-features=OnBeginFrameThrottleVideo` for B, and without it for A2.
Use the same Chrome version, profile, window geometry, source workload and H265/HD
settings throughout. The flag is diagnostic; do not add it to product defaults.
The harness serves local `dist` files only in its own tab; real API/authentication
and signaling remain unchanged. It refuses an existing active stream and performs
ordinary STOP followed by a successful lease DELETE before reporting completion.

The first15seconds are warmup. Measurement polls getStats at1Hz and collects
requestVideoFrameCallback metadata. Native tracing is a bounded20second window
per run, not a trace of the entire long run. Preserve visibility and record actual
window/source placement: capturing the receiver's own window creates feedback and
does not represent an independent source workload. For source comparisons, place
the receiver on the other monitor and use the same reproducible source content.

Record host load in a second shell using `sample-host.ps1 -OutputFile <private
JSONL-path> -DurationSeconds <bounded-duration>`. CPU process deltas are normalized
by logical processor count. GPU values are the maximum observed engine instance
for each engine type, not total device utilization; an absent type stays missing.

```text
node e2e/diagnostics/summarize-desktop.mjs .pytest_tmp/desktop-performance --brief
```

The summary joins RTP timestamp → native decoder input, and media timestamp →
enqueue → SetCurrentFrame. It excludes half a second at either trace boundary
when counting unselected queued frames. Native BeginFrame intervals measure video
processing opportunities; rVFC measures presentation submission, not physical
display refresh. A decoded or selected frame is not proof of a unique desktop
update: the source may encode cached pixels.

Raw traces, profiles, screenshots, source logs and samples are private ignored
evidence. Publish only reviewed aggregate results. Close/logout the study profile
after its streams and leases are stopped; never close unrelated user browsers.
