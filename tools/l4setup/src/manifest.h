#pragma once
#include "admission.h"

typedef struct SetupManifest SetupManifest;
/* Digest and publisher identity are authenticated caller inputs, never learned
 * from this document/archive. Pure bounded parse; no file/network/service writes. */
bool setup_manifest_parse(const void* bytes,DWORD size,const BYTE descriptor_sha256[32],
    const BYTE publisher_sha256[32],const L4Layout* layout,const char* arch,SetupManifest** result);
void setup_manifest_free(SetupManifest* manifest);
const BYTE* setup_manifest_archive_sha256(const SetupManifest* manifest);
/* Signature authenticates exact document bytes before any JSON/path processing.
 * Trust-root bytes and expected publisher are independently trusted caller inputs. */
bool setup_manifest_parse_signed(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const BYTE* trusted_public,DWORD public_size,const BYTE publisher_sha256[32],
    const L4Layout* layout,const char* arch,SetupManifest** result);
/* Production trust root is compiled in. Expected publisher is authenticated by
 * the signed root release metadata, a separate boundary still required. */
bool setup_manifest_parse_trusted(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const BYTE publisher_sha256[32],const L4Layout* layout,const char* arch,SetupManifest** result);
const L4ReleaseFile* setup_manifest_files(const SetupManifest* manifest,unsigned* count);
const L4Layout* setup_manifest_layout(const SetupManifest* manifest);
bool setup_manifest_prepare(const SetupManifest* manifest,const wchar_t* archive);
/* Read-only full inventory/Authenticode recheck for an already prepared release. */
bool setup_manifest_verify(const SetupManifest* manifest);
/* Read-only plan. Templates stay immutable in PF; applying them to ProgramData
 * requires the existing journaled config transaction, never an unconditional copy. */
bool setup_manifest_config(const SetupManifest* manifest,unsigned index,
    wchar_t source[MAX_PATH],wchar_t destination[MAX_PATH]);

bool setup_manifest_prepare_policy(const SetupManifest* manifest,const wchar_t* archive,SetupAdmissionPolicy policy);
bool setup_manifest_verify_policy(const SetupManifest* manifest,SetupAdmissionPolicy policy);

const char* setup_manifest_arch(const SetupManifest* manifest);
