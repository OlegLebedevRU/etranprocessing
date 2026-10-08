#pragma once
#include <windows.h>
#include <stdbool.h>

#define L4_METADATA_MAX_BYTES 65535u
#define L4_METADATA_SIGNATURE_BYTES 384u
#define L4_METADATA_PUBLIC_BYTES 411u
/* Exact bytes + raw detached RSA3072/PKCS1v1.5/SHA256 signature. trusted_public
 * must come from the signed executable's owner-managed trust root, never network
 * metadata or the payload. Verification does not parse JSON, fetch or write files.
 * digest is zeroed on refusal. Caller owns immutable input buffers during call. */
bool l4_metadata_verify(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const BYTE* trusted_public,DWORD public_size,BYTE digest[32]);
/* Production entry: one compiled-in owner key; no parameter or network override. */
const char* l4_metadata_key_id(void);
bool l4_metadata_verify_trusted(const void* bytes,DWORD size,const BYTE* signature,
    DWORD signature_size,BYTE digest[32]);
