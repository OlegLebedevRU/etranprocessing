#include "remote_request.h"
#include "journal_internal.h"
#include <string.h>
#include <wchar.h>
static bool valid(const L4Layout* roots,DWORD target,const char* version){
    if((target!=L4_REMOTE_SUITE && target!=L4_REMOTE_UPDATER) || !version || !*version || strlen(version)>=32)return false;
    if(!strcmp(version,"latest"))return true;
    wchar_t wide[32];L4Layout checked;
    return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,version,-1,wide,32) && l4_layout_from_roots(&checked,roots->binaries,roots->data,wide);
}
typedef struct {const L4Layout* roots;L4RemoteRequest result;bool found;} Scan;
bool l4_remote_request_decode(const L4Layout* roots,const void* data,DWORD size,L4RemoteRequest* saved){
    if(saved)memset(saved,0,sizeof(*saved));if(!roots || !data || !saved)return l4_store_fail(ERROR_INVALID_PARAMETER);
    const BYTE* bytes=data;
    if(size!=56 || memcmp(bytes,"L4RPC031",8) || l4_store_get32(bytes+8)!=1 || !memchr(bytes+16,0,32))return l4_store_fail(ERROR_INVALID_DATA);
    DWORD target=l4_store_get32(bytes+12);const char* version=(const char*)(bytes+16);ULONGLONG accepted=l4_store_get64(bytes+48);
    if(!valid(roots,target,version) || !accepted || accepted>2650467743999999999ULL)return l4_store_fail(ERROR_INVALID_DATA);
    for(size_t i=strlen(version)+1;i<32;++i)if(version[i])return l4_store_fail(ERROR_INVALID_DATA);
    saved->target=target;strcpy_s(saved->version,sizeof(saved->version),version);saved->accepted_utc=accepted;return true;
}
static bool visit(DWORD kind,ULONGLONG sequence,const void* data,DWORD size,void* context){
    Scan* scan=context;if(kind!=L4_RECORD_REMOTE_REQUEST)return true;
    if(scan->found || sequence!=1)return l4_store_fail(ERROR_INVALID_DATA);
    if(!l4_remote_request_decode(scan->roots,data,size,&scan->result))return false;scan->found=true;return true;
}
bool l4_remote_request_load(L4Journal* journal,L4RemoteRequest* saved){
    if(saved)memset(saved,0,sizeof(*saved));if(!journal || !saved)return l4_store_fail(ERROR_INVALID_PARAMETER);
    Scan scan={0};scan.roots=&journal->layout;
    if(!l4_journal_replay(journal,visit,&scan))return false;if(!scan.found)return l4_store_fail(ERROR_NOT_FOUND);
    *saved=scan.result;return true;
}
bool l4_remote_request_save(L4Journal* journal,DWORD target,const char* version,L4RemoteRequest* saved){
    if(saved)memset(saved,0,sizeof(*saved));
    if(!journal || !saved || journal->poisoned || journal->lock==INVALID_HANDLE_VALUE || !journal->lock || !valid(&journal->layout,target,version))return l4_store_fail(ERROR_INVALID_PARAMETER);
    L4RemoteRequest original;
    if(l4_remote_request_load(journal,&original)){
        if(original.target!=target || strcmp(original.version,version))return l4_store_fail(ERROR_ALREADY_EXISTS);
        *saved=original;return true;
    }
    if(GetLastError()!=ERROR_NOT_FOUND)return false;if(journal->sequence)return l4_store_fail(ERROR_INVALID_STATE);
    FILETIME time;GetSystemTimeAsFileTime(&time);BYTE bytes[56]={0};memcpy(bytes,"L4RPC031",8);l4_store_u32(bytes+8,1);l4_store_u32(bytes+12,target);
    strcpy_s((char*)bytes+16,32,version);l4_store_u64(bytes+48,((ULONGLONG)time.dwHighDateTime<<32)|time.dwLowDateTime);
    return l4_journal_append(journal,L4_RECORD_REMOTE_REQUEST,bytes,sizeof(bytes),NULL) && l4_remote_request_load(journal,saved);
}
