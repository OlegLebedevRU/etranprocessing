#pragma once
#include "installed_source.h"
#include "update_metadata.h"
#include "../../l4common/platform_profile.h"
typedef struct SetupAcceptancePin SetupAcceptancePin;
typedef struct {
 char operation[37],source_version[32],target_version[32],arch[8],profile[L4_PLATFORM_PROFILE_SIZE];
 BYTE source_root[32],target_root[32],catalog_sha256[32],intent_sha256[32],authorization_sha256[32];
 ULONGLONG created_utc,expires_utc,catalog_revision;
 bool force_rollback;unsigned configured_terminal,configured_tenant;
} SetupAcceptanceFacts;
/* Local operator intent only. Fixed protected original-operation files; no RPC,
 * environment, caller PASS, URLs, alternate owner key or floor reset. Absent is
 * allowed only after verified parent ancestry; partial/unsafe files refuse.
 * Pins borrow original journal until close, no use after worker transfer. */
bool setup_acceptance_open_optional(L4Journal* journal,SetupAcceptancePin** result);
bool setup_acceptance_bind(SetupAcceptancePin* pin,L4Journal* journal,SetupInstalledSource* source);
bool setup_acceptance_bind_plan(SetupAcceptancePin* pin,L4Journal* journal,const SetupOperationPlan* operation);
bool setup_acceptance_verify(SetupAcceptancePin* pin,L4Journal* journal);
/* Repeat full private-purpose admission plus enough TTL for fixed outer wait,
 * before creating92; later force/export check expiry without renewing it. */
bool setup_acceptance_prelaunch(SetupAcceptancePin* pin,L4Journal* journal);
/* Read-only completed-export adapter. Caller holds existing exclusive deployment
 * lock plus immutable fixed reader. No writable journal/codec view is fabricated. */
bool setup_acceptance_open_snapshot(const L4JournalReader* reader,const L4Layout* roots,
 const SetupOperationPlan* operation,SetupAcceptancePin** result);
bool setup_acceptance_verify_snapshot(SetupAcceptancePin* pin,const L4JournalReader* reader,
 const L4Layout* roots,const SetupOperationPlan* operation);
const SetupAcceptanceFacts* setup_acceptance_facts(const SetupAcceptancePin* pin);
void setup_acceptance_close(SetupAcceptancePin* pin);
/* After actual93 only; same strict signed60/catalog floor as ordinary remote.
 * Same controller prepares61/62/64 afterwards, no separate preparation engine. */
bool setup_acceptance_route(SetupAcceptancePin* pin,L4Journal* journal,SetupInstalledSource* source);
/* At one fixed executor checkpoint before settlement. No local file means no
 * forced failure. A present malformed/stale/mismatched purpose always refuses.
 * Does not itself mutate, roll back or confer stop permission. */
bool setup_acceptance_forcepoint(L4Journal* journal,const SetupOperationPlan* operation,bool* forced);
/* Elevated physical-console staging, before92. Local caller authenticates its
 * installed setup image separately; protected fixed intent/catalog files only. */
bool setup_acceptance_stage(L4Journal* journal,const char* source,const char* target,const char* arch,
 const wchar_t* catalog_path,const wchar_t* signature_path,
 const wchar_t* authorization_path,const wchar_t* authorization_signature_path);
