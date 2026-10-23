# MenuBuilder camera extension — local implementation checkpoint

This change prepares MenuBuilder for accepted `siplite-camera/1.0.0` readers.
It is not the full TASK-40 release and has not been deployed. TASK-30 owns the
native main_app producer adapter. TASK-35 supplies the existing-PKI public
recipient lookup and certificate-independent encrypted credential vault.

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

Validation at this checkpoint: backend full suite884passed/22skipped before
the final REST opt-in regression, then66focused camera/contract tests passed; app Ruff,
format and Pyright passed. Frontend build and102unit tests passed. Existing
camera browser scenarios passed; the three new draft/conflict/discovery cases
passed against the rebuilt preview on an isolated local port. A stale preview
initially failed the new success-message assertion; rebuilding the changed
source resolved that failure. These are fixture tests, not camera decode.

Remaining before TASK-40 completion: integrate the accepted TASK-30 native
camera inventory/state/CTL adapter, browser/server/camera/relay Auto selection
before START, explicit H264/H265 behavior, protected credential entry using
TASK-35 helpers, scan cancellation and final runtime/browser acceptance.
The current camera UI still uses the old stream-start path; do not interpret
this checkpoint as the new H265 producer being ready. No device writer or
production flag was enabled. Deploy only after reader and credential gates.
