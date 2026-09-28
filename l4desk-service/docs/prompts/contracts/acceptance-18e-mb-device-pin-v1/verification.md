# 18E finite IoT device and PB PIN contract verification

verification_status: VERIFIED
runtime_verification: NOT_REPEATED

The controller read only the exact ten IoT provenance files below from the accepted 18C implementation commit and generated a bounded data-only device projection. It copied three PIN documents byte-for-byte from the accepted 18B evidence export. No executable neighbor source or private configuration is exported.

| IoT provenance file | Accepted source commit | Git/raw SHA-256 |
|---|---|---|
| `app-service/api/__init__.py` | `35f1fce054e388a2206af45f1416b9572b0614a9` | `ab0fb414375f6bca3ca61500e0cdfdcf0910d80e8d9652aced352f1aa36a6b3f` |
| `app-service/api/internal_v1/__init__.py` | `35f1fce054e388a2206af45f1416b9572b0614a9` | `88d249cbff0fc75e7d9f35562119c401ce4fa343f4e98c290baa473ee0b30194` |
| `app-service/api/internal_v1/devices.py` | `35f1fce054e388a2206af45f1416b9572b0614a9` | `3e0491d68e91e6ca3008253ddd159906e0d014b99800cbee01bc467fcfa9bf96` |
| `app-service/api/internal_v1/provisioning.py` | `35f1fce054e388a2206af45f1416b9572b0614a9` | `dc62774668ba6c574b67b12e5f757fa3c6fac58b2b11d9703ce506126aa9ff82` |
| `app-service/api/internal_v1/internal_depends.py` | `35f1fce054e388a2206af45f1416b9572b0614a9` | `592eea2fb41978c9aa32cb66eb6b423f69f72ece19ffec5f306afc474d632b9a` |
| `app-service/core/config.py` | `35f1fce054e388a2206af45f1416b9572b0614a9` | `21f6290b0d2c4cfa6347e28e4abc55278f60364fab3f4a2cb7c32b51aa13784e` |
| `app-service/core/schemas/devices.py` | `35f1fce054e388a2206af45f1416b9572b0614a9` | `6c77d5c229ecaf0b553a9805b4cc00e4f4a38af8bc31be04910ec88b802421f7` |
| `app-service/core/schemas/provisioning.py` | `35f1fce054e388a2206af45f1416b9572b0614a9` | `52d882658ae3b3d7bfa072a19a13de1700c7db77c599e3e8430946d3442fdd16` |
| `app-service/core/services/provisioning.py` | `35f1fce054e388a2206af45f1416b9572b0614a9` | `753970ec174eb36dbf55b4195c6894dbef1e224bf501647e934c91254e294aaf` |
| `app-service/core/crud/device_repo.py` | `35f1fce054e388a2206af45f1416b9572b0614a9` | `8cf6e84fc10f4406c4e47e5aaf81f5530ac1dcd9754ba8a3334c1a4474863a4b` |

| PIN copy | Accepted source handoff | Source commit | Source artifact path | Git/raw SHA-256 |
|---|---|---|---|---|
| `pin-contract.md` | `H-L4D-18B-PB-EVIDENCE-CONTRACT-01-v1` | `61d79a9ae9790ab6eee991ad64539e893aab6ade` | `l4desk-service/docs/prompts/contracts/acceptance-18b-pb-evidence-v1/pin-contract.md` | `1c2787fc2c34343d9b46658bd304cb2110bf554019667712c5596eae8ce6c7f4` |
| `pin-schemas.json` | `H-L4D-18B-PB-EVIDENCE-CONTRACT-01-v1` | `61d79a9ae9790ab6eee991ad64539e893aab6ade` | `l4desk-service/docs/prompts/contracts/acceptance-18b-pb-evidence-v1/pin-schemas.json` | `563a00aabf4a539c92f6fccad36596dd63fea05bfc088ca1cf7ddbb8e16f26c0` |
| `pin-examples.json` | `H-L4D-18B-PB-EVIDENCE-CONTRACT-01-v1` | `61d79a9ae9790ab6eee991ad64539e893aab6ade` | `l4desk-service/docs/prompts/contracts/acceptance-18b-pb-evidence-v1/pin-examples.json` | `92a3beeb8323d4697e89ef3978d1d54de07202e5c87debec1adb53779cc48737` |

Contract limits:
- The IoT source permits arbitrary string tags; the `sys` enum windows/linux/esp32 is a MenuBuilder input requirement, not an IoT provider restriction.
- IoT source requires `sn` for provisioning; PB PIN source requires a verified `sn` and business terminal ID for issuance. `device_id` is the UI and IoT list primary key but is not a substitute for SN at those provider boundaries.
- IoT list connection state is a sampled/provider state, not instant proof of browser video. Missing connection or transport error must not be converted to offline.
- Tenant authorization must be established in MenuBuilder before forwarding org_id, device_id or an operation_id. No browser-facing service credential.
- The accepted PB PIN documentation predates 18B token hardening; 18B live report establishes a private token and public nginx denial. No secret value is included here.
- Production provider behavior was not repeated for this documentation export. The MB owner must run current consumer contract tests and bounded smoke.

Checks: 10/10 finite IoT source blobs read by exact commit; 3/3 PIN copies matched accepted artifact digests; generated JSON documents parsed; no runtime, SSH, server mutation, build or deploy performed.
