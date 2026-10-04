# Resolving/network reliability hardening after tools 1.10.1

- Owner: native terminal transport (`leo4proxy`) and installer diagnostics (`l4setup`).
- Scope: authorized tools fixes, deterministic failure matrix, native x86/x64 builds, documentation and signed release publication.
- Baseline: `9e529c5`; production 1.10.1 ready/0 and working video reported by operator. This does not validate failure recovery.
- Contracts: producer policy unchanged; consumer candidate order SRV → verified SRV LKG → fresh policy → defaults/CLI → permitted IP fallback. Stale policy IP is recovery-only. SRV `.` disables a channel. SN isolates cache.
- Security: embedded CA, logical TLS name, time/EKU and admission gates remain mandatory even when connecting to IP. No MQTT application traffic from TLS diagnostics.
- Verification: bounded resolver/connect/handshake/I/O, independent diagnostic channels, launch failures, smoke-only absence of mutations; native tests and builds on both architectures.
- No production deployment or installed-client mutation is required for these local checks. Signing remains an operator step under the release runbook.
- Status: completed: signed 1.10.2 published from clean main d43e4d5; installed-client Upgrade acceptance remains open.

## Delta / contracts

- Proxy 1.8.1: bounded SRV/A/TCP/TLS, multiple IPv4, source deduplication,
  reserved IP fallback budget, completed DNS cache eviction, independent probes,
  absolute policy I/O and complete HTTP framing.
- Setup 1.10.2: read-only smoke-only, real TLS network evidence, unavailable
  diagnostics fail instead of ready, SCM authoritative arguments, bounded cold
  transport retries, required-service checks, same-version network Repair and
  SCM prefill preserving other manual channels.
- SCM binary path comparison canonicalizes separator/trailing-path variants;
  generated bin guides keep working repository links after component builds.
- Producer policy/media unchanged. Diagnostics JSON v1 is additive (`elapsed_ms`,
  `attempts`); existing channel/verdict fields remain compatible.
- Security invariants unchanged. Native `/MT`, x86/x64/default builds maintained.

## Evidence

- Proxy `build.cmd all`: x86/x64/default + stabilization/policy/endpoint suites, exit 0.
  Routing matrix 2304 per architecture; 5 saturated fallback budgets; DNS cache
  eviction/24-worker saturation/numeric bypass; diagnostics 20; deadline fixtures 12;
  actual loopback stalled handshake/credential retirement passed.
- Setup suites x86/x64: local HTTP 9, pipeline 44, pipe/launch/exit race 7,
  certificate phase, SCM quoting/network extraction, startup and desktop wait.
  Final build gate is rerun after the last GUI/Repair correction.
- Implementation gates initially rejected the former test expecting mutating
  options together with smoke-only and a missing InetPton declaration in the
  new endpoint fixture. Both corrected; final gates pass, not waived.
- Actual smoke-only with a forward-slash destination first returned degraded/12
  due to literal SCM path comparison; installed_version stayed 1.10.1. Added
  canonical path comparison and a regression before repeating runtime acceptance.
- Runtime smoke-only repeated with normalized forward-slash destination and
  explicit process wait: ready/0; MQTT/HTTPS/RTP valid, stream skipped;
  all four service PIDs/SCM paths/statuses unchanged, installed_version remained
  1.10.1. Only diagnostic log/summary were written. This run used installed
  proxy 1.8.0; fresh proxy 1.8.1 TLS/bootstrap evidence is recorded separately.
- Fresh x86 proxy, read-only upstream TLS: MQTT/SRV, HTTPS/policy and RTP/SRV
  valid, each elapsed 172 ms; disabled stream skipped; exit 0.
- Fresh x86 proxy bootstrap policy GET via provisioned IP: policy valid,
  strict=true, media/HTTPS allowed; exit 0. No cache writes/application traffic.
- Packaging gate: both payloads match staging (61 files per architecture).
  Comparing archived signed 1.10.1 payloads: 16 unrelated EXEs byte-identical
  with valid RFC3161 timestamped signatures; only 2 proxy EXEs changed.
- Final unsigned installer: PE 1.10.2, 30,405,632 bytes, SHA256
  `6c510d0dd2cff9b2c8e0e0acf3a988f63a557558cb6eae765baa6d84e23a9954`.
  Manifest explicitly signed=false/dirty=true; no publication attempted.
  Setup/proxy x86 and x64 PE machine fields verified; universal proxy matches x86.
- Read-only task-file secret scan: 50 source/documentation files, no findings.
  Whitespace and source/generated Markdown link checks pass after bin-guide correction.
- Published setup 1.10.1 retained under ignored runtime-backup; SHA256
  `092d8e338a0d0fa30bc7578816fc6d21370b8592b49e90cb3f294815e4da343f`.

## Publication / remaining runtime gates

- Operator signing verified: all 18 staged EXEs and universal setup Valid with RFC3161 timestamps; both payloads matched all 122 staging files. Signed proxy binaries synchronized into tracked x86/x64/default outputs. No components rebuilt after signing.
- PR18 merged; clean main source checkpoint `d43e4d51f842585d19871260e82832f1b3a92f5f`. Manifest refresh did not change setup bytes. Strict publisher passed without allow-dirty.
- Published 1.10.2 on 2026-10-04 at 16:29 UTC. All three complete HTTPS GET downloads matched size/SHA256. [Publication record](../../../artifacts/l4tools/1.10.2.json).
- Signed setup: 29,705,272 bytes; SHA256 `6270c908f0275dbb12591f0b377f594a1e959cbe325dc14b1ef467a35e7e4ff1`.
- Publication-status documentation edits occur after signing; embedded README snapshots are retained unchanged. Immutable release is not repacked for these text changes.
- Installed 773 remains 1.10.1. Upgrade to 1.10.2, Win7, clean PIN enrollment, real GUI clicks/cancel, full outage/video matrix and application-stream resilience are not established by these native fixtures.
- No services stopped, certificate stores changed or installed tools replaced. Loopback fixture sockets/temporary KSP keys cleaned by tests. Release staging and previous signed backup retained for release recovery.
- Backend/frontend checks N/A: no Python/shared/frontend source changes.
- Detailed timing/reliability limits: [matrix](../../../docs/term_net-leo4proxy-resolving-reliability-matrix.md).
