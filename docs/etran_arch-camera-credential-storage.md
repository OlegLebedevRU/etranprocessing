# Camera credential storage v1

TASK-35 intake: PB owns verified public recipients and additive Alembic034
(parent033); MB owns camera vault/receipts. Shared ORM is declarative only.
Flow: authorized MB writer → encrypted DB receipt → frozen sealed envelope →
IoT166 → terminal; confirmation is correlated RES+gauge84. No UI/router/IoT
dispatch/firmware/GPIO/runtime deployment changes are included here.

## PKI API v1

GET `/api/certificates/terminals/{device_id}/credential-recipient?org_id=...`
uses existing service authentication, additionally requiring a configured key.
Returns exactly sn, device_id, cert_serial, recipient_cert_sha256 (SHA256 DER),
certificate_pem, not_valid_after; Cache-Control:no-store.
Missing/unsupported409, disabled403, unknown tenant/device404, missing auth401,
unconfigured service key503. One leaf PEM ≤8192 bytes, RSA2048/3072/4096,
current pinned CA/validity, CN=current SN, serial=current Terminal.cert_serial.
PB stores Terminal.credential_recipient_pem only after verified TLS possession
and exact current identity. Legacy Authorization/auto-bind/gauge headers cannot
populate it. Legacy authentication behavior is unchanged.

Changing current serial immediately invalidates old PEM. Terminal/org disable
denies lookup. Current PB has **no CRL/OCSP or independent CA revocation registry**;
do not claim such verification. Local revocation requires terminal disable or
authoritative serial replacement. External revocation, if required, is a rollout
blocker. Possession relies on existing private ingress overwriting TLS headers;
untrusted direct access to PB must remain prohibited. No new ingress/key mounts.

## At-rest namespace v1

MB owns `camera_credential_profiles` and `camera_credential_receipts`.
Scope: tenant_id, terminal_id, device_id, sn, camera_id. Unique terminal/camera
aggregate serializes writers by locking terminal then profile. Receipt UUID is
global; conflicting scope/content409 without readback. Profiles keep independent
active/pending operation UUIDs and monotonically allocated version. New pending
supersedes old pending, never active. Helpers flush, never commit: caller must
commit domain state together and rollback any helper exception.

Random32-byte DEK per receipt; AES256-GCM payload with12-byte nonce and16-byte
tag. DEK wrapped by AES256-GCM under stable server KEK with separate nonce.
`CAMERA_CREDENTIAL_KEK_B64`: SecretStr canonical base64 of32 random bytes;
`CAMERA_CREDENTIAL_KEK_ID`: stable non-secret identifier. No default KEK.
KEK is private runtime env/secret, separate from DB/Git/TLS private keys.
AAD: sorted compact ASCII JSON containing namespace=camera-vault/1, tenant_id,
terminal_id, device_id, sn, camera_id, profile_id, operation_id, version, kind,
expires_at_ms, key_id, algorithm. Add purpose=data or purpose=wrap respectively.
Swapping tenant/SN/record/version/deadline/key fails authentication.
Encrypted payload stores exact intent and original sealed envelope. Same UUID,
intent and deadline returns original receipt; changed content409. No public
password hash/fingerprint. Missing/wrong KEK/tag is fail-closed and redacted.
Completed/expired replay returns receipt metadata with delivery_allowed=false and
no envelope; original_envelope explicitly rejects unavailable delivery. Root must
honor delivery_allowed, including clear operations whose envelope is always None.

Transport remains frozen siplite-camera/1.0.0, package SHA256
e9a090949bf87c52b7b1de0c09d973da96b041d2c08b93c6c9c2abdf020fb23f:
RSA-OAEP-SHA256/MGF1-SHA256, label siplite-camera-credentials/v1, random
AES256-GCM, original HEADER/AAD and64/128 UTF8-byte credential limits.
Envelope deadline ≤60s and caller task deadline. Replay cannot extend deadline;
current recipient serial/fingerprint must still match. Rotated/expired envelopes
are never resent. New manual UUID may recover active credentials internally
under unchanged KEK and seal to new leaf. No public password GET.
Internal redelivery may also recover the current pending or latest failed intent
by explicit source UUID. Cleared/superseded historical intents are not eligible.
Encrypted historical receipts are retained for dedup; clear removes active access,
not encrypted audit history. Retention/KEK rotation is separate maintenance.

## Completion boundary

Trusted coordinator supplies independently validated scoped RES and gauge84 UUIDs,
success and custom/stock outcome. Both must match current pending operation and
unexpired receipt. Late, stale, failed, cancelled or expired results cannot replace
active. Clear has its own durable pending receipt; only both confirmations of
stock credentials clear active. Caller enforces user tenant/write permissions,
validates actual wire outcomes and dispatches; helpers perform no MQTT/RPC.
Python immutable bytes/strings and crypto buffers prevent guaranteed zeroization.
Secret-bearing repr is redacted. Do not log requests/payloads/envelopes.

## Coordinator deployment and rollback

1. Disable new camera writers; back up DB and KEK privately.
2. Roll MB bridge commit9c693cb73ba8a4cfea28fd7555f549f897faeef4 first: old ORM,
   startup accepts033/034. Then apply reviewed PB image migration033→034 before
   new readers/writers. No backfill. Final MB requires034 and checks vault objects;
   set REQUIRED_ALEMBIC_REVISION=034 in its runtime overlay (config.py unchanged).
3. Roll compatible PB collector/recipient reader; verify tenant/auth denials.
4. Provision stable CAMERA_CREDENTIAL_KEK_B64 and ID privately to MB runtime.
   Never put production key into arguments, Git, image/build context or evidence.
5. Roll compatible MB, preserve operator Compose overrides; verify discovery,
   rotation and outcome integration before enabling writer. Unit tests are not E2E.

Rollback: disable writers/dispatch, roll compatible app images back but retain034
schema, encrypted data and KEK. Downgrade drops vault irreversibly; do not run on
populated tables without explicit reviewed backup/restore. Lost KEK cannot be
recovered by certificate renewal. KEK rotation requires separate reviewed rewrap
with old keys retained until completion; it is not TLS certificate rotation.
