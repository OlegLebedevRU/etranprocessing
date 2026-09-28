# 18E MenuBuilder finite data-only contract export

verification_status: VERIFIED
runtime_verification: NOT_REPEATED

This packet copies exact Git/raw bytes from accepted handoffs. It is documentary provenance for the MenuBuilder 18E consumer. It does not repeat runtime verification or grant neighboring implementation source, tests, private configuration, payment credentials, or server changes. The 18D accepted handoff remains the separate sequence gate.

| Export file | Accepted source handoff | Repository | Source commit | Exact source artifact path | Git/raw SHA-256 | Bytes |
|---|---|---|---|---|---|---:|
| `17e-report.md` | `H-L4D-17E-MB-FIX-01-v1` | `central` | `f031b963f43d9c52fbf28aee9f978157386721f1` | `MenuBuilder/docs/l4desk/handoffs/L4D-17E-MB-FIX-01-v3-report.md` | `6dfa6654b7472351da1147f9a526634d6839507f917294fe75f9781f0f2e1be0` | 47936 |
| `17f-report.md` | `H-L4D-17F-DOCS-FIX-01-v1` | `central` | `a4b98e95e1493c76f0fdfc409a6441065f8e7020` | `l4desk-service/docs/handoffs/L4D-17F-DOCS-FIX-01-v2-report.md` | `82b603df099f48e66f0ba86db362b1d041920ec47e14518ca962502daed76abb` | 19422 |
| `17f-final-gate.json` | `H-L4D-17F-DOCS-FIX-01-v1` | `central` | `a4b98e95e1493c76f0fdfc409a6441065f8e7020` | `l4desk-service/docs/handoffs/evidence/17f-deploy-20260927/final-contract-gate.json` | `ddb5d97380f623a415d5249488ebc7c9c5d5bd2cf9d2f05dd902ef03775c3703` | 4918 |
| `18a-schema.json` | `H-L4D-18A-SHARED-v1` | `central` | `537a1e493c83d1fa8e8cb765228be8d1b24a1d62` | `shared/docs/l4desk/schema-v1.json` | `52f481dd3d9985c54b5388a1d9e63062a8fdbe626870b58a83b3461c3e08e49f` | 185309 |
| `18a-package-source.json` | `H-L4D-18A-SHARED-v1` | `central` | `537a1e493c83d1fa8e8cb765228be8d1b24a1d62` | `shared/docs/l4desk/package-source-v011.json` | `364efa7b369cdcb8da12025b377518834b6013fd693a28f321a81dcbe18a68c6` | 2336 |
| `18a-report.md` | `H-L4D-18A-SHARED-v1` | `central` | `2b38855000d67f06269ddf40e6305382abe2d3a9` | `shared/docs/l4desk/handoffs/L4D-18A-SHARED-FIX-01-report.md` | `c7a6bebcba52dce053d01715699421d04edb301b0e3d858d1afba508d393b12a` | 9456 |
| `18b-report.md` | `H-L4D-18B-PB-v1` | `central` | `3d76c7d6d5165959e4cd077b4730a5712d7e1f6a` | `ProcessingBackend/docs/l4desk/handoffs/L4D-18B-PB-FIX-01-report.md` | `388f232b5aa3c76408b80c232844cd8b57fc1207d94d060da4da24de19cea0ee` | 7835 |
| `18c-report.md` | `H-L4D-18C-IOT-v1` | `iot` | `fa7a91a631ced5134104e8bef61a69aa620c26a3` | `docs/l4desk/handoffs/L4D-18C-IOT-FIX-01-report.md` | `7f8fd3a4b312f75bc70e7bdeea236baae14420331e78d173d2a042f77ae18dc5` | 8947 |
| `18c-final-smoke.json` | `H-L4D-18C-IOT-v1` | `iot` | `fa7a91a631ced5134104e8bef61a69aa620c26a3` | `docs/l4desk/handoffs/evidence/18c-rollout/18c-production-final-smoke.json` | `042f54ea307fa124bd37f0a87a3ba322fcbd558a7d75bbc49bb7f3c1b5676823` | 477 |
| `18d-report.md` | `H-L4D-18D-MEDIA-v1` | `central` | `ed453db73b2dad4855cb099dfa5fdf9cf94ff655` | `l4media/docs/l4desk/handoffs/L4D-18D-MEDIA-FIX-01-report.md` | `85f554fc140ee10a2b7ba0b11d096a4e010af57a013dc2e723f37962b8f22b30` | 10120 |
| `18d-evidence.json` | `H-L4D-18D-MEDIA-v1` | `central` | `ed453db73b2dad4855cb099dfa5fdf9cf94ff655` | `l4media/docs/l4desk/handoffs/L4D-18D-MEDIA-FIX-01-evidence.json` | `7ddc676e9fd3f002a9b68ff21fe30146fe44367f925f5baed945b9a5e90896b6` | 4447 |
| `18d-openapi.json` | `H-L4D-18D-MEDIA-v1` | `central` | `e6e681dcf74fb0e81da5cf1f7f0fc2d39dca7f33` | `l4media/ingress/openapi.json` | `02298c2cba4c311e24d7ba893edc206ac01cb902746a931976c28225514cfd1c` | 16578 |
| `18d-mb-auth-report.md` | `H-L4D-18D-AUTH-MB-v1` | `central` | `7af9517dfea82d01a1cc6f62c8eb176e70353b97` | `MenuBuilder/docs/l4desk/handoffs/L4D-18D-AUTH-MB-FIX-01-report.md` | `61d74d4ff286f8d31c3d842b04bcedd0b96224425e7c1fe2f711137c00c0d010` | 4792 |

Gate and scope limits:
- The original 18E prompt names historical `H-L4D-17E-MB-v1` and `H-L4D-17F-DOCS-v1`, which are not accepted handoffs. Their accepted FIX handoffs remain immutable provenance and the exact 17E/17F reports are copied here.
- Accepted 18A/18B/18C/18D handoffs include neighboring implementation artifacts. This export binds only selected data-only report/schema/API/evidence bytes; no neighboring source or tests are granted to the MenuBuilder runtime owner.
- 18A published exact Git source for `etranprocessing-db==0.1.1`; its wheel and sdist were local verification artifacts, not a Python index release. ProcessingBackend owns schema migration 027; the 18E consumer must not create or apply migrations.
- The 17F report distinguishes deployed MenuBuilder baseline `fb2273c0bb633426067b2a9487538d84155ef446` / image `sha256:e0d17a09092e34b206eeb313b155186c419e55ee0419a684eb5ae2ce32e50090` from candidate/test source `4184ee930e869ddfb044029512e51e7a69ed20f6` / image `sha256:fa37ec23d4c2a807b7209440af00a123bdecbb2a862a3e3e1cc1b3cd1e091345`. The 18D MB auth report says both consumers retained those application images while private token configuration rotated.
- The accepted Agent `1.8.2-beta-1` remains unchanged. Current 18E activation, financial safety, live provider compatibility, smoke and rollback must be verified by the MenuBuilder owner; historic reports are not a fresh 18E E2E result.
- Existing authorized test tenants only, payment mock only and bounded period simulation are user constraints. No new tenant, live YooKassa charge or three-year wait is authorized by this export.
- Archive worker/purge stay disabled. 18D accepted a synthetic dry-run/sample restore; an off-host backup schedule and retention rollout were not attested.

Checks: 13/13 selected copies matched the accepted artifact SHA-256 at the exact immutable Git commits; 6 JSON copies parsed. No runtime, SSH, build, deploy or new provider test was performed for this export.
