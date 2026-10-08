#pragma once
#include "layout.h"

typedef struct {
    const wchar_t* component;
    const wchar_t* file;
    ULONGLONG size;
    BYTE sha256[32];
} L4ReleaseFile;

/* Caller supplies authenticated metadata, never hashes learned from the payload.
 * The archive and expanded payload must already be in protected preparation storage.
 * Only listed files are copied; config/data separation is the caller's responsibility.
 * Preparation does not replace an existing version or change SCM/runtime ACLs. */
bool l4_release_publish(const L4Layout* layout, const wchar_t* archive,
                        const BYTE archive_sha256[32], const wchar_t* expanded,
                        const L4ReleaseFile* files, unsigned count);
/* Full file inventory, hashes, hardlink/reparse/owner/protected ACL checks.
 * Unlisted files/directories or a missing file are a failure. */
bool l4_release_verify(const L4Layout* layout, const L4ReleaseFile* files, unsigned count);
/* A held, checked EXE for a stopped-service switch; caller owns file handle.
 * It pins all ancestor directories through the returned opaque fence. */
typedef struct L4ReleaseFence L4ReleaseFence;
bool l4_release_pin(const L4Layout* layout, const L4ReleaseFile* file,
                    L4ReleaseFence** fence, wchar_t path[MAX_PATH]);
void l4_release_unpin(L4ReleaseFence* fence);

/* Hashes the held archive BEFORE parsing, extracts only the authenticated inventory
 * into unique protected staging, then publishes through the same file engine.
 * ZIP names cannot select arbitrary filesystem destinations. */
bool l4_release_unpack_publish(const L4Layout* layout, const wchar_t* archive,
                               const BYTE archive_sha256[32], const L4ReleaseFile* files, unsigned count);

/* Bootstrap only: copy the verified l4launch template to a single PF/bin leaf.
 * Existing bytes are verified/reused; no overwrite. Caller journals the intent. */
bool l4_release_copy_launcher(const L4Layout* layout,const L4ReleaseFile* file,const wchar_t* leaf);

/* Extra mandatory admission on each hash-verified, locked file before publication.
 * Caller supplies authenticated inventory and owns trust policy. File/ancestors
 * remain pinned throughout callback; callback must not close or retain the handle.
 * Reuse of an existing version repeats admission; failures never publish/overwrite.
 * Historical hash-only helpers are foundation APIs, not signed install admission. */
typedef bool (*L4ReleaseAdmission)(HANDLE file,const wchar_t* path,void* context);
bool l4_release_unpack_publish_checked(const L4Layout* layout,const wchar_t* archive,
    const BYTE archive_sha256[32],const L4ReleaseFile* files,unsigned count,
    L4ReleaseAdmission admit,void* context);
bool l4_release_verify_checked(const L4Layout* layout,const L4ReleaseFile* files,
    unsigned count,L4ReleaseAdmission admit,void* context);
