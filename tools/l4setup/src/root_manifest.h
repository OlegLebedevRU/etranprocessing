#pragma once
#include "manifest.h"
#include "../../l4common/catalog.h"

typedef struct SetupRootManifest SetupRootManifest;
typedef struct {char name[40];ULONGLONG size;BYTE sha256[32];} SetupRootAsset;
/* catalog_release is an authenticated catalog selection, not an RPC URL/digest.
 * Production authenticates exact owner signature AND catalog hash before JSON.
 * No network, journal, services or installation side effects. */
bool setup_root_parse_trusted(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const L4CatalogRelease* catalog_release,const char* arch,SetupRootManifest** result);
/* Caller-owned fixture/integration trust boundary. Cannot create a root accepted
 * by the production descriptor entry. Never take this key/key ID from a server. */
bool setup_root_parse_signed(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const BYTE* public_key,DWORD public_size,const char* key_id,
    const L4CatalogRelease* catalog_release,const char* arch,SetupRootManifest** result);
void setup_root_free(SetupRootManifest* root);
bool setup_root_matches(const SetupRootManifest* root,const L4CatalogRelease* release,const char* arch);
/* index0 descriptor, index1 detached descriptor signature, index2 archive. */
const SetupRootAsset* setup_root_asset(const SetupRootManifest* root,unsigned index);
const SetupRootAsset* setup_root_installer(const SetupRootManifest* root);
const BYTE* setup_root_publisher(const SetupRootManifest* root);
const BYTE* setup_root_identity(const SetupRootManifest* root);
/* Checks size/hash of descriptor AND its raw signature against authenticated root,
 * signature-before-JSON, same version/layout/arch/publisher/archive identity.
 * Result remains an ordinary protected ZIP admission plan, not an installed release. */
bool setup_root_descriptor_trusted(const SetupRootManifest* root,const void* bytes,DWORD size,
    const BYTE* signature,DWORD signature_size,const L4Layout* layout,SetupManifest** result);
bool setup_root_descriptor_signed(const SetupRootManifest* root,const void* bytes,DWORD size,
    const BYTE* signature,DWORD signature_size,const BYTE* public_key,DWORD public_size,
    const L4Layout* layout,SetupManifest** result);
