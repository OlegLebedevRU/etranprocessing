# MenuBuilder camera extension — deployed TASK40

This change integrates accepted `siplite-camera/1.0.0` readers and the deployed
TASK-30 main_app/CTL-v1 adapter. TASK40 is deployed from b60088fbe113be61de6e2b8b6b4705d0f3870cc7 (PR100).
TASK-35 public-recipient lookup and certificate-independent encrypted vault are
deployed from482e099; TASK40 adds their authenticated UI/API workflow.

Implemented:

- Separate byte-identical camera RPC/media schema mirrors, bounded validation
  and safe errors. Existing desktop schema remains unchanged.
- Negotiation option on all l4media v2 calls; known extended epoch reads,
  renewals and stops forward its header. No implicit expansion for old callers.
- RPC161 native nested patches and RPC165/166 discovery/binding commands use
  fresh advertised methods, current revision and candidate physical port2/
  discovery generation. New REST tasks opt in with camera_contract_revision;
  original task method/topic/presence conventions remain unchanged.
- Capability-driven video/image editor. Polling preserves local changes;
  revision conflict blocks stale Apply; only edited fields are sent. Viewer,
  active-view, stale/offline and ambiguous POST restrictions are preserved.
- Port2 search/candidate UI displays correlated discovery progress. Cancel uses
  a distinct operation UUID targeting the scan, with independent RES+gauge84
  confirmation. Lost responses do not trigger automatic mutation retries.
- Operator-only write-only credential endpoint bounds input, suppresses secret
  reflection in validation errors, and persists encrypted intent before network
  dispatch. IoT task UUID equals operation UUID; replay observes the existing
  task without resealing or extending the original deadline. Explicit manual
  redelivery uses a new UUID and current verified terminal certificate.
- Credential promotion/clear requires matching independently read RES+gauge84.
  Failed or ambiguous delivery preserves the previous active server profile.
  Generic command clear cannot bypass vault reconciliation. Browser storage
  contains only operation metadata; login/password are cleared after submission.
- Stock-only illumination explanation, without physical controls/probes.
  Cloudflare STUN is the default; additional supplied ICE/TURN servers remain.
- Separate camera START path carries exact media descriptor/epoch to app1.
  Auto uses camera/server/relay/browser observations before media prepare;
  unknown or stale support never authorizes fallback. Explicit H265 fails
  without H264 restart. Codec changes are bounded before START with correlated
  RES+gauge84, fresh actual track and one shared preparation deadline.
- Native command_ack is distinct from READY/actual_descriptor/RTP/decode.
  Failed preparation/start compensates only its own epoch. Repeated active
  START reads the existing epoch without changing the shared codec.

Validation: accepted Linux builder953passed/25skipped; Ruff/format/Pyright
passed. Frontend105unit tests and production build passed. Local16camera browser
fixtures passed, including explicitH265 refusal, credential loss and targeted
cancellation. Frozen packages unchanged; source review36files passed.

Production browser smoke on camera4623: ordinary authenticated reads200, vault
and verified recipient ready, terminal online/lease idle. The existing firmware
does not advertise camera revision, so new controls are disabled with a reason.
Chrome advertisesH265. No camera mutations or JavaScript errors occurred.

Both images passed all-layer scans and were activated by immutable digest.
Existing Compose overlays, environment/mounts and stable private KEK survived;
11BFF neighbors unchanged. Frontend static publication restarted no containers.
See [release handoff](../.agent-context/tasks/completed/2026-10-24-siplite-task40-menubuilder.md).

TASK50 server-ready and TASK60/70 actual firmware/local API/tool/camera decode
acceptance remain. Camera writers remain disabled; this smoke started no stream.
Do not infer actual camera readiness or decode from UI/command_ack alone.
