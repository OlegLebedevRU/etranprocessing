# Installed source: physical-console ACL actor

## Intake / scope
Root identified missing actors.desktop in authenticated current source. Source captures only four suite service tokens while unchanged l4_access_verify requires all five actors. Scope: shared approved adaptive physical-console selector, thin FM wrapper, installed_source capture/recheck/close and meaningful native modeled tests. No ACL grants/matrix changes, arbitrary session, fabricated OS/logon token, installed service or MQTT changes.

## Change
- Moved previously approved fm_desktop_token selection body unchanged into common physical_console_token.h (only function/macro identifiers renamed); FM wrapper calls it. Prefer actual verified limited same-user token; unlinked Default standard/UAC-off admin keeps real OS rights. Full/Default linked errors and unsafe linked identity/elevation refuse; no SYSTEM/service or alternate-session fallback.
- Read-only retained-match re-queries only that selector, compares retained SID/session/logon AuthenticationId/impersonation type against the saved epoch and freshly selected token, closes only the new query handle. A console switch, new logon, foreign identity or WTS error refuses; retained token is never replaced.
- Source captures its fifth owned actor and stores physical session/authentication after four signed service captures. Every source_verify repeats the physical identity before inventory/SCM checks, covering prepare/pre-handoff repeats while source still owns its journal. Source_close closes all five actors. Source is never rechecked after journal transfer.
- Existing access.c remains unchanged. Its private_sid permits actual enabled+OWNER Administrators group without deny-only, and matrix expressly recognizes privileged recovery operator. Limited/nonadmin physical tokens retain private-receipt and parent-write denials. No rights are inferred or granted by selector.
- WTS SDK linkage is carried by existing/new header pragma; no new C source or frozen helper dependency.

## Validation
- Adaptive selection/retained-match fixture32checks/0failures x86/x64: original20 selection cases plus actual selection model for Default user/admin, limited pair, retained SID/session/type/auth drift, new logon, console switch, query errors and absent token. WinAPI calls modeled; no real token fabricated or changed.
- Installed source receipt guards64/0 x86/x64: real isolated temporary journal/ACL files and ordinary source token refusal; no production source/SCM admission claim.
- Production installed_source.c compiles /MT /W4 /WX x86/x64. Final Setup build remains root responsibility after this source change.
- Conall after shared-header extraction PASS x86/x64/default; log tools/l4con/obj/physical-console-unified-build.log. Added the token fixture as a permanent Conall label after that build; its standalone runner tests/test_fm_user.cmd PASS32/0 botharch on final header source. No product code changed after the unified build.

## Pending
Actual signed fresh baseline/source preflight remains necessary to prove all five actual actor tokens and ACL probes on773. These modeled token fixtures are not live desktop/SCM acceptance. No capability gate opened.
