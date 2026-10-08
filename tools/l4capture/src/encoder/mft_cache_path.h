#pragma once
#include "../../../l4common/layout.h"
/* Derived, optional runtime cache. Installed releases never write beside EXE;
 * an absent/unwritable provisioned state leaf means no persistent cache. */
static bool l4c_mft_cache_path_for_exe(const wchar_t* exe,wchar_t* out,size_t cap){
 wchar_t path[MAX_PATH];
 if(!out || !l4_runtime_exe_path(exe,L"l4capture",L4_DATA_STATE,
     L"l4capture\\mft_capability.ini",L"mft_capability.ini",path) || wcslen(path)>=cap)return false;
 return wcscpy_s(out,cap,path)==0;
}
