#pragma once
#include "../../l4common/release.h"

/* All inventory/archive hashes and publisher leaf certificate SHA256 MUST come
 * from authenticated metadata, not the downloaded payload. No trust discovery,
 * network download, SCM changes, installation entry or stable admission here. */
bool setup_release_prepare(const L4Layout* layout,const wchar_t* archive,
    const BYTE archive_sha256[32],const L4ReleaseFile* files,unsigned count,
    const BYTE publisher_sha256[32]);
bool setup_release_verify(const L4Layout* layout,const L4ReleaseFile* files,
    unsigned count,const BYTE publisher_sha256[32]);
/* Caller holds the hash-verified EXE and ancestors; publisher comes from the
 * production owner-trusted release root. Chain/revocation/timestamp mandatory. */
bool setup_signed_executable(HANDLE file,const wchar_t* path,const BYTE publisher_sha256[32]);

/* Explicit per-call local operator policy; existing APIs always remain strict.
 * Local cache-only verification permits only unavailable revocation evidence,
 * never known revocation, invalid signature/chain/time/publisher/timestamp. */
typedef enum {SETUP_ADMISSION_STRICT_REMOTE=0,SETUP_ADMISSION_LOCAL_OFFLINE=1} SetupAdmissionPolicy;
bool setup_signed_executable_policy(HANDLE file,const wchar_t* path,const BYTE publisher_sha256[32],SetupAdmissionPolicy policy);
bool setup_release_prepare_policy(const L4Layout* layout,const wchar_t* archive,const BYTE archive_sha256[32],const L4ReleaseFile* files,unsigned count,const BYTE publisher_sha256[32],SetupAdmissionPolicy policy);
bool setup_release_verify_policy(const L4Layout* layout,const L4ReleaseFile* files,unsigned count,const BYTE publisher_sha256[32],SetupAdmissionPolicy policy);
