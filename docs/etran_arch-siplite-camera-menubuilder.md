# MenuBuilder camera extension — local implementation checkpoint

This change integrates accepted `siplite-camera/1.0.0` readers and the deployed
TASK-30 main_app/CTL-v1 adapter. It is a local TASK-40 checkpoint, not deployed.
TASK-35 public-recipient lookup and certificate-independent encrypted vault are
deployed from482e099; this candidate adds their authenticated UI/API workflow.

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

Validation before final clear-endpoint guard: backend full952passed/25skipped;
final guard and credential boundary follow-up38passed. App Ruff/format/Pyright
passed. Frontend build/105unit tests and16camera browser fixtures passed, including
explicit H265 refusal without a legacy route or H264 START. Earlier stale-preview
assertion failure was resolved by rebuilding changed source. Fixtures do not
establish real camera relay/decode or post-START actual-descriptor proof.

Remaining: final source/release verification and production UI/browser smoke.
TASK60/70 own actual firmware control and real camera relay/decode acceptance.
No device writer or production flag was enabled. Do not infer camera readiness
from the UI or ACK alone.
