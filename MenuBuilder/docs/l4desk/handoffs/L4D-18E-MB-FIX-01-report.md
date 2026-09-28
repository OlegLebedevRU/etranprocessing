# 18E MenuBuilder restricted release — producer report

## Result and boundaries

Prepared for independent controller review under `R-L4D-18E-MB-FIX-01-v3`.
MenuBuilder is deployed; general commercial activation is **not approved**.
The producer does not accept its own handoff or open 18F.
Observation date: 2026-09-28 UTC. Runtime source:
`4f72dc6dbe03acc7975463bd4fa92832c4684638`.
Published operational probe: `aa629c6a2e7e149b0e195bcfb7dd45a280fa344f`.

Scope: MenuBuilder UI/BFF, its release artifacts and its private PIN consumer
configuration. ProcessingBackend schema `027`, accepted shared Git-source
package `etranprocessing-db==0.1.1`, IoT schema `0008_org_reservations`, accepted
18D media and Agent `1.8.2-beta-1` remain unchanged. No migration, provider
source change, Janus build, agent rebuild or Windows service operation occurred.

## Intake and immutable inputs

- Owner: MenuBuilder for portal authorization, terminal onboarding/PIN consumer,
  frontend state and financial services. PB remains certificate/migration owner;
  IoT remains device/presence owner. UI authorization does not replace ownership
  checks in BFF. PIN plaintext is returned only to an authorized tenant user.
- Producer/consumer: browser → authenticated MenuBuilder → accepted device/PIN
  contracts; provider responses must match tenant, terminal, SN and operation.
- Controller registration commit: `8743eff635da35359def624725a2c67059610179`.
  Baseline MenuBuilder source: `4184ee930e869ddfb044029512e51e7a69ed20f6`.
- Three direct data-only inputs, all accepted at intake:
  `H-L4D-18E-MB-EVIDENCE-CONTRACT-01-v1` at `7e1fc3c0eabcdb3fc333d0dbe88b938ae09acdab`,
  `H-L4D-18E-MB-DEVICE-PIN-CONTRACT-01-v1` at `57b7b015778ef46ea1d9f4b4b993e80c54042034`,
  `H-L4D-18E-MB-FIXTURE-CONTRACT-01-v1` at `088ade1279547af90f7d0e682547293768f453fb`.
- Packet manifest SHA-256:
  `d3890e5f27d71e5e61568501fc853c1db88aec48ec63f00a808aa76ae1ce147a`.
  All 27 packet Git/raw files verified, including 24 direct artifacts.
  Per-artifact hashes are preserved in the adjacent inputs JSON.
  `H-L4D-18D-MEDIA-v1` is a sequence gate, not another source-read grant.
  Revoked v1/v2 registrations were not used as the final gate.
- Three historical tests now consume exact approved data copies within
  MenuBuilder; assertions were preserved. No neighboring provider tests or
  configuration were imported as an implicit contract.

## Changes delivered

- Terminal identity is `device_id`; SN is abbreviated, atomic, copyable, and
  used where required by provider protocols. Tenant-filtered deep links do not
  fall back to a different device for a foreign/missing ID.
- L4Desk list reuses device-management presence mapping with periodic refresh;
  unavailable presence is unknown, not an invented offline state.
- Own-terminal PIN current/new endpoints enforce tenant ownership and existing
  management authorization, reject superuser/readonly access and missing SN,
  use no-store, validate provider identity and strip consumed/expired plaintext.
  No commercial prerequisite was added. Durable operation intent, advisory
  serialization and replay protect ambiguous provider failures.
- UI displays an unactivated PIN and retains the same retry UUID across reload.
  Current PIN is fetched independently of stale onboarding state. Async tenant
  guards prevent stale results or PINs crossing a tenant switch.
- Onboarding offers exactly `windows`, `linux`, `esp32` as the existing `sys`
  tag. The choice is validated and retained in onboarding audit/retry state.
  Linux/ESP32 do not receive Windows installer instructions. Old operations
  without a stored choice do not overwrite an existing provider tag.
- Responsive terminal/licence tables keep useful column widths and horizontal
  scroll. Initial video inventory loads for deep-linked/default selection;
  a delayed inventory result cannot overwrite the selected device.
- Soft-deleted L4Desk terminals disappear from lists/counts; ordinary terminals
  without a L4Desk row remain visible. Role 5 can open the settings profile,
  matching backend authorization; viewer restrictions are preserved.
- Agent metadata identifies accepted `1.8.2-beta-1`. An approved published
  download URL was absent from the input packet: the old 1.7.7 link was removed,
  and no replacement URL or agent build was invented.

## Build and deployment

Manual immutable path: exact published Git archive → isolated build on the
authorized build host → private registry → production pull by manifest digest.
No beta worker, image save/load delivery or production build was used.
The beta timer remains disabled/inactive and its service inactive.

| Artifact | Identifier |
|---|---|
| Git source archive SHA-256 | `74d3924612870a54ddeaf375b666f021b8db178e5b2675f7161e9113c048e442` |
| Backend registry manifest | `sha256:b0c4b0c11b93697ecda63a7b505ecc349d6b6624c43a3469dc54119c86105589` |
| Frontend registry manifest | `sha256:3fbed8c6104f8399e7486300c0a16e17fd96aeabe90ba4b7bcc11ddc404845d5` |
| Installed SPA index SHA-256 | `9dbe5fada28fe73f7c147679f6d7797ae68f7b02161fdee0e8ab5c08f00edaa7` |

OCI revision labels identify the source above. Only the MenuBuilder backend
was recreated. Hashed SPA assets were installed before an atomic index swap;
previous hashed assets remain during the rollback window. Nginx was not restarted.
Backend image scanning rejected private configuration, key material and test
files; frontend artifact scanning passed. Secrets were not put into artifacts.

MCP Ops was unavailable; approved noninteractive SSH was used. Final resource
preflight: production available RAM 2167 MiB, disk 52%, load 0.21; build host
available RAM 2850 MiB, disk 50%, load 0.00. Docker operations used sudo.

Rollback checkpoint was saved before the first deployment: backend images
`e0d17a09092e34b206eeb313b155186c419e55ee0419a684eb5ae2ce32e50090`
(production) and `fa37ec23d4c2a807b7209440af00a123bdecbb2a862a3e3e1cc1b3cd1e091345`
(isolated), compose selections, private MB env and complete previous SPA.
Restricted checkpoint: `/home/user1/.l4d-backups/18e-mb-01e1e58`.
Current deployment record: `/home/user1/.l4d-releases/mb-18e-4f72dc6/deployment.json`.
Rollback procedure is in `MenuBuilder/deploy/README.md`; a disruptive full
rollback rehearsal was not performed.

The existing PB credential was transferred opaquely on-host to the private
MenuBuilder consumer env with restrictive backup and atomic replacement.
No credential was printed, hashed, committed or rotated; PB was not recreated.
Missing/wrong PIN credentials returned 401; authorized absent-operation lookup
returned 404. This is consumer configuration, not a provider deployment.

## Verification evidence

| Check | Observed result and limit |
|---|---|
| Backend complete suite | 551 passed; Ruff/check-format/Pyright passed. 51 existing test warnings remain. Later frontend-only builds reused the identical verified backend layer. |
| Frontend | 63 passed; TypeScript/Vite build passed, including final source. Existing Ant Design vendor chunk warning (~1.4 MB uncompressed) remains. |
| Current-image phase contracts | 54 registration/reservation/payment/grace tests passed in the final verification image with network disabled. Registration replay/collision used mocks; no new registration email/tenant was created for this check. |
| Presence/identity | Existing 1000007 online, 1000006 offline; device-management mapping reused. Cross-tenant/missing ID regression checks passed. |
| PIN runtime | Existing 1000006 issue 200, same-operation replay 200/same PIN, GET 200 and pending display after reload. 1000007 consumed response hides PIN. Cross-tenant GET 404; superuser GET 403. No new certificate was installed. |
| PIN failure UI | Synthetic 503 then reload/retry preserved the UUID and showed pending PIN. This was a guarded browser mock, not a provider outage. |
| Platforms | Final production wizard offers Windows/Linux/ESP32. Guarded ESP32 browser submission carried `sys=esp32` and no Windows download action. Backend enum/replay/provider-tag tests passed; no physical Linux/ESP32 agent was installed. |
| Responsive | Terminal regression at 600/900/1440 px; identifiers no longer wrap into vertical text. Final licences/video checks at the same widths; after responsive layout settles, body width equals viewport. |
| Video | Real browser decoded 1920×1080; frames advanced from 3 to 1570 and playback time to 122.64 s. Stop 200. Session 508 closed with source hash; 128 seconds recorded in 3 portions. HTTP success alone was not used as video proof. |
| Console | Existing Windows test agent answered `ver`; normal disconnect. Session 509 closed with source hash. Existing terminal sys tag was set to its actual Windows platform through the accepted device API. |
| Profile/Hub | Final profile role5 GET 200 and form visible. Test superuser Hub registrations/terminals/sessions/usage/finance/payments/archives each 200; six tabs rendered. Tenant owner Hub access 403. |
| Mock payment replay | Existing mock payment 5 polled twice: 200/succeeded, same ledger transaction 9; balance unchanged at 1000 kopecks. No live YooKassa charge or new money created. |
| Monthly/grace on current image | Published rollback probe used real PostgreSQL and existing tenant 10000; one 10000-kopeck monthly posting, replay created none. Simulated grace 600 s: allowed before boundary, `entitlement_blocked` at boundary. |
| Stop/reconciliation in probe | One explicit mock stop, synthetic session closed with source hash; reconciliation matched, debit=credit=10000. Outer transaction rolled back despite service commits; balance and ledger/session/cycle/charge/reconciliation/audit counts unchanged. Not a real provider stop or durable posting. |
| Current durable reconciliation | UTC window starting 2026-09-28T00:00:00: tenant 1000 run 12 and tenant 10000 run 13 both matched, zero mismatches/difference. Includes fresh closed sessions; no money posted in this window. |
| Historical money reconciliation | Tenant 10000 full September run 8 matched, debit=credit=21000 kopecks. Tenant 1000 exception below is explicitly unresolved. |
| Archive | Hub archive status 200; accepted synthetic manifest consumer tests passed. Archive worker/purge remain disabled. No fresh off-host backup schedule or retention activation is claimed. |

120-minute video and earlier real grace-stop observations are historical accepted
17F baseline only; no new 120-minute or three-year wait is claimed. Current probe
tests the mechanism with a 10-minute simulated boundary and finishes in seconds.
Existing users are covered by unchanged classic navigation tests and actual
superuser login/Hub access; no new user credentials were generated.

## Effective phases and limits

Production final flags: umbrella, registration, billing, policy enforcement,
entitlement worker, metering-close worker and effective YooKassa **false**;
policy shadow **true**; terminal onboarding and financial core **true**;
free-quota/entitlement test allowlists empty. No live payment credentials added.
The isolated test backend was stopped after the historical reconciliation finding
and remains stopped. The final disposable probe did not start application workers
or enable global flags. The umbrella flag was never enabled, so its implicit
YooKassa behavior did not activate a live provider.

Phase records: registration/replay/collision are fresh network-isolated contract
tests plus accepted baseline; onboarding/PIN are bounded existing-tenant runtime
checks and guarded browser mocks; mock payment replay is existing-transaction only;
monthly/grace/stop are current-image PostgreSQL rollback checks; profile/Hub are
live authenticated read-only checks. Each uses the same saved rollback checkpoint.
This is a restricted release, not general commercial availability.

### Unresolved historical provenance finding

Full-September tenant 1000 reconciliation run 7 found five missing source hashes:
sessions 476–480. Debit and credit both 1000 kopecks, money difference zero;
other reconciliation categories empty. These old sessions have no durable cursor
or reconstructable source audit chain. This is **not** a previously accepted
exception and is not fixed by the new-window matched results. No hashes were
invented, old rows rewritten, or reconciliation invariant weakened. General
commercial activation remains blocked pending a separately reviewed resolution.

### Test resource incident and cleanup

An early browser mock URL missed a query string and created test terminal 1000008
under existing tenant 10000. It was disclosed, then removed through the normal
soft-delete API. It has no sessions/monthly charges; the free terminal remains
1000007. Its audit/provider records remain; physical deletion is not claimed.
This exposed the list visibility bug fixed in the release. Subsequent browser
mocks explicitly blocked unmatched mutations. No new tenant was created.

One pending renewal PIN for existing 1000006 remains available to the authorized
owner as requested. PIN values and browser credential state are excluded from
the report. Release checkpoint/build evidence are retained for audit/rollback;
the disposable financial container was removed. No live session was left by tests.

Initial unpublished build attempts exposed CRLF archive conversion and classic
Docker ignoring a Dockerfile-specific ignore file; both were corrected before
publication. Git archives now preserve raw bytes and the build script materializes
the tracked ignore policy in the isolated build context. These failures are not
counted as successful builds.

## Transfer

Output requested: `H-L4D-18E-MB-v1`, consumer `L4D-18F-DOCS` only. Report commit R
and a separate DETACHED_V1 candidate commit C are published for independent review.
Controller acceptance and any next-stage authorization remain separate actions.
