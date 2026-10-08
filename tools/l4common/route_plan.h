#pragma once
#include "catalog.h"
#define L4_RECORD_ROUTE_PLAN 60u
typedef struct L4RoutePlan L4RoutePlan;
/* One immutable route selection per original operation journal. Fresh signature,
 * catalog time/schema/BFS + global floor flush BEFORE journal append/flush.
 * Current version/digest are independently trusted installed-state inputs.
 * Saves exact catalog/signature and selection, not network URLs or ABI structs.
 * This is NOT a prepared package/service-switch/installation authorization. */
bool l4_route_save_trusted(L4Journal* journal,const void* catalog,DWORD size,const BYTE* signature,DWORD signature_size,
    ULONGLONG now,const char* current,const BYTE current_sha256[32],const char* requested,const char* arch,const char* profile,ULONGLONG* sequence);
/* Load an already acknowledged selection offline using its original admission
 * time. No new floor acceptance/fresh network catalog. Always compiled owner key. */
bool l4_route_load_trusted(L4Journal* journal,ULONGLONG sequence,L4RoutePlan** result);
/* Pure immutable record60 decoder for authenticated journal snapshots. Checks
 * original signed catalog/admission time and recomputes route; no disk/floor,
 * journal repair, fresh selection or live codec view is needed. */
bool l4_route_decode_trusted(const void* bytes,DWORD size,L4RoutePlan** result);
/* Caller-owned test/integration trust, never a downloaded key. */
bool l4_route_save_signed(L4Journal* journal,const void* catalog,DWORD size,const BYTE* signature,DWORD signature_size,
    const BYTE* public_key,DWORD public_size,const char* key_id,ULONGLONG now,const char* current,const BYTE current_sha256[32],
    const char* requested,const char* arch,const char* profile,ULONGLONG* sequence);
bool l4_route_load_signed(L4Journal* journal,ULONGLONG sequence,const BYTE* public_key,DWORD public_size,const char* key_id,L4RoutePlan** result);
bool l4_route_decode_signed(const void* bytes,DWORD size,const BYTE* public_key,DWORD public_size,const char* key_id,L4RoutePlan** result);
void l4_route_free(L4RoutePlan* plan);
bool l4_route_is_owner_trusted(const L4RoutePlan* plan);
const L4CatalogRoute* l4_route_steps(const L4RoutePlan* plan);
const char* l4_route_requested(const L4RoutePlan* plan);
const char* l4_route_arch(const L4RoutePlan* plan);
const char* l4_route_profile(const L4RoutePlan* plan);
ULONGLONG l4_route_revision(const L4RoutePlan* plan);
ULONGLONG l4_route_admitted_at(const L4RoutePlan* plan);
const BYTE* l4_route_catalog_sha256(const L4RoutePlan* plan);
/* Pins source identity and each traversed evidence, including revoked source. */
bool l4_route_source(const L4RoutePlan* plan,L4CatalogRelease* source);
const BYTE* l4_route_evidence(const L4RoutePlan* plan,unsigned step);
