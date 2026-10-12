# ProcessingBackend issuer compatibility and FM registration

## Task intake
- Goal: restore FM registration for fresh-installed suite 1.13.34 on device 1000007; audit all ProcessingBackend certificate consumers for root/issuing-CA compatibility.
- Owner: ProcessingBackend mTLS authentication and fm_agents persistence. Native hello producer remains unchanged.
- Flow: L4Con periodic HTTPS hello → existing Leo4Proxy/ingress → verified_terminal_identity/get_current_terminal → FM registration/readiness.
- Invariants: pinned root and issuing CA, TLS possession, current SN/serial, active terminal, instance binding; retain existing legacy SubCA endpoint behavior and optional_no_ca ingress configuration.
- Verification: real test certificate hierarchy, issuer/root auth matrix, FM registration regression, complete PB tests and Ruff/Pyright, standard builder release, passive live registration observation.
- Prohibited actions: no live issuance, native installation, service restart on terminal, manual fm_agents serial edits, RabbitMQ configuration changes or collision reset.

## Evidence before fix
- Device 1000007 is active. Current terminal serial 763F9A79CDDB045C51D441ED91FFF4EFCFC4017A was authenticated at ingress for /api/file-manager/v1/agent/hello.
- fm_agents retained previous serial 55E0AE606C556EDD083605C55C0CA354456079E8, last hello 2026-10-12 11:58:55 UTC, producing readiness certificate_changed.
- FM hello additionally required immediate issuer DN to contain iot.leo4.ru, after its dependency already verified the pinned chain. This rejects valid Terminal Issuing CA 01 leaves with 403 before upsert.
- Remove this redundant name gate; use existing verified identity dependency for both accepted branches. No new trust sources or crypto strictness introduced.

## Scope audit
| Service / boundary | Existing certificate path | Result |
| --- | --- | --- |
| payment, techgate (all handlers), gategauge | get_current_terminal | Root and issuing CA already use strict SN/current serial; legacy SubCA lookup retained |
| licensebilling and legacy alias | get_terminal_license_state → get_current_terminal | Same auth; commercial policy unchanged |
| ListMenuFile | get_current_terminal, scoped Authorization compatibility | Both CA branches supported; Authorization remains limited to this endpoint |
| leo4proxy/policy | get_current_terminal | Both CA branches supported; inactive terminal policy semantics retained |
| companion PFX map and v1 alias | get_current_terminal → shared sign_csr | Both incoming CA branches supported; outgoing chain includes configured issuer/root |
| certificates setup/check | Existing PIN/CSR flow | No additional issuer name gate; shared local signer and chain serialization retained |
| certificates renew | verified_terminal_identity + purpose-bound PIN/owner validation | Both pinned root-issued and intermediate-issued leaves supported; SubCA not admitted to this newer renewal API |
| FM hello and other agent calls | verified_terminal_identity + get_current_terminal | Fixed extra root-name gate; other agent calls already share verified chain/current identity |
| FM internal API, health | Service credential / public health respectively | Separate authentication, no terminal issuer classification |
| Terminal ingress | optional_no_ca, overwritten client certificate headers | Existing compatibility configuration unchanged |

No additional root-only issuer gates found in application consumers. The root CN selector in ca_trust identifies the pinned trust anchor, not the leaf issuer, and remains required.

## Validation and limits
- Added FM registration tests for real intermediate/root signatures and rejection without verified TLS possession.
- Extended shared terminal authentication checks over ten endpoint paths for both root and issuer. These test auth dependency behavior, not all business operations end to end.
- Existing legacy SubCA, wrong SN/serial, invalid leaf/issuer, expiry, ListMenuFile Authorization and renewal checks remain in the full suite.
- Local PB suite: 333 passed, 1 skipped; Ruff check/format and Pyright app passed. Existing mock/deprecation warnings noted; no additional failures.
- PR 82 merged as cc81203d9e9babea37833d9538285b37eecc3b1d; standard builder released processingbackend digest sha256:bd7b18314f1574c40d20bc734eef6c47dd923b1b897b8150238bca1497fa373d. Builder tests/quality, Alembic and health verification passed (initial startup health probe retried successfully).
- Production container f2995f9ab007 carries that revision/digest. All other container IDs unchanged, including app1, RabbitMQ, MB, and nginx.
- Passive live observation 2026-10-12 12:10:55 UTC: device 1000007 FM hello last_seen 12:10:42, serial matches current terminal serial, protocol 2, compatible/available/write_available=true, state=ready. Device 773 also ready with matching current serial and fresh hello.
- No native update, manual certificate registration, serial rewrite or terminal service restart needed. Complete user-driven file operation acceptance remains separate.
- Changed-file credential scan matched only existing fixture-only test password. Automatic command review rejected cleanup of ignored local .pytest_tmp (both recursive and exact file-list deletion), reporting only blocked by policy. Test fixtures remain local and ignored; no production credentials were copied.
