#pragma once
#include "journal.h"
typedef struct { HANDLE handles[MAX_PATH/2+2]; unsigned count; } L4FileFence;
struct L4Journal {
    L4Layout layout;
    HANDLE lock,file;
    L4FileFence fence;
    wchar_t directory[MAX_PATH];
    ULONGLONG sequence,end;
    BYTE digest[32],header[24];
    bool poisoned;
};
bool l4_store_fail(DWORD code);
bool l4_store_pin(const wchar_t* directory,const wchar_t* secure_root,bool control,L4FileFence* fence);
void l4_store_unpin(L4FileFence* fence);
bool l4_store_security(HANDLE file,bool control,BYTE** bytes,DWORD* size);
bool l4_store_hash(const void* bytes,DWORD size,const void* extra,DWORD extra_size,BYTE hash[32]);
bool l4_store_write(HANDLE file,const void* bytes,DWORD size);
void l4_store_u32(BYTE* out,DWORD value);
void l4_store_u64(BYTE* out,ULONGLONG value);
DWORD l4_store_get32(const BYTE* bytes);
ULONGLONG l4_store_get64(const BYTE* bytes);
bool l4_store_find_record(L4Journal* journal,DWORD kind,ULONGLONG sequence,BYTE** bytes,DWORD* size);
