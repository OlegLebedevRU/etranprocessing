# Terminal Issuing CA migration — handoff

## Task intake / scope

- Owner: ProcessingBackend issuance/renewal and strict terminal SN/serial authentication; no schema changes.
- Producer → consumer: PB signer → PKCS#7/PFX → Windows Store → HTTP ingress/PB and RabbitMQ.
- Authorized scope: implementation, main publication, standard builder release, private issuer provisioning and rollout of ProcessingBackend only. Terminal773 renewal is initiated by the user through MenuBuilder.
- Invariants: existing root trust anchor, intentional optional_no_ca, unchanged PIN/idempotency/serial ownership; no root key in PB, no secrets in Git/build; old and new certificate paths accepted.
- No native/MQTT client implementation, legacy edits, RabbitMQ changes or app1 deployment.
- [MCP Ops Readiness: UNAVAILABLE]: no callable Ops tools. SSH fallback preflight passed: available RAM1900 MiB, root58%, load0.14.

## Production release — verified 2026-10-11 19:04 UTC

- Accepted source: main `ba63536b3e6e43d85d7d71ab0f8fe2b028eaade4`.
- Standard builder → registry → production release succeeded; builder journal/checkpoint and production release record agree.
- Image: `dev-leo4-ru.cr.cloud.ru/etran/processingbackend@sha256:2b58d28d6bdc251df0c547cecd340db5ae1111d5047b127bafc31971ea0c726d`.
- Running PB container: `44ba94a5b40b` (previous `c9cd9e3c944e`). Runtime image/digest and OCI revision label checked.
- Standard Alembic upgrade passed; schema remains032. No migration, DB credential or network configuration was changed.
- Runtime check: `CA_SIGNER_BACKEND=local`, RSA4096 issuing key decrypts and matches the packaged public certificate; pinned root and issuer fingerprints match. This check did not issue a certificate.
- HTTP health200 after startup; release health briefly retried while Uvicorn was starting.
- Secrets mount is read-only. Host directory0700, encrypted key/password files0600. Password/key never entered Git, image, builder or command arguments.
- Verified digest persisted in production base Compose and single-service override. Repository Compose is pinned to the same digest.
- All neighbor container IDs stayed unchanged during this resumed release, including RabbitMQ, MenuBuilder, app1 and both Nginx containers. Earlier independent app1 work was confirmed by the user.

## Implemented

- Encrypted RSA issuer loader with the operator's named dotenv password format, key match and pinned hierarchy validation at startup.
- Explicit local/external selection; threaded local signing; no failure fallback. External remains the application compatibility default, while production explicitly selects local.
- Exact observed terminal773 leaf profile; notBefore minus one day, expiry capped by CA lifetime, positive159-bit serial with40 hex digits.
- Pinned new-issuer validation before strict DB authentication; old root leaves and endpoint-scoped ListMenuFile Authorization remain supported.
- Complete multi-cert PKCS#7/PFX; canonical DER CertificateSet ordering.
- Git/Docker secret exclusions, reviewed private SSH-stdin delivery and provisioning scripts.

## Verification evidence

- Root SHA256: `47614a31ba32cda8e389ade576db2878c4a7cb8e09ac4f4d1e8774d629aa9d25`.
- Issuer SHA256: `8d98c82fa2a3497e072dbb018afb0109615cbf90df7597c55485017250bff08d`.
- Real local773 certificate thumbprint `311F02628A6088FAC5D7A7FAFF2D056C48FEFC02`: ordered Subject and all four extensions exactly equal to fixture-root local signer output. Normal365-day issuance produces366-day leaf validity because notBefore is backdated one day.
- PB full suite:309 passed,1 existing skip,20 existing FastAPI/mock warnings. Ruff, format and Pyright passed, including on the standard Linux builder.
- Focused CA/deployment suite:38 passed; release CI suite:34 passed.
- Production RabbitMQ read-only inspection:4.3.5/OTP27.3.4.17; verify_peer, required client certificate, CN login, internal auth backend and unchanged root. Broker-side Erlang PKIX accepted the issuing certificate against that root.
- CA-path verification does not establish a new-leaf TLS handshake, Windows intermediate installation, MQTT CONNECT or topic authorization.
- No real issuer-signed pilot certificate was generated outside the enrollment/renewal flow; no773 private key was exported or changed.

## Pending terminal pilot / final cutover

| Contract | Verified | Pending |
|---|---|---|
| Setup/renew XML PKCS#7 and PFX | Unit tests preserve profile and full chain | User-triggered773 renewal and Windows installation |
| SN/serial authentication | Root/issuer and negative binding tests | Renewed773 authenticated HTTPS traffic |
| ListMenuFile Authorization | Existing endpoint-only exception retained in tests | Actual consumer behavior after renewal |
| RabbitMQ CN login | Existing root/runtime policy and intermediate PKIX | Fresh leaf+intermediate TLS, MQTT CONNECT and authorized/forbidden topics |
| Recovery/idempotency | Existing issuance/renewal tests pass | Real same-CSR recovery/repeat if needed during pilot |

- User can now initiate773 certificate auto-renew through MenuBuilder. Observe Subject/SAN/KU/EKU, issuer, one-day notBefore, new DB serial, full Windows chain, HTTPS and a fresh RabbitMQ connection.
- Do not impersonate active MQTT client IDs or publish presence during observation. MQTT schemas/QoS/retain/topic ownership are unchanged.
- After new-branch issuance, any rollback must retain the issuer-chain-aware verifier. Do not restore the pre-migration image.
- Cloud function shutdown/deletion of its root key remains unperformed. It needs the cloud-owner path and checks for remaining external consumers; root must not be declared fully offline until that cutover is complete.

## Earlier failed attempts / resolution

- PR63 implemented the signer; PR64 fixed isolated preflight to use the file-owner UID/GID after dropping all Linux capabilities. Secret permissions were not broadened.
- Four prior standard releases stopped on PostgreSQL connection reset/closed errors before PB recreation. Failures also occurred from the old image and production host; running/candidate DB settings matched. No Alembic bypass was used.
- On resume, a fresh bounded DB probe immediately returned SELECT1 and schema032; the standard release then completed. No DB/network fix was applied by this task.
- After deployment, an optional fresh DB connection for the773 baseline timed out during PostgreSQL SSL negotiation, before querying any terminal row. DB connectivity remains intermittent; serial baseline was not re-verified through this query. Local773 still has its previous root-issued certificate. Treat this as an open infrastructure issue during the pilot.

## Cleanup / shared workspace

- Local operator CA material and password files are untouched and ignored as appropriate.
- Task-owned remote public installer/pinning scripts were removed; diagnostic containers exited and no diagnostic process remains in PB.
- Automatic review rejected local synthetic PKI/test-directory/public-DER cleanup with `blocked by policy`; these temporary files remain. No deletion workaround was attempted.
- Shared checkout was synchronized to accepted main; all four unrelated user-untracked paths were preserved.
- A task-labeled stash retains the already published implementation snapshot; the clean release worktree remains available for the773 pilot follow-up.
