#pragma once
#include "root_manifest.h"
typedef struct SetupInstallBundle SetupInstallBundle;
/* Fixed filenames, local canonical path, held file/directory handles. Fresh
 * admission uses a current owner-signed offline setup catalog (stable=null,
 * no transitions). Recovery accepts only original journal85 root identity. */
bool setup_bundle_open(const wchar_t* directory,const char* version,const char* arch,
    const L4CatalogRelease* original,SetupInstallBundle** result);
void setup_bundle_free(SetupInstallBundle* bundle);
const SetupRootManifest* setup_bundle_root(const SetupInstallBundle* bundle);
bool setup_bundle_manifest(const SetupInstallBundle* bundle,const L4Layout* layout,SetupManifest** manifest);
const void* setup_bundle_descriptor(const SetupInstallBundle* bundle,DWORD* size);
const BYTE* setup_bundle_signature(const SetupInstallBundle* bundle,DWORD* size);
const wchar_t* setup_bundle_archive(const SetupInstallBundle* bundle);
/* Copies only held authenticated assets into an original locked operation's
 * private inputs and immutable versioned PF installer slot. Never SCM/Path. */
bool setup_bundle_stage(const SetupInstallBundle* bundle,L4Journal* journal,wchar_t host[MAX_PATH]);
bool setup_bundle_self(const SetupInstallBundle* bundle,const wchar_t* self);
#include "../../l4common/package_cache.h"
bool setup_bundle_cache(const SetupInstallBundle* bundle,const L4Layout* layout,L4CachedPackage** result);

/* Explicit local operator-only bundle: pinned signed metadata is unchanged;
 * self/stage EXE checks use cache-only local revocation policy. */
bool setup_bundle_open_local(const wchar_t* directory,const char* version,const char* arch,const L4CatalogRelease* original,SetupInstallBundle** bundle);
/* Fresh bootstrap opt-in; historical installed-source admission never assumes
 * new assets exist. Owner signature + fresh root + hash + Authenticode required. */
bool setup_bundle_prepare_bootstrap(SetupInstallBundle* bundle,const char* version);
bool setup_bundle_install_bootstrap(const SetupInstallBundle* bundle,L4Journal* journal);
bool setup_bundle_check_bootstrap(const SetupInstallBundle* bundle,L4Journal* journal);
