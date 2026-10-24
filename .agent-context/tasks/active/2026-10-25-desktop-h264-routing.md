# Restore desktop H264 routing

## Task intake
- Owner: MenuBuilder backend; desktop native 1.13.54 remains unchanged.
- Flow: browser codec policy → BFF admission → existing IoT H264 START/media route.
- Regression: v3 negotiation required H264 evidence from a native capability
  publisher that currently advertises only HEVC. Explicit H264 and Auto without
  HEVC failed before reaching the retained H264 adapter.
- Invariants: authenticated device access, publisher lease and owner/session
  binding, browser support, route-before-START, no fallback after producer failure.
- Change: H264 keeps a real advertised v3 track. If an otherwise valid capability
  contains no H264 entry, select the existing adapter. Auto negotiates H264 on
  explicit browser non-support or the existing definitive HEVC unsupported
  reasons. Unknown, stale, busy and mismatched capabilities still fail closed.
- Modern viewer attachment accepts an existing authorized H264 route without
  inventing a v3 descriptor. It cannot create a replacement route.
- Wire contracts: unchanged. H264 response has resolved_codec=h264 and no
  descriptor; native v3 HEVC and camera negotiation are unchanged.
- Validation: targeted routing/admission and viewer tests, backend quality checks,
  backend-only deployment, browser H264 and HEVC on terminal773.
- Checks: 84 initial focused tests; after retaining advertised v3 H264 support,
  82 HEVC/media-v3/viewer tests pass, Ruff and Pyright pass.
- Status: deployment and live acceptance pending.
