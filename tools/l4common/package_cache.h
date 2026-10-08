#pragma once
#include "layout.h"
#include "registry_http.h"
typedef struct L4CachedPackage L4CachedPackage;
/* Authenticated size/hash are supplied by the signed root, never HTTP headers.
 * Unique CREATE_NEW private file; incomplete downloads are never returned.
 * Success holds file/ancestors against replacement until close. No services. */
bool l4_package_download(const L4Layout* layout,WORD port,const char* version,
    const char* name,ULONGLONG size,const BYTE sha256[32],DWORD timeout,
    L4CachedPackage** result);
/* Persist only this canonical leaf in a prepared record, never an RPC
 * path. Reopen rechecks size/hash/security; it is not signed package admission. */
bool l4_package_reopen(const L4Layout* layout,const wchar_t* leaf,
    ULONGLONG size,const BYTE sha256[32],L4CachedPackage** result);
const wchar_t* l4_package_path(const L4CachedPackage* package);
const wchar_t* l4_package_leaf(const L4CachedPackage* package);
void l4_package_close(L4CachedPackage* package);

/* Offline import from an already held authenticated bundle handle. Unique private
 * cache leaf, bounded exact copy/hash, then immutable read-only held file. */
bool l4_package_import(const L4Layout* layout,HANDLE source,ULONGLONG size,
    const BYTE sha256[32],L4CachedPackage** result);
