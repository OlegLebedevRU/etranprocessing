# l4media downstream RTCP handoff

## Task intake and scope

User authorized l4capture/proxy fixes, tools release, and parallel l4media build/deploy.
This component change owns `l4media` ingress and its local validation; release
owner coordinates commits and beta launcher deployment. Producer is Janus RTCP,
consumer is terminal proxy then capture. L4RTP/1 reverse frames use the existing
TCP/mTLS connection and preserve RTP freshness, private management API, route
lifecycle and current certificate/SN behavior. No Python application code,
archive worker, Janus implementation or unrelated dirty files changed.
Working-tree implementation, based on repository HEAD; 2026-10-10 local checks.

## Implementation

- Lazily created per-client connected RTCP UDP socket learns a Janus feedback
  endpoint through upstream RTCP and restricts incoming packets to that peer.
- Compound RTCP structural validation; bounded batches and 128KiB TCP queue;
  nonblocking partial writes driven by EPOLLOUT. Queue saturation drops new
  datagrams rather than allocating unbounded memory.
- Route changes/stop close the old socket, invalidate generation tags, and drop
  untouched queued frames. An already-started frame completes to preserve TCP
  framing. Disconnect/replacement discards all queue/socket state.
- Numeric epoch/generation epoll tags reject stale events after free, reroute or
  fd reuse. Downstream framing: `02 00 length_BE16 payload`, no reverse preamble.
- README/changelog/component card document behavior. `make test` now runs tests
  against production source with actual nonblocking UDP and TCP sockets.

## Verified

- WSL Ubuntu22.04 `make clean all test`: exit0, warning-clean GCC build, existing
  seven unit groups and new production feedback socket tests passed.
- Tests include two independent streams/initial SR endpoints, fragmented
  preamble/upstream frames, malformed and wrong-source datagrams, actual partial
  TCP writes, queue saturation, route deletion/recreation, stale TCP epochs and
  UDP generations after reconnect. Legacy clients create no RTCP socket until
  they send upstream RTCP.
- Existing `test_ingress_regression.py`: all six local production-process tests
  passed, including stale RTP, RTCP-only, duplicate SN and dynamic route behavior.
- `python3 tests/test_feedback_transport.py`: local live event loop passed two
  streams, reverse frame dispatch, fragmented input and route deletion.
- Production-source socket tests built/run with AddressSanitizer and
  UndefinedBehaviorSanitizer: exit0, no findings.
- `git diff --check`: passed. Placeholder test credentials only; no real secrets.

## Production deployment

- Committed/pushed source e1da7090c93db93446c055a8304fbf56f9977102.
- Standard beta launcher built linux/amd64 ingress on builder;32 CI tests and Docker make test gate passed.
- Published/deployed immutable image digest sha256:b4dd78598c1ce7ea04ad14207429a14228b0ea49e40c7d9673f282f022a5f7a1.
- Production container072bc9c575dd reports the exact source revision and digest. Independent GET /health on port9100 returned status=ok, routes=1, active_media_sessions=0.
- Neighboring container IDs unchanged, including Janus1cd35cea4177 and media nginx8927aebcc8cc.

## Remaining work / limits

- Tools release/sign/publication is tracked in the standalone l4tools repository.
- Local fake Janus sockets establish transport behavior; actual Janus PLI and
  browser IDR recovery require the corresponding new terminal tools and viewer.
- Janus needs an initial upstream RTCP datagram to latch the return endpoint;
  a source producing only RTP has no feedback destination.
- No NACK retransmission, REMB adaptation or camera management is added.
- All temporary local ingress processes terminate; generated build/test outputs
  are removed from repository after validation.
