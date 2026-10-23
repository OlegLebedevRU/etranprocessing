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

## Terminal773 renewal incident — 2026-10-11, read-only investigation

Scope: diagnose the user-triggered renewal; no new issuance, PIN replay, certificate import, serial rollback, authentication relaxation or service restart was performed. Runtime and server-code evidence below do not establish the native installer's exact failing branch.

| UTC / boundary | Observed evidence | Correlation / conclusion |
|---|---|---|
| 19:16:05 | Renewal PIN row327 created | No PIN value retained in this handoff |
| 19:16:20 | MenuBuilder queued RPC7011 | Task e3e68a6e-981c-41e5-bac1-da4d1d65b57c, pin_id327 |
| 19:17:35–36 | PB certificate log records successful CHECK and SETUP | SN a4b0000773c82116d210826 / device773 / org1 |
| 19:17:36 | Persisted response, used PIN and certificate history242; Terminal.cert_serial updated | New serial49267C2D17CE9DF8871018E2D8C7A69CAFDF0A1F; issuance/commit succeeded |
| First task result | IoT reports failed, HTTP-style result500, exit_code5, duration3406 ms | Client command failed; no stderr/reason is present in the task result |
| 19:21:17 / 19:22:26 | Two more tasks queued for the same PIN327 | 83775490-d7a2-46cb-9174-fa1d12ca2eed / 8ff7f32b-3844-4bd2-89d8-7ea6fbfef502; both result500/exit5, durations282/140 ms |
| 19:25:55 discovery snapshot | Old serial still presented; validation serial_mismatch, no new-serial observation | Failure is after issuance and before successful new-identity use |
| Local Windows store/runtime | Only previous terminal leaf; Leo4Proxy service and supervisor state reference its thumbprint311F02628A6088FAC5D7A7FAFF2D056C48FEFC02 | Old serial3C812A8A05507ADB336006D7F6C325C388BEF6A7 remains installed |

Persisted new PKCS#7 has root, intermediate, leaf in canonical DER enumeration order. Leaf issuer is Terminal Issuing CA01; notBefore2026-10-10 19:17:36 UTC and notAfter2027-10-11 19:17:36 UTC. The one-day backdate is preserved. Old persisted issuance305 enumerates leaf then root and needs a BER fallback. Native dependence on enumeration order or a direct-root issuer remains a hypothesis until the actual installer branch is checked.

Confirmed status defect: MenuBuilder certificate_renewal.py applies task failure/expiry/cancellation only when latest PIN status is pending. A used PIN without successful discovery always produces issued_waiting_identity, even with result500. The frontend uses that status and does not expose result_code. Thus the displayed waiting status obscures the three failed commands.

Recovery boundary: PB returns the persisted response only for the same CSR/current issuance, within15 minutes of used_at and before PIN expiry. CHECK excludes used PINs; whether the native recovery path skips CHECK requires consumer verification. Installed l4pin documentation says CSR/key and returned PKCS#7 are persisted under DPAPI/protected HKLM; no protected state was decrypted or changed.

ProgramData inspection: accessible files under C:\ProgramData\Leo4\Tools\logs contain only older l4capture/l4desk/mosquitto logs. l4superv/leo4proxy/l4con log directories have no accessible diagnostic file for this attempt. State reports the old thumbprint as valid. This does not prove the absence of protected pending state or other logging destinations.

The user authorized read-only tools/terminal-cert-installer and tools/l4superv. The former path does not exist; runtime documentation identifies tools/l4pin as the actual consumer. Authorization for that additional source path has not been explicitly received. l4superv's inspected transition code reacts to an already changed store/proxy thumbprint and restarts the MQTT stack; it does not explain the observed installer exit5.

Remaining checks: actual l4pin error5 branch, intermediate installation and leaf selection, protected same-key pending-response recovery, then renewed current-serial HTTPS discovery and fresh RabbitMQ connection. Database access remains intermittently unreliable, but the observed renewal327 is committed; a DB outage alone is not an established explanation for this installation failure. No Python/native code was changed or tested during this investigation.

### Follow-up: native source inspection authorized

The user explicitly authorized installer source inspection. Read tools/l4pin plus the already authorized tools/l4superv; no native source was modified, built or executed.

- Confirmed incompatibility: l4pin/src/cert_discovery.c cert_is_leo4_issuer accepts only an immediate issuer CN equal to iot.leo4.ru. cert_store.c rejects the selected certificate using that function before opening MY. A correct Terminal Issuing CA01 leaf therefore cannot pass this installer.
- Additional deterministic design defect: cert_store_install_pkcs7 treats the first non-self-issued certificate as leaf, retains only one CA, and does not use Basic Constraints/public-key matching to distinguish intermediate from terminal leaf. Canonical DER set order is not a leaf-selection contract. The intermediate can be selected as leaf; even if the terminal leaf is selected, the direct-root issuer gate rejects it. Exact Windows enumeration/failing diagnostic was not reproduced through a store-mutating installation.
- Intermediate handling retains only one CA and ignores CA-store installation errors. Full-chain support requires installing the verified intermediate(s) to CA, preserving the existing pinned root trust anchor and explicitly handling failures; do not trust a root merely because it is included in a response.
- l4pin discovery itself excludes the new issuer. l4superv/src/cert_discovery.c is a separate copy with iot.leo4.ru issuer-string filtering. Both require chain-aware discovery; merely bypassing the installation check is insufficient.
- renewal.c returns exit5 after installer failure or pending-value deletion failure. The old-only Windows store plus the incompatible pre-install gate supports installer failure here; no protected pending value was read. RegOpenKey of the known per-SN HKLM path is denied to this account; no elevation/DPAPI decryption attempted.
- Recovery clarification: with a loaded pending response, renewal_run skips both CHECK and SETUP and retries local installation with the same persisted key/PKCS7. The repeated exit5 results indicate it reached that install/cleanup boundary. The server's15-minute response-retrieval window does not govern the already cached local PKCS7 branch. Earlier concern about repeated CHECK blocking this observed cached-response recovery is not supported by the inspected consumer.
- Ordinary GUI/PIN input uses main.c's legacy setup branch, not renewal_run. A valid old certificate causes early success without a network call unless forced. Forced legacy setup does not accept purpose=renew PINs; it also reaches the same incompatible installer for any new issuing-CA response. Do not use fresh PIN issuance or force as recovery for this incident.
- Installed l4pin SHA256 is6E6E2E1213B0DFE13BF245D6292AD2A166096DAE566DC56A1ED26A4A2F3D9810. Its embedded diagnostics match the inspected rejection and persisted-recovery messages. Local build artifacts have different hashes; source/binary equality is not claimed and no exact error-line runtime log was recovered.

Next implementation scope: chain verification against the existing pinned root for both old and new leaves; unambiguous leaf/key/profile selection independent of PKCS7 order; intermediate installation and discovery in native consumers; accurate MenuBuilder issued-but-install-failed state. Verify permutations/wrong-root/wrong-key/CA-as-leaf cases with synthetic PKI first, then recover773 using the existing protected key/response and confirm HTTPS plus fresh MQTT. Audit other native consumers before production rollout. No serial rollback or general old-certificate grace is proposed.

### Certificate subsystem audit / common proposal

User scope: audit tools and propose consolidation, preserving direct-root and intermediate-issued certificates with a self-contained package and no new online trust dependency. Static source audit at HEAD4b950735; no build, code modification, live probe or release.

| Owner / primary files | Current responsibility / divergence | Impact |
|---|---|---|
| l4pin/src/cert_discovery.c, cert_store.c, gui.c | Metadata extraction, CNG-only discovery, exact direct-root issuer CN; GUI repeats store enumeration; PKCS7 install selects first non-self-issued certificate and retains one CA | New issuing leaf rejected; installer, GUI and discovery must agree |
| l4superv/src/cert_discovery.c/.h | Independent copy of discovery; issuer CN/DN substring recognition instead of exact equality | Copies have already drifted; distinct binaries can disagree |
| leo4proxy/src/cert_store.c/.h | Third metadata/selection implementation, exact issuer CN, CNG then CSP fallback; thumbprint/email/default tiers; LocalMachine then CurrentUser fallback | New issuing leaf rejected; preserving existing CSP/store/selection contracts matters |
| l4setup/src/cert_phase.c and build.cmd | Installation/reuse/PIN orchestration; directly compiles l4pin discovery source | Coupled to another component's private implementation; inherits issuer incompatibility |
| l4setup/src/preflight.c | Root provisioning; embedded public PEM/hash and optional payload crt file | Root defined separately from proxy resource; optional file import lacks post-decode equality check against expected embedded root |
| leo4proxy/src/schannel_tls.c, res/leo4proxy.rc | Schannel credential creation and offline pinned-root remote-server chain/name verification using embedded DER resource201 | Existing reusable offline technique; remote TLS server policy must remain distinct from local terminal identity validation |
| l4superv/src/hardware_fingerprint.c; l4pin/src/cert_store.c | Clone reset deletes by broad subject/email substring; renewal cleanup recognizes direct-root plus scoped legacy certsrv | Destructive eligibility differs from discovery; adding intermediate support must not broaden unrelated-certificate deletion |
| l4common/proxy_certificate.c/.h | Parses authenticated SCM selection options for probes and update commands | Already common, but not X.509 parsing, chain validation or installation |

Additional source findings:
- Discovery considers private-key availability, optional SN and NotAfter; it does not check NotBefore. Proxy checks full validity first. Consolidation must report both bounds and keep read-only inventory separate from selection of usable credentials.
- Proxy config's exact cert_thumbprint excludes all other candidates; service_mgr.c polls the same config for hot rotation. A configured old thumbprint can prevent adoption of a newly installed certificate. SCM remains authoritative; do not silently bypass an operator's explicit selector. Managed renewal needs a defined selector update or stable same-SN selection contract.
- Repeated helpers include CN/email/DN extraction, serial endian formatting, thumbprint formatting, time conversion, key accessibility, and certificate-context ownership. l4pin/proxy both define generic CertDetails with different layouts. Shared types/functions should use l4_cert_ prefixes and explicit ownership, avoiding private-header coupling and name collisions.
- PKCS7 CA selection/installation ignores CA-store failures and can put the root into CA while omitting the intermediate. Correct bundle processing requires distinguishing leaf/intermediate/root and returning a structured install error before claiming success.
- No statement about unsupported native components or runtime certificate flows is an E2E result. Audit covered all production C/H hits for direct Windows store/key/chain calls under tools, then read the identified primary sources. Tests were inspected where relevant, not executed.

Recommended common: statically compiled C modules under existing tools/l4common with an isolated certificate namespace/subdirectory. Each EXE gets the same sources/public CA assets; no new DLL, service or deployment dependency. Boundaries: cert_identity (metadata/profile), cert_trust (offline existing-root paths), cert_inventory/selection (read-only candidates plus explicit caller options), cert_bundle (PKCS7 leaf/key/chain), cert_install (mutating Windows provisioning/binding), cert_key (existing CNG key primitives). Separate structured error causes from coarse existing CLI/RPC exit codes.

Trust contract: accept old leaf -> existing root, and leaf -> one or more supplied intermediate CA certificates -> the same root; CA names do not establish trust. The package supplies public root/current issuers independently of prior Windows state. Response intermediates may augment a temporary verification store only after the complete path is verified to the known root; roots supplied by the response never expand anchors. No mandatory AIA fetching, CRL/OCSP or online validation is proposed. Existing terminal profile remains the contract; unrelated Windows trust anchors do not substitute for this root.

Keep orchestration in component owners: PB issuer/PIN/serial, l4pin enrollment and protected recovery state, l4setup provisioning/SCM, proxy Schannel/session rotation, supervisor service/MQTT reaction. Common does not issue certificates, retrieve PINs, call backends, restart services or automatically delete records during discovery. Existing HKLM/DPAPI recovery data must remain readable through migration; ProgramData diagnostics should add failure stage/reason without exposing PIN/key/blob.

Alternatives: (A) only extract metadata/discovery, leaving bundle/install policy duplicated: smaller initial change but insufficient for this incident; (B) full bounded common described above: recommended; (C) shared DLL/certificate daemon: additional versioning, runtime and Win7 deployment complexity with no demonstrated need. Common can be compiled as sources initially; a static library is optional build organization, not a separate distribution artifact.

Suggested sequence: freeze old/new compatibility fixtures and explicit selectors; introduce pure metadata/offline trust and migrate three selection implementations plus setup/GUI adapters; implement order-independent bundle validation and complete intermediate provisioning; define managed thumbprint rotation and structured diagnostics; repair MenuBuilder failure states; synthetic Win32 x86/x64 tests across empty/root-only/root+issuer stores, direct-root/new/multiple-intermediate paths, CSP/CNG where currently allowed, wrong root/key, future NotBefore, and all PKCS7 orders; only then signed suite pilot773 and fresh HTTPS/MQTT acceptance. Clone cleanup and user-profile deletion retain separate reviewed scope/policy.
