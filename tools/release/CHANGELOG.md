# Release control changelog

## Unreleased

### Added

- Root Python 3.14 uv project/lockfile and `l4release` plan, prepare, release,
  read-only artifact verification and one-time metadata key creation commands.
- Declarative ordered build plan, native asset preflight, fresh x86/x64/universal
  PE checks, setup tests for both architectures, redacted logs and phase checkpoints.
- Workspace lock, bounded child commands, unchanged-input checks, signing resume
  and mandatory verification before immutable candidate publication.
- Recovered OpenH264 2.6.0.2502 headers/static libraries pinned by SHA-256;
  validated import command and mandatory existing l4capture test gate.

### Changed

- `tools/build_dist.cmd` delegates ordinary preparation to the unified pipeline.
- Automatic signing reads owner-approved `sw_sign.env`; manual signing is optional.
- Terminal gate/promotion placeholders fail explicitly until later stages implement
  transition evidence/catalog. No service, RPC or installer runtime change here.
