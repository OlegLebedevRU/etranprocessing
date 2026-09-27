# 18D media finite data-only evidence export

verification_status: VERIFIED
runtime_verification: NOT_REPEATED

This packet contains exact Git/raw copies of historical accepted media lifecycle and archive documents plus accepted 18C IoT report/evidence. It grants no executable source, tests, neighboring repository reads or live deployment conclusion. The runtime owner must independently verify current media image/config, management auth, mount/backup readiness, safe archive restore and smoke.

| Export file | Source repository | Source commit | Exact source path | Git/raw SHA-256 | Bytes |
|---|---|---|---|---|---:|
| `08a-openapi.json` | `central` | `37adfd01e5492e6b61e8ecb243579389ae2d858e` | `l4media/ingress/openapi.json` | `831acbc9753bab5d88c036adf9f43d98a28ca45f2143b06d12cb0ce758f80ce4` | 13955 |
| `08a-report.md` | `central` | `aba1339a315bcc5956bb8686a0ad5cc113eee8fd` | `l4media/docs/l4desk/handoffs/L4D-08A-MEDIA-report.md` | `cdd394524f58fc88a04973944d07658c05f2760daa7c60c607a7476192857112` | 17168 |
| `15c-report.md` | `central` | `7c4dd6408fc60e58cc9cda9c6710679f5c890566` | `l4media/docs/l4desk/handoffs/L4D-15C-MEDIA-report.md` | `2940be6d37996923bc6ac5e1997fbe09f9ae09b3ceb8a093d7ae7299604bad7d` | 17377 |
| `archive-contract.md` | `central` | `5d2f23cf54b3b39e47405bf9bb7bc6df3a7e96f7` | `l4desk-service/docs/prompts/contracts/archive-manifest-v1/contract.md` | `568ca7ce6535fe5eb9e388a760aa1bbde672e8ce7c4ffea5af33d3b7da53d7bf` | 23553 |
| `archive-manifest.schema.json` | `central` | `5d2f23cf54b3b39e47405bf9bb7bc6df3a7e96f7` | `l4desk-service/docs/prompts/contracts/archive-manifest-v1/archive-manifest.schema.json` | `3a908c6fe80177126ba783fbf70c3c0de76b8428b3d8fd8a135e08cbd347bfc2` | 8972 |
| `archive-examples.json` | `central` | `5d2f23cf54b3b39e47405bf9bb7bc6df3a7e96f7` | `l4desk-service/docs/prompts/contracts/archive-manifest-v1/examples.json` | `29379c6f4559992f86f48ef194379814228c8e653155af64e219c8c5c686a717` | 21545 |
| `18c-report.md` | `iot` | `fa7a91a631ced5134104e8bef61a69aa620c26a3` | `docs/l4desk/handoffs/L4D-18C-IOT-FIX-01-report.md` | `7f8fd3a4b312f75bc70e7bdeea236baae14420331e78d173d2a042f77ae18dc5` | 8947 |
| `18c-production-archive.json` | `iot` | `fa7a91a631ced5134104e8bef61a69aa620c26a3` | `docs/l4desk/handoffs/evidence/18c-rollout/18c-production-archive.json` | `8183ac58ca7035ef20b91b12f1017fde6f7ff38df0b39b42a4c4ced8f0791600` | 239 |
| `18c-production-final-smoke.json` | `iot` | `fa7a91a631ced5134104e8bef61a69aa620c26a3` | `docs/l4desk/handoffs/evidence/18c-rollout/18c-production-final-smoke.json` | `042f54ea307fa124bd37f0a87a3ba322fcbd558a7d75bbc49bb7f3c1b5676823` | 477 |

Historical gate limits:
- H-L4D-17D-MEDIA-v1 has no artifact_paths/artifact_sha256 and an old deployment_status; it is provenance only, not a direct FIX input.
- H-L4D-08A-MEDIA-v1 is addressed to 08B and its OpenAPI historical digest differs from Git/raw bytes; this export binds exact published bytes without changing the old handoff.
- H-L4D-15C-MEDIA-v1 is addressed to 16 and includes executable source/test artifact paths; this export conveys only its published report and shared archive contract documents.
- H-L4D-18C-IOT-v1 is the accepted sequence gate. Its subject artifacts include neighboring implementation; only the finite data-only copies above are provided for media consumption.
- The existing accepted H-L4D-17D-MEDIA-CONTRACT-01-v1 remains a separate data-only direct FIX input with its own finite digest grant.
- Empty-month archive behavior, production purge disabled, no production restore/rollback exercise, and old Agent contract-only validation are disclosed in the copied 18C report; none are silently upgraded to a success claim.

Checks performed for this export:
- 9/9 copies matched immutable Git/raw source bytes and SHA-256.
- JSON copies parsed; no credential-shaped value or private key marker was found by the bounded export scan.
- The exact archive-contract.md copy retains historical Markdown hard-break spaces; git diff --check reports those lines, which are preserved to keep the published source digest exact.
- No runtime/SSH, tests, server mutation, or media deploy was performed by the controller.
