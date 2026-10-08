#include "recovery_store.h"
#include "journal_internal.h"
#include "update_state_internal.h"
#include <sddl.h>
#include <objbase.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct L4RecoveryGuard {L4Layout roots;wchar_t directory[MAX_PATH];HANDLE lock,plan_file;L4FileFence parents,base;
    BYTE* bytes;DWORD size;L4RecoveryPlan plan;BYTE digest[32];bool locked;};
static bool fail(DWORD error){SetLastError(error);return false;}
static bool path(const wchar_t* directory,const wchar_t* name,wchar_t result[MAX_PATH]){
    return swprintf_s(result,MAX_PATH,L"%ls\\%ls",directory,name)>0;
}
static bool streams(const wchar_t* name){WIN32_FIND_STREAM_DATA data;HANDLE search=FindFirstStreamW(name,FindStreamInfoStandard,&data,0);
    if(search==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_HANDLE_EOF;
    bool ok=true;do{if(wcscmp(data.cStreamName,L"::$DATA")){ok=false;break;}}while(FindNextStreamW(search,&data));
    DWORD error=GetLastError();FindClose(search);return ok && error==ERROR_HANDLE_EOF;}
static bool checked(HANDLE file,const wchar_t* name,DWORD limit,DWORD* size){
    BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER length={0};BYTE* sd=NULL;DWORD sd_size;
    bool ok=GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks==1 &&
        GetFileSizeEx(file,&length) && length.QuadPart>=0 && length.QuadPart<=limit && l4_store_security(file,true,&sd,&sd_size) && streams(name);
    free(sd);if(!ok)return fail(ERROR_INVALID_DATA);*size=(DWORD)length.QuadPart;return true;
}
static bool read_file(const wchar_t* directory,const wchar_t* name,DWORD limit,HANDLE* held,BYTE** bytes,DWORD* size){
    *held=INVALID_HANDLE_VALUE;*bytes=NULL;*size=0;wchar_t full[MAX_PATH];if(!path(directory,name,full))return fail(ERROR_FILENAME_EXCED_RANGE);
    HANDLE file=CreateFileW(full,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);if(file==INVALID_HANDLE_VALUE)return false;
    bool ok=checked(file,full,limit,size);BYTE* value=ok?malloc(*size?*size:1):NULL;if(ok && !value){ok=false;SetLastError(ERROR_NOT_ENOUGH_MEMORY);}
    DWORD count=0;if(ok)ok=ReadFile(file,value,*size,&count,NULL) && count==*size;
    DWORD error=GetLastError();if(!ok){free(value);CloseHandle(file);return fail(error?error:ERROR_INVALID_DATA);}*held=file;*bytes=value;return true;
}
static bool private_create(const wchar_t* name,HANDLE* result){
    *result=INVALID_HANDLE_VALUE;HANDLE previous=NULL;PSECURITY_DESCRIPTOR sd=NULL;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL))return false;
    /* Producer must restore its scoped token even when previous=NULL. Preserve
     * frozen reader/helper compilation; this branch changes producer only. */
#ifndef L4_RECOVERY_READER_ONLY
    bool scoped=l4_layout_owner_begin(&previous),ok=scoped;
#else
    bool ok=l4_layout_owner_begin(&previous);
#endif
    DWORD error=GetLastError();SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};
    if(ok){*result=CreateFileW(name,GENERIC_READ|GENERIC_WRITE|READ_CONTROL|DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,NULL);ok=*result!=INVALID_HANDLE_VALUE;error=GetLastError();}
#ifndef L4_RECOVERY_READER_ONLY
    if(scoped && !l4_layout_owner_end(previous)){
#else
    if(previous && !l4_layout_owner_end(previous)){
#endif
        if(*result!=INVALID_HANDLE_VALUE)CloseHandle(*result);LocalFree(sd);TerminateProcess(GetCurrentProcess(),ERROR_CANNOT_IMPERSONATE);ExitProcess(ERROR_CANNOT_IMPERSONATE);}
    LocalFree(sd);return ok?true:fail(error);
}
static void discard(HANDLE file){FILE_DISPOSITION_INFO disposition={TRUE};SetFileInformationByHandle(file,FileDispositionInfo,&disposition,sizeof(disposition));}
#ifndef L4_RECOVERY_READER_ONLY
static bool immutable_file(const wchar_t* directory,const wchar_t* leaf,const BYTE* bytes,DWORD size){
    wchar_t full[MAX_PATH];if(!path(directory,leaf,full))return fail(ERROR_FILENAME_EXCED_RANGE);HANDLE file;
    if(private_create(full,&file)){bool ok=l4_store_write(file,bytes,size) && FlushFileBuffers(file);DWORD error=GetLastError();if(!ok)discard(file);CloseHandle(file);return ok?true:fail(error);}
    if(GetLastError()!=ERROR_FILE_EXISTS && GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    BYTE* existing=NULL;DWORD count=0;
    if(!read_file(directory,leaf,size,&file,&existing,&count))return false;
    bool ok=count==size && (!size || !memcmp(existing,bytes,size));free(existing);CloseHandle(file);return ok?true:fail(ERROR_REVISION_MISMATCH);
}
bool l4_recovery_prepare(L4Journal* j,const L4RecoveryPlan* plan,HANDLE worker){
    if(!j || j->poisoned || !j->lock || j->lock==INVALID_HANDLE_VALUE || !plan || !worker || memcmp(&plan->operation,j->header+8,16))return fail(ERROR_INVALID_PARAMETER);
    FILETIME created,exit,kernel,user;DWORD code=STILL_ACTIVE;
    if(GetProcessId(worker)!=plan->worker_pid || !GetProcessTimes(worker,&created,&exit,&kernel,&user) || CompareFileTime(&created,&plan->worker_created) ||
       !GetExitCodeProcess(worker,&code) || code!=STILL_ACTIVE)return fail(ERROR_REVISION_MISMATCH);
    FILETIME now;GetSystemTimeAsFileTime(&now);ULONGLONG utc=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;
    if(utc<plan->armed_utc || utc>=plan->deadline_utc)return fail(ERROR_TIME_SKEW);
    BYTE* operation=NULL;DWORD operation_size=0;
    if(!l4_store_find_record(j,64,plan->sequence,&operation,&operation_size))return false;
    free(operation);if(!operation_size)return fail(ERROR_INVALID_DATA);
    BYTE* bytes=NULL;DWORD size=0;if(!l4_recovery_encode(&j->layout,plan,&bytes,&size))return false;
    L4FileFence base={0},parents={0};bool ok=l4_update_state_pin(&j->layout,&base) && l4_store_pin(j->directory,j->directory,true,&parents);
    if(ok)ok=immutable_file(j->directory,L"supervisor.decision.lock",NULL,0);
    if(ok)ok=immutable_file(j->directory,L"supervisor.runner.lock",NULL,0);
    bool published=false;
    if(ok){HANDLE file;BYTE* existing=NULL;DWORD count=0;
        if(read_file(j->directory,L"supervisor.recovery",L4_RECOVERY_PLAN_LIMIT,&file,&existing,&count)){
            published=count==size && !memcmp(existing,bytes,size);free(existing);CloseHandle(file);
            if(!published){ok=false;SetLastError(ERROR_REVISION_MISMATCH);}
        }else if(GetLastError()!=ERROR_FILE_NOT_FOUND)ok=false;
    }
    /* Publication intent is for producer audit only. Helper never loads journal.
     * Exact retries and conflicting plans do not append another intent. */
    if(ok && !published)ok=l4_journal_append(j,66,bytes,size,NULL) && immutable_file(j->directory,L"supervisor.recovery",bytes,size);
    DWORD error=GetLastError();l4_store_unpin(&parents);l4_store_unpin(&base);free(bytes);return ok?true:fail(error);
}
#endif /* producer only */
void l4_recovery_release(L4RecoveryGuard* g){
    if(g && g->locked){OVERLAPPED io={0};UnlockFileEx(g->lock,0,1,0,&io);g->locked=false;}
}
bool l4_recovery_relock(L4RecoveryGuard* g,DWORD timeout){
    if(!g || g->locked || !timeout || timeout>300000 || g->lock==INVALID_HANDLE_VALUE)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG deadline=GetTickCount64()+timeout;
    while(!g->locked){OVERLAPPED io={0};g->locked=LockFileEx(g->lock,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&io)!=0;
        if(g->locked)break;DWORD error=GetLastError();if(error!=ERROR_LOCK_VIOLATION)return false;
        ULONGLONG now=GetTickCount64();if(now>=deadline)return fail(ERROR_TIMEOUT);Sleep(deadline-now<10?(DWORD)(deadline-now):10);}
    if(GetTickCount64()>=deadline){l4_recovery_release(g);return fail(ERROR_TIMEOUT);}return true;
}
void l4_recovery_close(L4RecoveryGuard* g){if(!g)return;l4_recovery_release(g);if(g->lock!=INVALID_HANDLE_VALUE)CloseHandle(g->lock);
    if(g->plan_file!=INVALID_HANDLE_VALUE)CloseHandle(g->plan_file);l4_store_unpin(&g->parents);l4_store_unpin(&g->base);free(g->bytes);free(g);}
bool l4_recovery_open(const L4Layout* roots,const wchar_t* operation,DWORD timeout,L4RecoveryGuard** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;if(!roots || !operation || wcslen(operation)!=36 || !timeout || timeout>300000)return fail(ERROR_INVALID_PARAMETER);
    wchar_t uuid[40];swprintf_s(uuid,40,L"{%ls}",operation);GUID id;wchar_t canonical[40];
    if(FAILED(CLSIDFromString(uuid,&id)) || StringFromGUID2(&id,canonical,40)!=39 || _wcsnicmp(canonical+1,operation,36))return fail(ERROR_INVALID_NAME);
    ULONGLONG deadline=GetTickCount64()+timeout;
    L4RecoveryGuard* g=calloc(1,sizeof(*g));if(!g)return fail(ERROR_NOT_ENOUGH_MEMORY);g->roots=*roots;g->lock=g->plan_file=INVALID_HANDLE_VALUE;
    bool ok=swprintf_s(g->directory,MAX_PATH,L"%ls\\%ls",roots->operations,operation)>0 && l4_update_state_pin(roots,&g->base) && l4_store_pin(g->directory,g->directory,true,&g->parents);
    if(ok)ok=read_file(g->directory,L"supervisor.recovery",L4_RECOVERY_PLAN_LIMIT,&g->plan_file,&g->bytes,&g->size) &&
        l4_recovery_decode(roots,g->bytes,g->size,&g->plan) && !memcmp(&id,&g->plan.operation,16);
    if(ok)memcpy(g->digest,g->bytes+g->size-32,32);
    wchar_t name[MAX_PATH];DWORD size=0;
    if(ok)ok=path(g->directory,L"supervisor.decision.lock",name);
    if(ok){g->lock=CreateFileW(name,GENERIC_READ|GENERIC_WRITE|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);ok=g->lock!=INVALID_HANDLE_VALUE && checked(g->lock,name,0,&size);}
    if(ok){ULONGLONG now=GetTickCount64();if(now>=deadline){ok=false;SetLastError(ERROR_TIMEOUT);}else ok=l4_recovery_relock(g,(DWORD)(deadline-now));}
    DWORD error=GetLastError();if(!ok){l4_recovery_close(g);return fail(error?error:ERROR_INVALID_DATA);}*result=g;return true;
}
const L4RecoveryPlan* l4_recovery_plan(const L4RecoveryGuard* g){return g?&g->plan:NULL;}
static bool status(L4RecoveryGuard* g,L4RecoveryStatus* phase,DWORD* error){
    if(!g || !g->locked)return fail(ERROR_INVALID_PARAMETER);*phase=0;*error=0;HANDLE file;BYTE* bytes=NULL;DWORD size=0;
    if(!read_file(g->directory,L"supervisor.result",96,&file,&bytes,&size))return GetLastError()==ERROR_FILE_NOT_FOUND;
    bool ok=l4_recovery_result_decode(&g->plan,g->digest,bytes,size,phase,error);DWORD code=GetLastError();free(bytes);CloseHandle(file);return ok?true:fail(code);
}
bool l4_recovery_action(L4RecoveryGuard* g,ULONGLONG now,bool boot,L4RecoveryAction* action){L4RecoveryStatus phase;DWORD error;
    return status(g,&phase,&error) && l4_recovery_decide(&g->plan,phase,now,boot,action);}
static bool publish(L4RecoveryGuard* g,L4RecoveryStatus phase,DWORD error){
    BYTE bytes[96];if(!l4_recovery_result_encode(&g->plan,g->digest,phase,error,bytes))return false;
    GUID id;wchar_t uuid[40],temp[MAX_PATH],target[MAX_PATH];if(FAILED(CoCreateGuid(&id)) || StringFromGUID2(&id,uuid,40)!=39 ||
       swprintf_s(temp,MAX_PATH,L"%ls\\.%ls.result.tmp",g->directory,uuid)<0 || !path(g->directory,L"supervisor.result",target))return fail(ERROR_INVALID_NAME);
    HANDLE file;if(!private_create(temp,&file))return false;bool ok=l4_store_write(file,bytes,96) && FlushFileBuffers(file);DWORD code=GetLastError();if(!ok)discard(file);CloseHandle(file);
    if(ok){ok=MoveFileExW(temp,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;code=GetLastError();if(!ok)DeleteFileW(temp);}return ok?true:fail(code);
}
bool l4_recovery_begin(L4RecoveryGuard* g,ULONGLONG now,bool boot){
    L4RecoveryStatus phase;DWORD error;L4RecoveryAction action;if(!status(g,&phase,&error) || !l4_recovery_decide(&g->plan,phase,now,boot,&action))return false;
    if(action!=L4_RECOVERY_REQUIRED)return fail(ERROR_INVALID_STATE);return phase==L4_RECOVERY_STARTED || publish(g,L4_RECOVERY_STARTED,0);
}
bool l4_recovery_finish(L4RecoveryGuard* g,L4RecoveryStatus desired,DWORD error,ULONGLONG now){
    if(desired<L4_RECOVERY_COMMITTED || desired>L4_RECOVERY_FAILED)return fail(ERROR_INVALID_PARAMETER);
    L4RecoveryStatus phase;DWORD previous;if(!status(g,&phase,&previous))return false;
    if(desired==phase && error==previous && phase!=0)return true;
    if(desired==L4_RECOVERY_COMMITTED){if(phase || now<g->plan.armed_utc || now>=g->plan.deadline_utc || error)return fail(ERROR_INVALID_STATE);}
    else if((desired!=L4_RECOVERY_RESTORED && desired!=L4_RECOVERY_FAILED) || phase!=L4_RECOVERY_STARTED)return fail(ERROR_INVALID_STATE);
    return publish(g,desired,error);
}
