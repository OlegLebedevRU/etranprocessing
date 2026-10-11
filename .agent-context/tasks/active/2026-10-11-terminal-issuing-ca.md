# Terminal Issuing CA migration — handoff

## Task intake / scope

- Implement local terminal signer in PB under the existing offline root; preserve certificate consumers.
- Owner: PB issuance, renewal, strict terminal SN/serial and migrations (no schema migration needed).
- Producer → consumer: PB signer → PKCS#7/PFX → Windows Store → HTTP ingress/PB and RabbitMQ.
- Boundaries: PB code/config, deployment distribution and certificate docs. No native client/MQTT client implementation or legacy code. Production rollout is explicitly authorized.
- Invariants: root unchanged/offline target, optional_no_ca intentional, unchanged PIN/idempotency/serial ownership; no root key in PB, no secrets in Git/build; old and new paths accepted.
- Release checkout is based on current origin/main `4b413ed`; concurrent Compose changes are preserved.
- [MCP Ops Readiness: UNAVAILABLE]: no callable Ops tools; SSH read-only fallback resource check passed (RAM available about 2030 MiB, root 57%, load 0.05).

## Implemented

- Encrypted RSA issuer loader with password-file support for the operator's named dotenv format, key match and pinned hierarchy validation at startup.
- Explicit local/external selection, threaded local signing, no failure fallback.
- Exact observed terminal 773 leaf profile; notBefore minus one day, expiry capped by CA lifetime, 40-digit positive serial.
- Pinned new-issuer validation before strict DB auth; old root leaves and scoped ListMenuFile Authorization remain supported.
- Complete multi-cert PKCS#7/PFX; canonical DER CertificateSet ordering.
- Read-only secrets mounts and local signer environment in deployment Compose; secret exclusions in Git/Docker.
- Reviewed installer and SSH stdin delivery scripts; dry-run default and conflicting-key overwrite refusal.

## Contracts

| Contract | Producer | Consumer | Compatibility |
|---|---|---|---|
| Setup/renew XML PKCS#7 | PB | Windows installer | Same XML; intermediate added; actual terminal install pending |
| PFX chain | PB companion | Windows/PFX consumer | All chain certificates included; actual consumer install pending |
| SN/serial auth | ingress/PB | PB handlers | New issuer uses strict branch, old root/legacy retained |
| ListMenuFile Authorization | existing terminal consumer | PB | Existing endpoint-only exception preserved with new-chain verification |
| Certificate CN login | terminal TLS | RabbitMQ MQTT | CN preserved, CA PKIX path verified; new-leaf handshake/CONNECT/ACL pending |

MQTT schemas, QoS/retain, presence and topic ownership are unchanged. TLS exchange
has no application ACK; a successful CA-path check does not establish MQTT authorization.

## Verification evidence

- Operator-provided RSA4096 encrypted issuing key decrypted successfully and matches packaged public issuer.
- Root SHA256: `47614a31ba32cda8e389ade576db2878c4a7cb8e09ac4f4d1e8774d629aa9d25`.
- Issuer SHA256: `8d98c82fa2a3497e072dbb018afb0109615cbf90df7597c55485017250bff08d`.
- Real local terminal 773 certificate thumbprint `311F02628A6088FAC5D7A7FAFF2D056C48FEFC02`: ordered Subject and all four extensions exactly equal to fixture-root local signer output; both validity intervals 366 days for 365-day issuance.
- Production RabbitMQ read-only: version4.3.5/OTP27.3.4.17, verify_peer/required client cert, CN login, internal auth backend, same root. Erlang PKIX returned `issuer_chain_valid` for the new issuer against the broker root.
- PB full suite: 309 passed, 1 skipped (existing), 20 existing FastAPI/mock warnings. New tests use isolated roots and encrypted fixture issuer keys, not production credentials.
- Focused CA + installer suite: 38 passed; Ruff/Pyright passed for app and new code before final documentation edits.
- Initial pytest temp setup failed on pre-existing Windows `.pytest_tmp` permissions; rerun used task-owned fresh temp dirs. No tests were excluded.

## Pending / release gates

- Production build and pull completed; image-only signer preflight passed with real encrypted key. Rollout remains pending: three standard release attempts stopped at Alembic before replacing ProcessingBackend.
- New leaf through Windows Store/Schannel to fresh RabbitMQ TLS + MQTT authorization: not performed.
- Real authenticated enrollment/renewal of a dedicated pilot and repeat/recovery: not performed.
- Cloud function shutdown and deletion of its root key: not performed; needs cloud-owner path and checks for remaining external consumers.
- User explicitly confirmed publication to main, build on the standard builder, issuer-secret installation and rollout of ProcessingBackend only. Terminal 773 renewal remains operator-owned.
- After any new leaf is issued, rollback must keep issuer-chain-aware verifier. Do not revert to pre-migration image.

## Cleanup

- Only public 773 DER was exported for transient profile comparison; its private key was never exported or modified.
- Original operator CA material and password files remain untouched and ignored as appropriate.
- Remote production issuer secrets are installed (directory0700, files0600, read-only mount); local originals are untouched. No test identities were created. Automatic review rejected local test-directory cleanup with "blocked by policy"; synthetic PKI temp directories/public DER remain.
- Production Compose received only PB local-signer settings and the secrets mount, with a0600 backup. RabbitMQ config/permissions and terminal certificate store are unchanged.

## Authorized release progress / external blocker

- Implementation PR63 and preflight UID/GID fix PR64 are merged to main.
- Latest tested release source: `10642e2f8606ad72ffeb880c7b9f2f8904be703c`; concurrent PR65 changed only unrelated release documentation and app1 Compose digest. PB/shared/CI source is unchanged versus `15a4833b2fa2ae7397abcf0a76e0e5a1892a281b`.
- Latest published/pulled PB artifact: `sha256:3032c5ddddcb48bec049d2e7ec28551389760d09ddd6a7169534c632286b7029`. Earlier key/image preflight artifact: `sha256:935427bf0fe2ad209725e73c0ce15b324683be8b9fd23b75834cea7366e42019`.
- Isolated preflight initially could not read user1-owned0600 secrets because all capabilities were dropped; fixed by explicitly using file-owner UID/GID. No secret permissions were broadened.
- Deployment failed before container recreation on PostgreSQL connection reset/connection closed. Reproduced with new and old images and from the production host itself (TCP and PostgreSQL SSLRequest accepted; TLS handshake timed out/reset). Some read-only queries eventually succeeded; schema remained032. Running/candidate database URL components are identical; no credential values were printed.
- Existing PB container remains `c9cd9e3c944e`, image `sha256:a1e3b4970a2bee9f59b3a6700e69e7b9bad6ae1d8a5f1e54dd92270551918ab7`, HTTP health200. No image pin/checkpoint was advanced after failure. Do not start terminal773 renewal as the new-issuer pilot until rollout is verified.
- RabbitMQ, MenuBuilder, Nginx and other observed neighbors retained container IDs. app1 independently changed from `34b3cd1b52ac` to `23cf41dcbb78`; user confirmed parallel app1 work. This task did not deploy app1.
- Task-owned remote public installer files were removed by delivery wrapper. Diagnostic containers exited; no diagnostic Python process remains in PB.
- Resume via the authorized standard component launcher after PostgreSQL connectivity stabilizes. Verify local startup/key load, revision/digest, HTTP health and old-terminal behavior, then let the user initiate773 auto-renew in MenuBuilder.