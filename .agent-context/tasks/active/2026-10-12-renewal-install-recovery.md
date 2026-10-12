# Certificate issuance versus installation recovery

## Task intake

Owner authorized implementation of escalation and retry of an already-issued
certificate, preserving RabbitMQ configuration and trust. PB owns issuance,
PIN and serial; MB owns tenant UI/queue orchestration; l4pin owns saved local
CSR/key/PKCS7 and installation. No schema migration or native release is needed.
Worktree starts at origin/main fcfe99c; original dirty checkout is preserved.

User-installed suite34 completed operation9c6ab688; failed later taskb3d273c6
used PIN328 while native pending belongs to issued327. Saved leaf serial matches
current DB serial. Do not clear pending, overwrite serial, replay PIN or install
on the live terminal from the agent. Owner triggers recovery in the UI.

## Contract

| Producer → consumer | Transport | Change / compatibility | Lifetime / result |
|---|---|---|---|
| Browser → MB | existing certificate-renewal POST | optional resume_installation=false; explicit original pin_id for true | tenant admin / readonly guard; no browser PIN |
| MB → PB | existing renewal-pins POST | same optional flag; deploy PB before MB | trusted service auth, tenant/terminal/SN/current history, expiry and subscription checks |
| MB → app1 → l4con → l4pin | existing RPC7011 | unchanged dt fields pin, pin_expires_at, ttl_sec; native installed34 supports cached response | original PIN expiry, queue TTL and protected120s Job retained |
| Terminal → PB → MB | existing verified certificate discovery | current serial observed after issuance is confirmation | no MQTT trigger or scheduler added |

PB locks the terminal, refuses a new renewal when its current issued renewal has
not been observed, and returns only the original unexpired PIN for an explicit
installation retry. No signer invocation, new PIN, payment permission consumption,
serial write, expiry extension or expansion of the15-minute response-fetch window.
If native cached response is missing, normal existing recovery restrictions apply;
the operation fails/escalates rather than creating another issuance. Expired PIN
requires intervention. There is no automatic server revocation or serial rollback.

MB identifies the current issuance even if a newer unused PIN exists. Failure
of a used PIN is installation_failed. Five minutes without verified new identity
or task expiry/cancellation yields installation_unconfirmed. Verified use wins
over a failed command. New orders are refused before payment and at PB authority;
retry queues the original PIN without a new billing permission. UI exposes the
failure code and disables unavailable recovery, preserving evidence and pending.
Future issuance audit is certificate_renewal_issued with stage issued and
installation unconfirmed; old history/audit is unchanged.

## Verification and delivery

Targeted Python and browser tests cover current issuance selection, used-PIN
failure, confirmation precedence, expired/wrong PIN refusal, no issuance/payment
effects, lost queue response and unchanged RPC fields. Local final validation:
PB311passed/1skipped, MB779passed/22skipped; Ruff check/format and Pyright pass
in both modified backends. Frontend build,93unit and6fake-REST browser tests pass.
Changed tracked-file secret scan has no findings.
Live owner recovery, certificate store install, identity hot rotation and RabbitMQ
use are not yet verified. Release order: PB → MB backend → frontend via builder.

## Delivered 2026-10-12

PR78 merged; source main b152e353d7c49ffce7d0448392ef1156f8eefef5.
All three explicit builder launches exited0 with normal full checks, immutable
publication/pull and runtime verification. Initial launcher typo
processing-backend was refused before deployment; corrected to processingbackend.

| Component | Verified digest |
|---|---|
| ProcessingBackend | 590c1551876b906655e4d6d2142b06144d22cbb467259662c0d2c63d346065e8 |
| MenuBuilder backend / paired JWT signer | 6e1317bd6d33377634172b5c32aeca9c88816062dc070f34f361657077919ebf |
| MenuBuilder frontend | 075080c440fa81702cb8bc93f2c3bc3834a44e3b4ace35707ae0ac62f62cc4b6 |

PB health, MB OpenAPI and signer health verified. Frontend served index matched
the artifact; nginx was not restarted. app1/rabbitmq/other unrelated container
IDs match the preflight snapshot. There is no native build/install requirement
for this server/UI change; RPC7011 remains byte-schema compatible with suite34.

Production read-only transactions executed the deployed PB/MB selection helpers:
device773/current issuance327/latest PIN328/observed new identity false/
original PIN has >120s remaining. Current serial remains49267C2D17CE9DF8871018E2D8C7A69CAFDF0A1F.
No PIN value, response blob or key was printed. No queue/issuance/store/serial
change, recovery execution or terminal service restart was performed by agent.
Owner should refresh UI and choose repeat installation for773. Original PIN
expires2026-10-12T19:16:05Z; after that automatic recovery is unavailable and
requires intervention. Live installation/hot rotation/RabbitMQ acceptance remain
not run. No ad hoc user tokens or synthetic authenticated endpoint checks used.
