# Mosquitto diagnostics ACL — 2026-10-08

## Task intake
- Owner: tools setup/updater provisioning; supervisor ordinary idle reconciliation.
- Scope: Mosquitto diagnostic log and built-in broker configuration only.
- No live ACL/SCM changes, new MQTT clients, signing, publication or backend changes.
- Ordinary Users receive read only. Private control, operations, state and custom-template files remain private.

## Cause and changes
Installed logs/mosquitto parent grants Users read and SYSTEM/Admins management, but the actual mosquitto.log is protected SYSTEM-only. Upstream Mosquitto 2.1.2 src/logging.c opens it with mosquitto_fopen(..., restrict_read=true); libcommon/file_common.c uses an explicit Windows current-user-only ACL. Directory inheritance alone cannot fix creation/recreation.

Sources: https://github.com/eclipse-mosquitto/mosquitto/blob/v2.1.2/src/logging.c and https://github.com/eclipse-mosquitto/mosquitto/blob/v2.1.2/libcommon/file_common.c . Upstream inspection supports the mechanism; the current vendor EXE was not rebuilt or instrumented.

Installed svc_configure_mosquitto_log now reconciles only fixed mosquitto.log to the installer-owned parent inheritable policy, preserving actor grants and owner/content. Ancestors/file are held without delete sharing; reparse points and hardlinks refuse. Actual effective Users read and absence of write/delete/DAC/owner permissions are checked afterwards. Supervisor runs this only after the persisted active-window early return, so update drain remains quiet. Portable legacy helper policy is unchanged.

Built-in active/standby configurations contain local routing and SN, no credential material. Those files receive explicit Users read only; config/mosquitto parent and other files remain private. Arbitrary custom templates remain private. Runtime migration copies source file policy to replacement and backup; a stale candidate is removed before rendering so an earlier public file cannot expose subsequent custom content.

New l4_config_prepare_public_broker is fixed-path and fresh-file-only. Config record20 version2 distinguishes this new fresh public-read policy, requires absent original and exact mosquitto\\mosquitto.conf, and reconstructs the exact parent+read policy on apply/verify/rollback. Existing record version1 and existing-file update/restore behavior remain unchanged. The fresh renderer callsite is owned by the parallel fresh-install task; generic extras do not gain public access. Communication recovery still accepts existing-file version1 plans only; a future remote preparation captures the already public source into a normal version1 record.

## Checks
- x86/x64 actual Win32 log ACL test: 0 failures each. Restricted ordinary token can read, cannot write/delete/change ACL/owner; writer-only creation/recreation, repeat reconciliation, content preservation and hardlink refusal.
- Fixture writer-only ACE is the actual test process user, reflecting the production SYSTEM writer pattern. Raw SYSTEM-only reconciliation has NOT been exercised under SYSTEM in this task.
- x86/x64 journal/config: 125 checks, 0 failures each, including fixed public broker admission/apply/verify/rollback and effective read-only rights.
- x86/x64 route/config tests passed, including built-in public read, private parent, custom private and retained previous custom private.
- Scoped whitespace checks passed.
- Initial test failures were corrected: test token was closed before recreation assertions; source/target policy handles need FILE_READ_ATTRIBUTES. No production access gate was disabled to make tests pass.
- Full unified builds and integrated fresh tests are owned by root/fresh-install agent, not duplicated concurrently here.

## Remaining
- Deploy a new signed candidate and prove actual SYSTEM reconciliation of current protected log, ongoing broker creation/rotation, local desktop read/copy and FM download.
- Ordinary elevated administrator security-only CreateFile cannot necessarily open the protected SYSTEM-only file; reconciliation is designed for supervisor SYSTEM, not an admin repair bypass.
- Current installed 1.13.6 log/config ACLs have NOT changed.
- Some failed isolated ACL fixtures may remain in owner TEMP (L4LogAcl-*); no live resources/services/processes were created or stopped. Successful fixtures remove their own files.

## Live log correction and independent check
Root separately authorized/performed a fixed-file live correction (no SCM): mosquitto.log inherits parent DACL and preserves explicit SYSTEM full control. Current observed SDDL: O:BAG:SYD:AI(A;;FA;;;SY)(A;ID;FA;;;SY)(A;ID;FA;;;BA)(A;ID;0x1200a9;;;BU). Root reports actual read-only file-open succeeded.

Independent actual live AccessCheck under a restricted ordinary token (Administrators disabled, privileges disabled): read=1, write=0, delete=0, WRITE_DAC=0, WRITE_OWNER=0. No contents printed, no service/channel changes. The isolated checker used production setup_broker_render to generate expected standard active bytes, and compared SHA256 of actual configuration without printing its contents. SN773 rendered hash did not match current configuration; terminal number is not assumed to equal MQTT SN. No config read grant was applied. Further exact-binding classification is required.

Creation/rotation durability still requires the new signed supervisor release and actual SYSTEM/FM/desktop acceptance. The live fix does not prove rotation.

## Exact standard config live read grant
Actual SN was obtained read-only by root from the running localhost leo4proxy /_leo4/info: a4b0000773c82116d210826, ready/cert_ready. Production setup_broker_render with that SN and installed fixed ProgramFiles/ProgramData roots produced SHA256 DF6AD5B15364AD877C498D5733971722A399294B856A6665DD84F4548A5FFB82, EXACTLY matching the actual config. The complete config contents were never printed.

Root authorized a fixed-file-only Users Read grant after exact classification. Ancestors were inspected for reparse and held with native handles excluding delete sharing; the target was held with a read-only stream excluding delete sharing. Hash was checked before and after Set-Acl. Parent ACLs and other files were not changed; SCM/services/processes were untouched. Final config SDDL: O:BAG:SYD:AI(A;;FR;;;BU)(A;ID;FA;;;SY)(A;ID;FA;;;BA). Independent restricted ordinary token AccessCheck confirms read1/write0/delete0/DAC0/owner0. The private config/mosquitto parent remains private (no ordinary directory enumeration), fixed-file reads use normal traverse privilege. Custom files were not read or granted.

Root reports full l4superv build.cmd all succeeded after source changes. Runtime 1.13.6 has not been replaced by that build; creation/rotation acceptance is still pending.
