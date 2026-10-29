# XP XEnroll CSR compatibility — source only

Owner requested a separate unsigned XP x86 l4pin using legacy CSP/XEnroll while
preserving modern SN and certificate fields. No XP host is available by agreement.
Client lives in l4tools branch `feat/l4pin-xp`; no native suite changes here.

PB owns PIN and certificate issuance. CHECK without v already returns Microsoft
Enhanced Cryptographic Provider v1.0; v26 returns CNG. SETUP accepts modern CN=SN
as well as historical cpserial. No schema, PIN ownership or DN changes needed.

Found with cryptography50: a mathematically valid XEnroll RSA/SHA1 CSR fails the
generic is_signature_valid check. `services/csr_signature.py` now verifies this
specific algorithm explicitly using RSA PKCS1v15 over the exact CSR signed bytes.
Router and local CA use it. Other algorithms retain generic validation. Invalid
signatures still refuse. This is request proof of possession, not permission to
issue SHA1 certificates; CA output remains SHA256, with modern subject/profile.

RSA2048 remains the local CA minimum. Owner mentioned nginx allows RSA1024, but
that does not override this separate issuance policy. No policy relaxation made.

Validation:76 focused certificate-contract/local-CA tests passed, including
CHECK provider split, valid/invalid SHA1 CSR, unchanged modern path, local issuer
RSA2048 success and RSA1024 refusal. Ruff check/format and affected application
Pyright passed. Whole test-file Pyright has two existing Settings(_env_file)
errors; no claim of a clean whole-repository typecheck. Tests use isolated keys.

No production deployment, live PIN use or live certificate installation occurred.
Deploy PB changes before expecting the new client SHA1 CSR flow to work against
cryptography50. XP TLS/SHA256 validation still needs operator acceptance; no
certificate-verification bypass or HTTP downgrade is provided in the client.
