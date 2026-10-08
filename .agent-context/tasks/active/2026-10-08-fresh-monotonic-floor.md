# Fresh replacement keeps independent catalog floor, 2026-10-08

## Problem and boundary

Cold baseline retention initially moved catalog.floor away with actor state and
restored it only after fresh Setup started Con/Sup. That allowed a new signed
catalog to be admitted without the previous monotonic revision/hash/time floor.
Moving installation history into an unrelated configuration was not required.

Root now restores the exact retained floor and ACL before fresh installation,
while suite services remain absent and existing deployment.lock remains held.
The native fresh validator permits this one independent security metadata file;
all actor configuration/state/log entries remain refused.

## Source changes

- New common catalog_floor.h: read-only l4_catalog_floor_verify_existing(layout),
  absent allowed; never creates/repairs/truncates/resets.
- catalog_floor.c: shared existing chain decoder, reused by normal catalog accept
  and read-only validation. Compiled owner key ID, private owner/DACL, no reparse,
  hardlink or alternate stream, read-only no-WRITE/DELETE-sharing handle and full
  existing canonical header/hash-chain/revision/time/digest invariants.
- fresh_install.c data_empty allows only verified state/catalog.floor. Same name
  directory/reparse, every other state/config/log entry and existing update.state
  still refuse. Fresh setup does not adopt old actors, configs, updater pointer
  or application state.

## Checks

- Isolated production floor/fresh compilation /MT /W4 /WX PASS x86/x64.
- New actual protected-I/O floor test52/0 each architecture; absent does not
  create, valid bytes unchanged, wrong key/hash/torn tail and semantic chain drift
  refuse, hardlinks/ADS/writer-sharing/foreign ACE/directory refuse.
- Existing signed catalog/route/global floor regression271/0 each architecture,
  including normal revision/time/hash refusal and unchanged saved route behavior.
- Existing fresh fixture1155/0 each architecture. Actual directory enumeration
  exercises valid floor-only allowance and other files/directory refusal; floor
  crypto verifier is modeled there and separately tested with actual I/O above.
- No production ProgramData/service writes, helper changes, signing/publication.

Root owns permanent Setup gate links and retention scripts. New focused test
links test_catalog_floor.c, catalog_floor.c, metadata.c, layout.c, journal.c with
advapi32/bcrypt/ole32/shlwapi. Existing fresh fixture models the verifier and
therefore needs no added library/source dependency. Final full native gate and
real owner-controlled baseline replacement remain required.
