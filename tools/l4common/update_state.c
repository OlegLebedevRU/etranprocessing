#include "update_state_internal.h"
#include <stdlib.h>
#include <string.h>
static bool owner_valid(const char* owner){
    if(owner[36] || owner[37] || owner[38] || owner[39])return false;
    bool nonzero=false;
    for(unsigned i=0;i<36;i++){
        char c=owner[i];
        if(i==8 || i==13 || i==18 || i==23){if(c!='-')return false;}
        else if((c>='0' && c<='9') || (c>='a' && c<='f')){if(c!='0')nonzero=true;}
        else return false;
    }return nonzero;
}
static bool valid(const L4UpdateState* state){
    if(!state->generation){
        char zero[40]={0};return !memcmp(state->owner,zero,40) && !state->window && !state->plan_sequence && !state->deadline_utc;
    }
    return owner_valid(state->owner) && state->plan_sequence && state->window<=L4_UPDATE_OTHER_TOOLS &&
        (state->window?state->deadline_utc!=0:state->deadline_utc==0);
}
bool l4_update_state_encode(const L4UpdateState* state,BYTE bytes[L4_UPDATE_STATE_SIZE]){
    if(!state || !bytes || !valid(state))return l4_store_fail(ERROR_INVALID_DATA);
    memset(bytes,0,L4_UPDATE_STATE_SIZE);memcpy(bytes,"L4UPD01",8);memcpy(bytes+8,state->owner,40);
    l4_store_u32(bytes+48,state->window);l4_store_u64(bytes+56,state->generation);
    l4_store_u64(bytes+64,state->plan_sequence);l4_store_u64(bytes+72,state->deadline_utc);
    return l4_store_hash(bytes,80,NULL,0,bytes+80);
}
static bool streams(const wchar_t* path){
    WIN32_FIND_STREAM_DATA data;HANDLE h=FindFirstStreamW(path,FindStreamInfoStandard,&data,0);
    if(h==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_HANDLE_EOF;
    bool ok=true;do{if(wcscmp(data.cStreamName,L"::$DATA")){ok=false;break;}}while(FindNextStreamW(h,&data));
    DWORD error=GetLastError();FindClose(h);return ok && error==ERROR_HANDLE_EOF;
}
bool l4_update_state_pin(const L4Layout* layout,L4FileFence* fence){
    if(!l4_store_pin(layout->operations,layout->data,false,fence))return false;
    /* Fixed layout tail: Data, update, operations. Unlike writable component
     * leaves, no other actor may replace/delete a control file through a parent. */
    bool ok=fence->count>=3;
    for(unsigned i=ok?fence->count-3:0;ok && i<fence->count;i++){
        BYTE* sd=NULL;DWORD size=0;PACL acl=NULL;BOOL present=FALSE,defaulted=FALSE;
        ok=l4_store_security(fence->handles[i],false,&sd,&size) && GetSecurityDescriptorDacl(sd,&present,&acl,&defaulted) && present && acl;
        for(WORD n=0;ok && n<acl->AceCount;n++){
            ACCESS_ALLOWED_ACE* ace=NULL;ok=GetAce(acl,n,(void**)&ace)!=0;if(!ok)break;
            PSID sid=&ace->SidStart;
            if(!IsWellKnownSid(sid,WinLocalSystemSid) && !IsWellKnownSid(sid,WinBuiltinAdministratorsSid))
                ok=!(ace->Mask&~(FILE_GENERIC_READ|FILE_GENERIC_EXECUTE|GENERIC_READ|GENERIC_EXECUTE));
        }free(sd);
    }
    if(!ok){l4_store_unpin(fence);return l4_store_fail(ERROR_ACCESS_DENIED);}return true;
}
bool l4_update_state_read(const L4Layout* layout,L4UpdateState* state){
    if(!layout || !state)return l4_store_fail(ERROR_INVALID_PARAMETER);
    memset(state,0,sizeof(*state));wchar_t path[MAX_PATH];L4FileFence fence={0};
    if(!l4_layout_data_path(layout,L"update\\operations\\update.state",path) ||
       !l4_update_state_pin(layout,&fence))return false;
    HANDLE file=CreateFileW(path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    bool ok=file!=INVALID_HANDLE_VALUE;DWORD error=GetLastError();
    if(ok)SetLastError(ERROR_SUCCESS);
    BYTE* sd=NULL;DWORD sd_size=0;BY_HANDLE_FILE_INFORMATION info={0};LARGE_INTEGER size={0};BYTE bytes[L4_UPDATE_STATE_SIZE];DWORD read=0;
    if(ok)ok=GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks==1 &&
        l4_store_security(file,true,&sd,&sd_size) && streams(path) && GetFileSizeEx(file,&size) && size.QuadPart==L4_UPDATE_STATE_SIZE &&
        ReadFile(file,bytes,sizeof(bytes),&read,NULL) && read==sizeof(bytes);
    BYTE digest[32];L4UpdateState value={0};
    if(ok)ok=!memcmp(bytes,"L4UPD01",8) && !l4_store_get32(bytes+52) && l4_store_hash(bytes,80,NULL,0,digest) && !memcmp(digest,bytes+80,32);
    if(ok){memcpy(value.owner,bytes+8,40);value.window=l4_store_get32(bytes+48);value.generation=l4_store_get64(bytes+56);
        value.plan_sequence=l4_store_get64(bytes+64);value.deadline_utc=l4_store_get64(bytes+72);ok=valid(&value);}
    if(!ok && file!=INVALID_HANDLE_VALUE)error=GetLastError()?GetLastError():ERROR_INVALID_DATA;
    free(sd);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);l4_store_unpin(&fence);
    if(!ok)return l4_store_fail(error?error:ERROR_INVALID_DATA);*state=value;return true;
}
bool l4_update_consumer_init(const wchar_t* component,L4UpdateConsumer* consumer){
    if(!consumer)return l4_store_fail(ERROR_INVALID_PARAMETER);
    memset(consumer,0,sizeof(*consumer));consumer->enabled=true;
    wchar_t exe[MAX_PATH],release[MAX_PATH];bool installed=false;
    DWORD count=GetModuleFileNameW(NULL,exe,MAX_PATH);
    if(!count || count>=MAX_PATH || !l4_runtime_release_from_exe(exe,component,release,&installed))return false;
    if(!installed){consumer->enabled=false;return true;}
    const wchar_t* version=wcsrchr(release,L'\\');
    return version && l4_layout_resolve(&consumer->layout,version+1) && !_wcsicmp(release,consumer->layout.release);
}
bool l4_update_consumer_read(const L4UpdateConsumer* consumer,L4UpdateState* state){
    if(!consumer || !state)return l4_store_fail(ERROR_INVALID_PARAMETER);
    if(!consumer->enabled){memset(state,0,sizeof(*state));return true;}
    return l4_update_state_read(&consumer->layout,state);
}
static bool same_state(const L4UpdateState* a,const L4UpdateState* b){
    return !memcmp(a->owner,b->owner,40) && a->window==b->window && a->generation==b->generation &&
        a->plan_sequence==b->plan_sequence && a->deadline_utc==b->deadline_utc;
}
static ULONGLONG utc_now(void){FILETIME time;GetSystemTimeAsFileTime(&time);return ((ULONGLONG)time.dwHighDateTime<<32)|time.dwLowDateTime;}
DWORD l4_update_consumer_drain(L4UpdateConsumer* consumer,const L4UpdateState* expected,
                               DWORD timeout,HANDLE cancel,L4UpdateBusy busy,void* context){
    if(!consumer || !consumer->enabled || !expected || !valid(expected) || !expected->window ||
       !timeout || timeout>300000 || !cancel || !busy)return ERROR_INVALID_PARAMETER;
    ULONGLONG deadline=GetTickCount64()+timeout;
    for(;;){
        if(WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT)return ERROR_CANCELLED;
        if(GetTickCount64()>=deadline || utc_now()>=expected->deadline_utc)return ERROR_TIMEOUT;
        if(TryAcquireSRWLockExclusive(&consumer->admission)){
            L4UpdateState current;DWORD result=ERROR_BUSY;
            if(!l4_update_consumer_read(consumer,&current))result=ERROR_INVALID_DATA;
            else if(!same_state(&current,expected))result=ERROR_REVISION_MISMATCH;
            else if(!busy(context)){
                /* Recheck after asynchronous consumer observations; never echo
                 * success from a previous operation/window or an expired budget. */
                if(!l4_update_consumer_read(consumer,&current))result=ERROR_INVALID_DATA;
                else result=same_state(&current,expected)?ERROR_SUCCESS:ERROR_REVISION_MISMATCH;
            }
            ReleaseSRWLockExclusive(&consumer->admission);
            if(WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT)return ERROR_CANCELLED;
            if(GetTickCount64()>=deadline || utc_now()>=expected->deadline_utc)return ERROR_TIMEOUT;
            if(result!=ERROR_BUSY)return result;
        }
        ULONGLONG now=GetTickCount64();DWORD left=now<deadline?(DWORD)(deadline-now):0;if(left>10)left=10;
        if(WaitForSingleObject(cancel,left)!=WAIT_TIMEOUT)return ERROR_CANCELLED;
    }
}
