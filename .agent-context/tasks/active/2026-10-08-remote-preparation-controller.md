# Remote preparation controller

## Task intake
- Goal: compose original7031 request92/host93 with one immutable route60 and all-hop package61/62 preparation before service changes.
- Scope: native common platform/host helpers and setup remote_prepare/update_metadata plus relevant native fixtures/build. No live SCM changes, signing/publication, IoT deployment or frozen helper edits.
- Owner: SYSTEM controller owns its original deployment journal and retained authenticated installed source; Registry owner signs catalog/transitions/packages.
- Producer → consumer: native OS facts + installed signed identity → signed exact-profile route → pinned prepared packages → future typed forward worker.
- Invariants: original operation UUID; no RPC URLs/platform aliases; latest resolves once; no reselection on retry; no package-ready claim authorizes stop; total preparation budget and cooperative cancellation; no second MQTT connection.
- Verification: exact platform classifier/runtime x86/x64; controller composition and package cancellation fixtures; unified native build.

## Platform contract
Production `l4_platform_current` reads RtlGetVersion from already loaded OS ntdll and GetNativeSystemInfo, without environment, registry ProductName or manifest-sensitive version fallback. Profile schema: `windows-nt-<major>.<minor>.<build>-<native arch>-client`. The signed transition's existing profile field must match exactly; catalog schema is unchanged. A label never proves compatibility. Bundle architecture remains a separate transition field: x86 under WOW64 reports the native x64 machine profile.

The current stand reports `windows-nt-10.0.19045-x64-client` from both x86/x64 executables. Client Win7SP1+, Win8/8.1 and NT10 versions can be identified; server/domain-controller, ARM64, pre-SP1 Win7 and unknown major-version facts refuse. This is platform identification, not evidence that any transition works on those systems. A matching owner-signed pair evidence remains mandatory; missing profile/edge refuses, never aliases to fixture-only windows-10-x64/windows-7-x86 names.

## Evidence / state
- Platform fixture x86/x64 `/MT /W4 /WX`, Win7 subsystem target: PASS. Tests cover exact build changes, native architecture, unsupported/server refusal and real runtime equality between selectors.
- Preparation composition is implemented: original92/93 → one pinned60 → all-hop61/62, with retained package pins, repeated source/host verification and cancellation/deadline checks. Main remote engine stays NULL until forward execution/watchdog/result delivery are actually connected.
- Unified setup build x86/x64/default passed. Focused checks on each architecture: preparation171/0, metadata1225/0, host104/0 and platform PASS. Metadata tests include cancellation/expiry after flushed62: return failure without erasing durable completion; retry can load it offline.
- Detailed implementation/evidence: [remote controller preparation](2026-10-08-remote-controller-preparation.md). Composition fixtures model source/SCM/transport/admission; actual live SYSTEM/Registry preparation was not run.
- No live services/installed files, marker, external APIs or Registry publication changed in this step.

## Remaining
- Typed forward apply and durable final installed-source authority; actual watchdog failure acceptance and reporter76 integration.
- Signed baseline/candidate and controlled terminal773 acceptance. Existing1.13.6 lacks the new controller/watch/probe producer; new source code is not installed by a successful development build.
