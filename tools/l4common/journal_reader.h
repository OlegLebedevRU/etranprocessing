#pragma once
#include "journal.h"
typedef struct L4JournalReader L4JournalReader;
/* Existing private operation only, under the caller's held deployment lock.
 * Immutable full-chain snapshot: refuses torn tails and never creates/repairs. */
bool l4_journal_reader_open(L4Journal* owner,const wchar_t* source_id,L4JournalReader** result);
/* SYSTEM-only transient ACK/status snapshot, no deployment lock. KnownFolders
 * roots/private storage only. Incomplete/changing tail: ERROR_IO_PENDING, no
 * callback data. Never an installed-source authority or codec/mutation view. */
bool l4_journal_reader_open_live(const L4Layout* layout,const wchar_t* operation_id,L4JournalReader** result);
/* SYSTEM/native fixed-reference immutable historical snapshot, FILE_SHARE_READ
 * only. No deployment lock/mutation or source authority; caller must separately
 * authenticate its protected reference and completed signed fresh history. */
bool l4_journal_reader_open_fixed_immutable_reference(const L4Layout* layout,const wchar_t* operation_id,L4JournalReader** result);
void l4_journal_reader_close(L4JournalReader* reader);
bool l4_journal_reader_replay(const L4JournalReader* reader,L4JournalVisitor visitor,void* context);
bool l4_journal_reader_find(const L4JournalReader* reader,DWORD kind,ULONGLONG sequence,BYTE** bytes,DWORD* size);
const wchar_t* l4_journal_reader_directory(const L4JournalReader* reader);
/* Historical locked snapshot only; live ACK/status snapshots never grant
 * installed-source metadata admission. No mutable codec view is exposed. */
bool l4_journal_reader_is_immutable(const L4JournalReader* reader);
