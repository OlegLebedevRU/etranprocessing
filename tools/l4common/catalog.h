#pragma once
#include "metadata.h"
#include "journal.h"

#define L4_CATALOG_MAX_RELEASES 24u
#define L4_CATALOG_MAX_TRANSITIONS 24u
typedef struct L4Catalog L4Catalog;
typedef struct {char version[64];BYTE manifest_sha256[32];bool revoked;} L4CatalogRelease;
typedef struct {unsigned count;L4CatalogRelease releases[L4_CATALOG_MAX_RELEASES];} L4CatalogRoute;
/* Signature before JSON; now is authoritative UTC Unix seconds, never a network
 * metadata time. Production entry always uses compiled owner key. No I/O here. */
bool l4_catalog_parse_trusted(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    ULONGLONG now,L4Catalog** result);
/* Low-level caller-owned trust input (fixture/integration boundary). Never fetch
 * these inputs from the downloaded catalog. Production uses the entry above. */
bool l4_catalog_parse_signed(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const BYTE* public_key,DWORD public_size,const char* expected_key_id,ULONGLONG now,L4Catalog** result);
void l4_catalog_free(L4Catalog* catalog);
bool l4_catalog_resolve(const L4Catalog* catalog,const char* requested,L4CatalogRelease* release);
bool l4_catalog_route(const L4Catalog* catalog,const char* current,const BYTE current_manifest[32],
    const char* requested,const char* arch,const char* profile,L4CatalogRoute* route);
/* Must complete before starting a new operation. Journal holds the common
 * deployment lock; global protected floor survives operation changes/restarts.
 * Old revision, same revision/different bytes, clock rollback, corrupt/torn floor
 * refuse. Does not authorize installation or replace saved operation recovery. */
bool l4_catalog_accept(L4Journal* journal,const L4Catalog* catalog,ULONGLONG now);
