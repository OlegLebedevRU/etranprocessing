#pragma once
#include "journal_reader.h"
typedef struct {
    char version[32],arch[8],origin[37];
    BYTE root_sha256[32],publisher[32],image_sha256[32];ULONGLONG image_size;
} L4ActiveUpdaterInfo;
typedef struct L4ActiveUpdater L4ActiveUpdater;
/* Fixed private pointer is an index, not authority. Completed immutable fresh
 * history, original85, owner-signed root and exact held image are mandatory.
 * SYSTEM/native KnownFolders only; no scans/fallback or mutable caller path. */
bool l4_active_updater_open_fixed(const L4Layout* roots,L4ActiveUpdater** updater);
bool l4_active_updater_open(L4Journal* owner,L4ActiveUpdater** updater);
bool l4_active_updater_verify(L4ActiveUpdater* updater);
const L4ActiveUpdaterInfo* l4_active_updater_info(const L4ActiveUpdater* updater);
HANDLE l4_active_updater_file(const L4ActiveUpdater* updater);
const wchar_t* l4_active_updater_path(const L4ActiveUpdater* updater);
void l4_active_updater_close(L4ActiveUpdater* updater);
/* Original fresh55/91 only. Atomic CREATE_NEW private pointer; exact retry okay,
 * conflict refuses. Suite operations never initialize/change this pointer. */
bool l4_active_updater_initialize_fresh(L4Journal* fresh,const char* arch);
/* Pure pointer codec only; does not authenticate an updater or install state. */
bool l4_active_updater_decode(const void* bytes,DWORD size,L4ActiveUpdaterInfo* info);
bool l4_active_updater_encode(const L4ActiveUpdaterInfo* info,char* bytes,DWORD capacity,DWORD* size);
