# Local terminal issuing CA as primary signer

## Owner direction / scope
- Established issuer-signed certificates and suite 1.13.34 are installed on both pilot terminals; FM registration fixed and verified.
- ProcessingBackend becomes the primary terminal signer. Existing root-issued and intermediate-issued leaves remain accepted; no issuer/serial rollback.
- Native manual installation pilot belongs to standalone l4tools: 1.13.35 clean removal then fresh deployment, operator-controlled, no deep automatic-transition acceptance yet.

## Change / validation
- Default ca_signer_backend changes from external to local. External is an explicit compatibility selection; local failure never invokes it automatically.
- Existing startup preflight requires the encrypted issuing key, password, key/certificate match and pinned CA validity. No root key is needed or provisioned in ProcessingBackend.
- General API tests explicitly select external to avoid depending on a production issuer key; real local CA tests still use isolated encrypted fixture keys.
- Added default/explicit selection regression. PB full suite: 334 passed, 1 skipped; Ruff/format/Pyright passed. Secrets remain external and mount ACLs unchanged.
- PR #84 merged; source 5703b044ac7a30cd9ecb67232e8ecb9b4d05f0b0 released through the standard processingbackend builder launcher on 2026-10-12.
- Production image sha256:f299eb6938d889fd92933e4d8fb09cddd398d648dffa1752632288f228e7191b; container 04e468730a04. Health 200, signer local, issuing key/certificate match true. Root SHA256 47614a31ba32cda8e389ade576db2878c4a7cb8e09ac4f4d1e8774d629aa9d25; issuer SHA256 8d98c82fa2a3497e072dbb018afb0109615cbf90df7597c55485017250bff08d.
- Builder checks passed (334 tests passed, one skipped; Ruff/format/Pyright); migration and deployment completed. Neighbor container IDs unchanged, including RabbitMQ, app1 and MenuBuilder. No live certificate issuance or terminal reinstall performed by agent.

## Next acceptance sequence
1. Operator clean-installs signed tools 1.13.35 on both pilots; check visible errors, four SCM services, HTTPS/MQTT, FM and ordinary terminal operations.
2. Run owner-triggered renewal, recovery after install failure and repeated requests. Check current serial, one-day backdating, full chain, actual HTTPS/MQTT use and UI escalation.
3. Coordinate app1 legitimate-rotation recognition: current SN_COLLISION detector must not confuse a confirmed previous→new certificate transition with independent devices. Retain clone detection; no broker reconfiguration required by signer stabilization.
4. Review encrypted issuing-key backup/restore, least-privilege access, expiry monitoring and issuance audit.
5. Cloud owner inventories remaining external signer users, disables the old function and removes its online root key after reviewed cutover. Until that is verified, root must not be declared fully offline.
6. Restore strict release checks incrementally, then admit measured remote auto-update transitions separately; do not infer compatibility from this manual reset pilot.
