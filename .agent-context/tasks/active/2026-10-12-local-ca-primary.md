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
- Builder release and runtime evidence pending. No live certificate issuance or terminal reinstall performed by agent.

## Next acceptance sequence
1. Operator clean-installs signed tools 1.13.35 on both pilots; check visible errors, four SCM services, HTTPS/MQTT, FM and ordinary terminal operations.
2. Run owner-triggered renewal, recovery after install failure and repeated requests. Check current serial, one-day backdating, full chain, actual HTTPS/MQTT use and UI escalation.
3. Coordinate app1 legitimate-rotation recognition: current SN_COLLISION detector must not confuse a confirmed previous→new certificate transition with independent devices. Retain clone detection; no broker reconfiguration required by signer stabilization.
4. Review encrypted issuing-key backup/restore, least-privilege access, expiry monitoring and issuance audit.
5. Cloud owner inventories remaining external signer users, disables the old function and removes its online root key after reviewed cutover. Until that is verified, root must not be declared fully offline.
6. Restore strict release checks incrementally, then admit measured remote auto-update transitions separately; do not infer compatibility from this manual reset pilot.
