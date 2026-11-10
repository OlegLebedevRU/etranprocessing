# JWT library migration, 2026-11-10

## Task intake
- Owner: MenuBuilder authentication domain; backend, internal signer and API-token issuance.
- Replace python-jose with PyJWT 2.15.1 (crypto extra, major version constrained below 3).
- Producer: private RS256 signer or optional external issuer; consumer: MenuBuilder auth and issuer-response validation. HS256 API tokens remain a separate audience/issuer domain with live database revocation and endpoint restrictions.
- Preserve keys, fixed algorithm allowlists, issuer/audience policy, tenant/session claims, integer org_id conversion, mock-only gates, replay/idempotency and refresh persistence. No DB migrations or frontend changes.
- User owns the external signer update; no external deployment or availability prerequisite. User accepts possible session logout. No key rotation or intentional session invalidation is needed.
- Use existing isolated python-maintenance worktree; original dirty checkout preserved. Base main includes operator acceptance PR153. No restricted legacy/native/SQL directories inspected.

## Implementation
- Replace imports and exception type in four application modules and existing JWT tests.
- Translate python-jose require_exp/require_iat/require_nbf to PyJWT require list, preserving mandatory issuer-response claim checks.
- Unverified decode remains confined to the existing explicit mock gate in application code.
- Lock removes python-jose, ecdsa, rsa, pyasn1 and unused six; no unrelated dependency upgrades.
- PyJWT also validates future iat and string sub/jti. Invalid legacy claims may require sign-in again; identity checks are not relaxed.
- Independent cryptography RS256 verification and signing exercise wire compatibility without requiring the external issuer's library or service. Negative tests cover missing claims, expiry, future nbf/iat, audience, issuer, claim types, unsigned/DER-HMAC substitutions and API-token domain boundaries.
- Reference: [PyJWT decode options and algorithm policy](https://pyjwt.readthedocs.io/en/stable/api.html).

## Verification
- Windows Python 3.14.10: initial auth/signer/tenant-switch suite 50 passed.
- Initial full backend run: 1224 passed, 25 skipped, one new test failed during fixture construction because PyJWT correctly rejects DER as an HMAC key. Fixture now constructs the attacker signature directly with stdlib HMAC; no application policy was weakened.
- Ruff check/format and Pyright app passed before the test-only fixture correction; final results recorded below.
- uv audit locked Linux/Python3.14: 57 packages, zero vulnerabilities, zero adverse statuses.
- [MCP Ops Readiness: UNAVAILABLE]; direct SSH available. Production available RAM1930MiB/root54%/load0.56, builder2593MiB/root74.9%/load0.04 before release.
- Final Windows backend suite: 1229 passed, 25 skipped (155s, exit0), including corrected attack fixtures. Targeted signer/API suite: 48 passed.
- Final Ruff check/format and Pyright app: passed (zero type errors). Existing test-only short mock-key and framework/mock warnings remain; production key policy is unchanged.
- Read-only task-file secret scan: 11 source/lock/context files, no private keys, credential URLs or recognized access tokens; diff whitespace check passed.
- Production build and release evidence: pending.

## Limits and handoff
- No real user credentials or production-secret-derived JWTs created; browser login/refresh acceptance requires operator verification.
- External serverless signer remains the user's responsibility; internal signer and external HTTP envelope contracts are unchanged.
- Production release must preserve the existing full Compose label chain, environment, mounts, key permissions and unrelated container IDs; paired backend/signer rollback uses the previous shared image digest.
