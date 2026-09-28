# 18B ProcessingBackend data-only evidence export

verification_status: VERIFIED
runtime_verification: NOT_REPEATED

This finite export preserves published historical reports and the accepted
certificate PIN contract as exact Git/raw bytes. It does not accept 18B,
prove current production state, or authorize a server change. The 18B
runtime agent must independently inspect the current package, image,
Alembic head, API behavior, backup and rollback before rollout.

| Export file | Source commit | Exact source path | Git/raw SHA-256 | Bytes |
|---|---|---|---|---:|
| `pb-17b-report.md` | `bc4ec6c3ece48d75c08ff98773af7488da4ace84` | `ProcessingBackend/docs/l4desk/handoffs/L4D-17B-PB-report.md` | `6d58d9af79eba50eea09ebdb5d3fe0254fe1b426b6eaaf0ef33b41a26c84ea56` | 10349 |
| `pb-04b-report.md` | `c889ec5f9b0366d3a61e908f82dcd2e8f4c0b367` | `ProcessingBackend/docs/l4desk/handoffs/L4D-04B-PB-report.md` | `db6595242565600d81f34196c6c1b4501d2ca658d69f86b2ecfa2472a0607a69` | 10230 |
| `pb-06a-report.md` | `291b075f33b2a4f37a84091e2f60d383cbed8fcf` | `ProcessingBackend/docs/l4desk/handoffs/L4D-06A-PB-report.md` | `3120cc31b8360db092ceede22edb663adc8d7c20157a071b37a9083afc35d202` | 19457 |
| `pin-contract.md` | `1971e51f1e7764a31d586174e42513160f8598eb` | `l4desk-service/docs/prompts/contracts/certificate-pin-v1/contract.md` | `1c2787fc2c34343d9b46658bd304cb2110bf554019667712c5596eae8ce6c7f4` | 13266 |
| `pin-schemas.json` | `1971e51f1e7764a31d586174e42513160f8598eb` | `l4desk-service/docs/prompts/contracts/certificate-pin-v1/schemas.json` | `563a00aabf4a539c92f6fccad36596dd63fea05bfc088ca1cf7ddbb8e16f26c0` | 3920 |
| `pin-examples.json` | `1971e51f1e7764a31d586174e42513160f8598eb` | `l4desk-service/docs/prompts/contracts/certificate-pin-v1/examples.json` | `92a3beeb8323d4697e89ef3978d1d54de07202e5c87debec1adb53779cc48737` | 5453 |

Historical input limits:

- `H-L4D-17B-PB-v1` has no artifact paths or digests and uses a noncanonical `deployment_status: ACCEPTED`. Its report is copied as historical evidence only.
- `H-L4D-04B-PB-v1` has two Git/raw digest mismatches. The migration file differs only by LF/CRLF, while `test_schema_migration.py` does not; §10.4 cannot bind that handoff as a whole.
- `H-L4D-06A-PB-v1` has six Git/raw mismatches, all explained by LF/CRLF. This export carries the accepted data-only PIN contract instead of granting its source files as a direct input.
- The PIN contract copies come from accepted `H-L4D-06A-PB-CONTRACT-01-v1`; its four artifacts were published at source commit `1971e51f1e7764a31d586174e42513160f8598eb`. Three contract files are copied here. The separate historical verification report remains available in that accepted handoff, but is not a direct 18B input.
- Source reports describe historical tests and deployment; they do not replace current 18B release checks.
- Source `.py` files, DDL, credentials, neighboring repositories, and linked documents are not exported or granted.
