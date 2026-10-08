#pragma once
#include "journal.h"
#include "release.h"

/* Pointer bytes are prepared through the same durable config transaction.
 * Caller supplies a complete authenticated inventory; this does not verify signatures. */
bool l4_launcher_prepare(L4Journal* journal,const L4Layout* target,const wchar_t* tool,
                         const L4ReleaseFile* files,unsigned count,ULONGLONG* sequence);
/* Fixed supported names only. Holds verified executable/ancestors until release_unpin. */
bool l4_launcher_resolve(const L4Layout* roots,const wchar_t* tool,
                         L4ReleaseFence** fence,wchar_t executable[MAX_PATH]);
bool l4_launcher_identity(const wchar_t* self,const L4Layout* roots,const wchar_t** tool);
bool l4_launcher_command(const wchar_t* executable,const wchar_t* original,wchar_t** command);
/* Waits for its child, preserving arguments, standard streams and exit code.
 * Work directory must have been provisioned by the account/access gate. */
bool l4_launcher_run(const L4Layout* roots,const wchar_t* tool,const wchar_t* original,DWORD* exit_code);

/* Fresh bootstrap only: flushed intent, immutable copy/reuse, flushed done.
 * No replacement, PATH modification or service registration. */
bool l4_launcher_install(L4Journal* journal,const L4Layout* target,const wchar_t* tool,
                         const L4ReleaseFile* files,unsigned count);
