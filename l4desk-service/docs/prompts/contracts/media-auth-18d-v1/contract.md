# 18D media management credential contract for the MB consumer

contract_version: 1.0.0
verification_status: VERIFIED_DOCUMENTATION
runtime_verification: NOT_REPEATED
source_commit: c200d60485d32c805ae00530c57ad7a4b6b2b1ce

The l4media ingress management API declares the service-token header
`X-Media-Service-Token` in the exact exported OpenAPI. It also declares a
Bearer alternative. The media provider reads `L4MEDIA_SERVICE_TOKEN`; Janus
admin integration reads `JANUS_ADMIN_SECRET`. These are names only: this
contract contains no credential value or digest of a credential.

Required target for the coordinated 18D rotation:
- Media provider must have a new private, nonempty service credential and
  fail closed for missing/wrong credentials. No tracked public default may
  remain in source, Compose, example env or image.
- Janus admin secret must likewise come from private runtime configuration,
  with no tracked public default; verify actual Janus/ingress agreement without
  printing a value.
- MenuBuilder consumer must discover its actual environment key in its own
  scope, receive the same new service credential through the approved private
  owner channel, and prove provider/consumer equality as a boolean only.
- Inspect whether an isolated MenuBuilder test backend consumes the old
  credential; update and restart it only if the bounded inspection confirms
  it is an affected consumer. Do not copy production secrets to a test host.
- Preserve the existing API/header and supported Agent baseline. Perform
  unauthorized rejection and an authorized internal operation probe only
  after both owner steps are staged. No public management endpoint exposure.

This is a data-only contract and correction target. It does not assert that
the old running provider is safe, that the new media image is deployed, or
that MB has already rotated. Credential transfer and server configuration
remain private operational actions. Media and MB must keep separate scope
and report their own changes and sanitized evidence.
