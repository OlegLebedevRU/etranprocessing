# 18D media auth contract provenance

verification_status: VERIFIED
runtime_verification: NOT_REPEATED

Finite read-only source inspection at the published media baseline commit. The controller checked only symbol presence and OpenAPI structure; no secret/default value was printed, copied or recorded.

| Source path | Git/raw SHA-256 | Bytes |
|---|---|---:|
| `l4media/ingress/openapi.json` | `02298c2cba4c311e24d7ba893edc206ac01cb902746a931976c28225514cfd1c` | 16578 |
| `l4media/ingress/src/l4media_ingress.c` | `90b98bd2462973d300824dd14bfb1381e722c0dfad5dd28e028d9a89f1f53098` | 48786 |
| `l4media/ingress/src/media_lifecycle.h` | `154ff73645635f222cccaedcc56ea1417fa85c417ca131b095d3cf6466c6da77` | 47248 |
| `l4media/compose.yaml` | `52dfcbfec05a1e6f5b6109fa58d328b1b8a2a2087c9fa951642527a541669e78` | 2958 |

Checks:
- OpenAPI parsed and declares `ServiceTokenAuth` as the `X-Media-Service-Token` header; BearerAuth is also declared.
- Provider implementation and Compose contain the exact environment key names; no private configuration file was opened.
- Source contains tracked defaults and the current fail-open risk reported by the runtime agent; correction target is stated in contract.md, not claimed as implemented.
- MenuBuilder consumer environment key and isolated test-backend use have not been inspected by this controller; the MB-owned step must determine them in its own scope.
- No runtime check, host mutation, credential rotation, media deploy or MB restart was performed.
