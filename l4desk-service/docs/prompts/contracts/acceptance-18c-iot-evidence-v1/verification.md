# 18C IoT data-only evidence export

verification_status: VERIFIED
runtime_verification: NOT_REPEATED

This finite export preserves exact Git/raw historical reports, schemas,
OpenAPI and synthetic fixtures. It is documentation evidence, not current
IoT release acceptance or proof of a production deploy. Its executable
source, migrations, test code, private configuration and linked files are
outside this export and not a consumer read grant.

| Export file | Source repository | Source commit | Exact source path | Git/raw SHA-256 | Bytes |
|---|---|---|---|---|---:|
| `01c-report.md` | `central` | `b07962f8805666581d87a7d4b064dcafb2e869fb` | `l4desk-service/docs/handoffs/L4D-01C-DOCS-report.md` | `1c412fa34400a53163694fb1891b20b5e0088d5fdb57ddb315ffd20a879fa34b` | 26194 |
| `01c-agent-compatibility.json` | `central` | `b07962f8805666581d87a7d4b064dcafb2e869fb` | `tools/docs/l4desk/contracts/agent_compatibility_contract_v1.json` | `51a29c80272b1e92c2d80b65057b413c4bdad9c7df6b8d0c274b83037ba8db4d` | 4784 |
| `01c-golden-vectors.json` | `central` | `b07962f8805666581d87a7d4b064dcafb2e869fb` | `tools/docs/l4desk/fixtures/golden_vectors_v1.json` | `0ae6f4b2815d95f41c41eba80712ed1184f05cd81740a2597246b54dfa24a202` | 17963 |
| `02-event-feed-contract.json` | `iot` | `a5524d356dda343eca96010d16535d9f37ff4ece` | `docs/l4desk/contracts/iot_event_feed_contract_v1.json` | `07be82d70e768ae0a44f24f6e5de6b948a039b746a798ce9c08179fbae810a77` | 5281 |
| `02-event-feed-openapi.json` | `iot` | `a5524d356dda343eca96010d16535d9f37ff4ece` | `docs/l4desk/contracts/schemas/iot_event_feed_openapi.json` | `8196befa2b3e103ec27cfbd39f65cbd230de55de037cadfbb890b06792d4d324` | 27595 |
| `02-remote-session-event.schema.json` | `iot` | `a5524d356dda343eca96010d16535d9f37ff4ece` | `docs/l4desk/contracts/schemas/remote_session_event.schema.json` | `4d7393d0dcd1ae62f04e0ad488b6bab519d8d7e357f0cad569809743cb6270e9` | 3544 |
| `02-remote-session.schema.json` | `iot` | `a5524d356dda343eca96010d16535d9f37ff4ece` | `docs/l4desk/contracts/schemas/remote_session.schema.json` | `c05027474d31f993954451d388667370caadcaeee044997a7a5a97e066cbb1ad` | 6778 |
| `02-event-feed-examples.json` | `iot` | `a5524d356dda343eca96010d16535d9f37ff4ece` | `docs/l4desk/fixtures/iot_event_feed_examples_v1.json` | `41734c7b68850b083eefc07891183c91965c88d0a6589f15e265be0563006f61` | 8394 |
| `06b-provision-openapi.json` | `iot` | `4a0f9d4b218e96273eed605e9edd0f9ab3564b68` | `docs/l4desk/contracts/schemas/device_provisioning_openapi.json` | `bf15db6fff2fd6cddd7b14eea50e7be6c62ca22fd640706e564578e5d408a7b3` | 14374 |
| `06b-provision-request.schema.json` | `iot` | `4a0f9d4b218e96273eed605e9edd0f9ab3564b68` | `docs/l4desk/contracts/schemas/device_provision_request.schema.json` | `baad69034c08785c2fab3f3c2affbeba938da6340513ceb5bfbf8972a18520fa` | 1906 |
| `06b-provision-response.schema.json` | `iot` | `4a0f9d4b218e96273eed605e9edd0f9ab3564b68` | `docs/l4desk/contracts/schemas/device_provision_response.schema.json` | `e1bbb431378086b6b9b57ded34764d6ce251041522a4c5fd95d4b6ed0a096802` | 2779 |
| `06b-provision-examples.json` | `iot` | `4a0f9d4b218e96273eed605e9edd0f9ab3564b68` | `docs/l4desk/fixtures/device_provisioning_examples_v1.json` | `abe3a4c8e1136103ca16e45ed5b778c4ba55f0cddb8f693bdd338cfa096c3653` | 3110 |
| `06b-report.md` | `iot` | `2bca5e86ac916aef7478443d089c983efa708bc3` | `docs/l4desk/handoffs/L4D-06B-IOT-FIX-01-report.md` | `5759fc1ab00dacfe2f16585f4df2753a8b4d5a8bc197af22f5b99c0fcff62d80` | 33990 |
| `07-remote-session.schema.json` | `iot` | `c4e892f4c1dbf8f967109e8a06c3f63b0c9bd483` | `docs/l4desk/contracts/schemas/remote_session.schema.json` | `db102accadce58a7055ede40e6d4896b9d2a82004157a25b8b9c3e2a80df315b` | 7676 |
| `07-remote-session-event.schema.json` | `iot` | `c4e892f4c1dbf8f967109e8a06c3f63b0c9bd483` | `docs/l4desk/contracts/schemas/remote_session_event.schema.json` | `4d7393d0dcd1ae62f04e0ad488b6bab519d8d7e357f0cad569809743cb6270e9` | 3544 |
| `07-event-feed-openapi.json` | `iot` | `c4e892f4c1dbf8f967109e8a06c3f63b0c9bd483` | `docs/l4desk/contracts/schemas/iot_event_feed_openapi.json` | `dddc1d913e6ac94d82a09312060642bbc99c0d2c39c28b52b649aaea40aac81a` | 41778 |
| `07-report.md` | `iot` | `c4e892f4c1dbf8f967109e8a06c3f63b0c9bd483` | `docs/l4desk/handoffs/L4D-07-IOT-report.md` | `857cbe719dc7a035b79e8218aab9417a6176933ddc076b1884fcf4004e5cd0bd` | 17921 |
| `15b-report.md` | `iot` | `af3b4f06456ddad2c8c44477dc52f25c49ee5722` | `docs/l4desk/handoffs/L4D-15B-IOT-report.md` | `dcfcd947f0e141fc8a7d687a1c3e35d8354c12432b1a3fd5f363db26d022aa3a` | 17823 |
| `17c-report.md` | `iot` | `7c6f75f8169d39e4953f4033695726408d3efa58` | `docs/l4desk/handoffs/L4D-17C-IOT-report.md` | `100ea273756776d445722e7c9d129894b63ffaca04cb80b2bcb25ace1277b949` | 10131 |
| `17c-video-watch-contract.md` | `iot` | `7c6f75f8169d39e4953f4033695726408d3efa58` | `docs/l4desk/handoffs/L4D-17C-VIDEO-WATCH-IOT-01-contract.md` | `ef924d9992b4d319e3a876f01834175ba3fb8b2438698cfb289ccaed40bd5e42` | 2080 |

Historical gate limits:

- `H-L4D-17C-IOT-v1` has no artifact paths/digests; its report is copied as historical evidence, not promoted to a current runtime attestation.
- `H-L4D-15B-IOT-v1` lists seven paths without any digests; its archive report is copied, without executable archive code.
- `H-L4D-01C-DOCS-v1` has three CRLF historical digests, including two data-only tools JSON files. Those JSON bytes are copied without reading tools source.
- `H-L4D-02-IOT-v1` has five LF/CRLF-only historical digests; a separate existing byte binding covers that older handoff, but this export has fresh Git/raw digests.
- `H-L4D-06B-IOT-v1` has four LF/CRLF-only JSON digests; its FIX report appears in later commit `2bca5e86ac916aef7478443d089c983efa708bc3` with the historical report SHA. It was absent from the listed producer commit.
- `H-L4D-07-IOT-v1` has five LF/CRLF-only digests; only three data-only JSON schemas/OpenAPI and the historical report are copied, not migration/test source.
- Reports and schemas describe accepted contracts and historical checks. The 18C agent must independently inspect current IoT source, effective configuration, deployed image, migrations, Agent compatibility, readiness, smoke, metrics and rollback.
- OpenAPI `$ref` values in the three copied OpenAPI artifacts are local `#/...` references; no external references are granted.
- The current `D:\work\iot.leo4.ru\iot-rpc-rest-app-18c` checkout and its production state were not changed by this export.
