#include "update_state_internal.h"
#include <sddl.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool l4_update_state_replace(const L4Layout* layout,const BYTE bytes[L4_UPDATE_STATE_SIZE],bool create_only){
    wchar_t target[MAX_PATH],temp[MAX_PATH];L4FileFence fence={0};
    if(!l4_layout_data_path(layout,L"update\\operations\\update.state",target) || !l4_update_state_pin(layout,&fence))return false;
    GUID guid;wchar_t text[40];bool ok=SUCCEEDED(CoCreateGuid(&guid)) && StringFromGUID2(&guid,text,40)==39;
    if(ok)ok=swprintf_s(temp,MAX_PATH,L"%ls\\%ls.state.tmp",layout->operations,text)>0;
    PSECURITY_DESCRIPTOR sd=NULL;HANDLE previous=NULL;bool scoped=false;HANDLE file=INVALID_HANDLE_VALUE;
    if(ok)ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL)!=0;
    if(ok){scoped=l4_layout_owner_begin(&previous);ok=scoped;}
    if(ok){SECURITY_ATTRIBUTES attrs={sizeof(attrs),sd,FALSE};file=CreateFileW(temp,GENERIC_WRITE|DELETE,0,&attrs,CREATE_NEW,FILE_FLAG_WRITE_THROUGH,NULL);ok=file!=INVALID_HANDLE_VALUE;}
    DWORD error=GetLastError();
    if(scoped && !l4_layout_owner_end(previous)){
        if(file!=INVALID_HANDLE_VALUE){FILE_DISPOSITION_INFO disposition={TRUE};SetFileInformationByHandle(file,FileDispositionInfo,&disposition,sizeof(disposition));CloseHandle(file);}
        if(sd)LocalFree(sd);
        /* Never return to the controller with an unrestored privilege scope. */
        TerminateProcess(GetCurrentProcess(),ERROR_CANNOT_IMPERSONATE);ExitProcess(ERROR_CANNOT_IMPERSONATE);
    }
    if(sd)LocalFree(sd);
    if(ok){ok=l4_store_write(file,bytes,L4_UPDATE_STATE_SIZE) && FlushFileBuffers(file);if(!ok)error=GetLastError();}
    if(!ok && file!=INVALID_HANDLE_VALUE){FILE_DISPOSITION_INFO disposition={TRUE};SetFileInformationByHandle(file,FileDispositionInfo,&disposition,sizeof(disposition));}
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(ok){ok=MoveFileExW(temp,target,MOVEFILE_WRITE_THROUGH|(create_only?0:MOVEFILE_REPLACE_EXISTING))!=0;
        if(!ok){error=GetLastError();DeleteFileW(temp);}}
    l4_store_unpin(&fence);return ok?true:l4_store_fail(error?error:ERROR_INVALID_DATA);
}
bool l4_update_state_provision(L4Journal* j){
    if(!j || j->poisoned || j->lock==INVALID_HANDLE_VALUE)return l4_store_fail(ERROR_INVALID_PARAMETER);
    L4UpdateState state;
    if(l4_update_state_read(&j->layout,&state))return state.window?l4_store_fail(ERROR_BUSY):true;
    if(GetLastError()!=ERROR_FILE_NOT_FOUND)return false;
    const wchar_t* fixed_services[]={L"Leo4Proxy",L"Mosquitto",L"L4Con",L"L4Superv"};
    for(unsigned i=0;i<4;i++){L4ServiceInventory service;
        if(!l4_service_inventory(fixed_services[i],&service))return false;
        if(service.installed)return l4_store_fail(ERROR_SERVICE_EXISTS);}
    BYTE bytes[L4_UPDATE_STATE_SIZE];memset(&state,0,sizeof(state));
    return l4_update_state_encode(&state,bytes) && l4_journal_append(j,L4_RECORD_UPDATE_STATE_INTENT,bytes,sizeof(bytes),NULL) && l4_update_state_replace(&j->layout,bytes,true);
}
bool l4_update_state_publish(L4Journal* j,ULONGLONG plan_sequence,ULONGLONG expected_generation,DWORD window,ULONGLONG deadline){
    if(!j || j->poisoned || j->lock==INVALID_HANDLE_VALUE || !plan_sequence || window>2 || (window?!deadline:deadline!=0) || expected_generation==~0ull)return l4_store_fail(ERROR_INVALID_PARAMETER);
    BYTE* plan=NULL;DWORD size=0;
    if(!l4_store_find_record(j,64,plan_sequence,&plan,&size))return false;free(plan);
    if(!size)return l4_store_fail(ERROR_INVALID_DATA);
    wchar_t id[40];GUID guid;memcpy(&guid,j->header+8,sizeof(guid));
    if(StringFromGUID2(&guid,id,40)!=39)return l4_store_fail(ERROR_INVALID_DATA);
    L4UpdateState before,value={0};
    for(unsigned i=0;i<36;i++){wchar_t c=id[i+1];value.owner[i]=(char)((c>=L'A' && c<=L'F')?c+32:c);}
    value.window=window;value.generation=expected_generation+1;value.plan_sequence=plan_sequence;value.deadline_utc=deadline;
    if(!l4_update_state_read(&j->layout,&before))return false;
    if(before.generation==value.generation && !memcmp(before.owner,value.owner,40) && before.window==window && before.plan_sequence==plan_sequence && before.deadline_utc==deadline)return true;
    if(before.generation!=expected_generation)return l4_store_fail(ERROR_REVISION_MISMATCH);
    FILETIME time;GetSystemTimeAsFileTime(&time);ULONGLONG now=((ULONGLONG)time.dwHighDateTime<<32)|time.dwLowDateTime;
    if(window && (deadline<=now || (before.window && before.deadline_utc<=now)))return l4_store_fail(ERROR_TIMEOUT);
    bool same=!memcmp(before.owner,value.owner,40) && before.plan_sequence==plan_sequence;
    if(before.window){
        if(!same || (window && !(before.window==1 && window==2)))return l4_store_fail(ERROR_BUSY);
    }else if(!window)return l4_store_fail(ERROR_INVALID_STATE);
    BYTE bytes[L4_UPDATE_STATE_SIZE];
    if(!l4_update_state_encode(&value,bytes))return false;
    /* A flushed intent can survive a failed atomic file replacement. Reuse its
     * exact bytes instead of appending a second transition on retry. An older
     * last intent must already match actual state before a new one is recorded. */
    BYTE* last=NULL;DWORD length=0;bool reuse=false;
    if(l4_store_find_record(j,L4_RECORD_UPDATE_STATE_INTENT,j->sequence,&last,&length)){
        BYTE actual[L4_UPDATE_STATE_SIZE];
        bool valid=length==sizeof(bytes);
        if(valid && !memcmp(last,bytes,sizeof(bytes)))reuse=true;
        else valid=valid && l4_update_state_encode(&before,actual) && !memcmp(last,actual,sizeof(actual));
        free(last);if(!valid)return l4_store_fail(ERROR_REVISION_MISMATCH);
    }else if(GetLastError()!=ERROR_NOT_FOUND)return false;
    return (reuse || l4_journal_append(j,L4_RECORD_UPDATE_STATE_INTENT,bytes,sizeof(bytes),NULL)) && l4_update_state_replace(&j->layout,bytes,false);
}
