# MenuBuilder camera extension — local implementation checkpoint

This change integrates accepted `siplite-camera/1.0.0` readers and the deployed
TASK-30 main_app/CTL-v1 adapter. It is a local TASK-40 checkpoint, not deployed.
TASK-35 supplies the existing-PKI public recipient lookup and certificate-independent
encrypted credential vault; its runtime rollout and protected entry remain separate.

Implemented locally:

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
- Port2 search/candidate UI and return-to-native credential action. The
  credential-entry writer and cancel-while-scan UI are not implemented yet.
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

Validation: backend full891passed/22skipped; app Ruff/format/Pyright passed.
Frontend build/105unit tests and13camera browser fixtures passed, including
explicit H265 refusal without a legacy route or H264 START. Earlier stale-preview
assertion failure was resolved by rebuilding changed source. Fixtures do not
establish real camera relay/decode or post-START actual-descriptor proof.

Remaining: protected credential entry using accepted/deployed TASK-35 helpers,
scan cancellation/progress and final runtime/browser acceptance. No device
writer or production flag was enabled. Deploy only after reader and credential
gates; do not infer camera readiness from the new UI or ACK alone.
