# FM production release — 2026-10-05

## Intake
- User explicitly authorized production deploy and preparation of l4tools signing script.
- Standard builder→registry→production flow; separate IoT master; only PB/MB/frontend/app1 and FM ingress.
- MCP Ops unavailable; SSH fallback. Initial production available RAM2076MiB/root46%/load0.21.
- S3 profile supplied by user; values never printed/committed. User approved C:\l4tools\fm.
- l4tools1.12.0 unsigned incremental packet, l4con1.11.0; native publication waits for operator signing.

## Preflight
- Origin main d871997 / IoT master8c2be80; GitHub credentials and SSH connectivity verified.
- Existing FM temp bucket discovered through allowed bucket inventory; profile example URL is an alias, not bucket name.
- Enabled versioning on FM-only bucket; preserved existing public access block and CORS/lifecycle,
  added Classic production origins and noncurrent FM-prefix cleanup1day.
- Synthetic local S3 probe passed checksum rejection, zero-byte, HEAD SHA256/VersionId, pinned GET after latePUT;
  all3 created versions removed. This is provider integration, not terminal E2E.
- l4con x86/x64 tests/build and setup x86/x64 build passed;130 embedded files matched staging.
- Signing packet sealed in ignored tools/dist/fm-signing-input.json. Script prepares and signs incrementally
  through existing Complete-SignedRelease.ps1; no private keys/PFX copied into repository.

## Release evidence
Pending commits/digests/migration/runtime verification.
