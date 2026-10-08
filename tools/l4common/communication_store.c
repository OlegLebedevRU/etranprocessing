#include "communication_plan.h"
#include "journal_internal.h"
#include "update_state_internal.h"
#include <sddl.h>
#include <objbase.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct L4CommunicationPin {L4Layout roots;HANDLE file;L4FileFence parents,base;BYTE* bytes;DWORD size;L4CommunicationPlan plan;};
/* Private file primitives intentionally preserve frozen helper's boundary;
 * no helper source/build changed or generic helper functionality introduced. */
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
    bool scoped=l4_layout_owner_begin(&previous),ok=scoped;DWORD error=GetLastError();SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};
    if(ok){*result=CreateFileW(name,GENERIC_READ|GENERIC_WRITE|READ_CONTROL|DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,NULL);ok=*result!=INVALID_HANDLE_VALUE;error=GetLastError();}
    if(scoped && !l4_layout_owner_end(previous)){if(*result!=INVALID_HANDLE_VALUE)CloseHandle(*result);LocalFree(sd);TerminateProcess(GetCurrentProcess(),ERROR_CANNOT_IMPERSONATE);ExitProcess(ERROR_CANNOT_IMPERSONATE);}
    LocalFree(sd);return ok?true:fail(error);
}
static void discard(HANDLE file){FILE_DISPOSITION_INFO disposition={TRUE};SetFileInformationByHandle(file,FileDispositionInfo,&disposition,sizeof(disposition));}
#ifndef L4_COMMUNICATION_READER_ONLY
static bool immutable_file(const wchar_t* directory,const wchar_t* leaf,const BYTE* bytes,DWORD size){
    wchar_t full[MAX_PATH];if(!path(directory,leaf,full))return fail(ERROR_FILENAME_EXCED_RANGE);HANDLE file;
    if(private_create(full,&file)){bool ok=l4_store_write(file,bytes,size) && FlushFileBuffers(file);DWORD error=GetLastError();if(!ok)discard(file);CloseHandle(file);return ok?true:fail(error);}
    if(GetLastError()!=ERROR_FILE_EXISTS && GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    BYTE* existing=NULL;DWORD count=0;
    if(!read_file(directory,leaf,size,&file,&existing,&count))return false;
    bool ok=count==size && (!size || !memcmp(existing,bytes,size));free(existing);CloseHandle(file);return ok?true:fail(ERROR_REVISION_MISMATCH);
}

static bool same_record(L4Journal* j,DWORD kind,ULONGLONG sequence,const BYTE* expected,DWORD size){
    BYTE* bytes=NULL;DWORD length=0;if(!l4_store_find_record(j,kind,sequence,&bytes,&length))return false;
    bool ok=length==size && !memcmp(bytes,expected,size);free(bytes);return ok?true:fail(ERROR_REVISION_MISMATCH);
}
static bool operation_binding(L4Journal* j,const L4CommunicationPlan* p){
    BYTE* b=NULL;DWORD size=0;if(!l4_store_find_record(j,64,p->sequence,&b,&size))return false;
    BYTE digest[32];bool ok=size>=28 && l4_store_get32(b)==1 && l4_store_hash(b,size,NULL,0,digest) && !memcmp(digest,p->operation_sha256,32);
    DWORD count=ok?l4_store_get32(b+20):0,configs=ok?l4_store_get32(b+24):0;
    ok=ok && count>=1 && count<=24 && configs<=L4_OPERATION_CONFIG_LIMIT && 28ull+8ull*configs+32ull*count==size;
    if(ok)for(unsigned i=0;i<2;i++){
        ok=ok && l4_store_get64(b+28+8*configs+8*i)==p->switch_sequence[i];bool found=false;
        for(unsigned k=0;k<configs;k++)if(l4_store_get64(b+28+8*k)==p->config_sequence[i])found=true;
        ok=ok && found;
    }
    ok=ok && l4_store_get64(b+28+8*configs+16)==p->con_sequence;
    free(b);if(!ok)return fail(ERROR_REVISION_MISMATCH);
    for(unsigned i=0;i<2;i++)if(!same_record(j,10,p->switch_sequence[i],p->switches[i],p->switch_size[i]) ||
        !same_record(j,20,p->config_sequence[i],p->configs[i],p->config_size[i]) || !l4_config_verify(j,p->config_sequence[i],false))return false;
    return same_record(j,10,p->con_sequence,p->con_switch,p->con_size);
}
typedef struct {const L4CommunicationPin* pin;unsigned found;} VerifiedPlan;
static bool verify_intent(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){
    (void)sequence;VerifiedPlan* v=context;if(kind!=70)return true;
    if(v->found++ || size!=v->pin->size || memcmp(bytes,v->pin->bytes,size))return fail(ERROR_REVISION_MISMATCH);return true;
}
bool l4_communication_plan_verify_journal(L4Journal* j,const L4CommunicationPin* pin){
    if(!j || !pin || j->poisoned || !j->lock || j->lock==INVALID_HANDLE_VALUE || memcmp(&j->layout,&pin->roots,sizeof(j->layout)) ||
        memcmp(j->header+8,&pin->plan.operation,16))return fail(ERROR_INVALID_PARAMETER);
    VerifiedPlan v={pin,0};return l4_journal_replay(j,verify_intent,&v) && v.found==1 && operation_binding(j,&pin->plan)?true:fail(ERROR_REVISION_MISMATCH);
}
bool l4_communication_plan_prepare(L4Journal* j,const L4CommunicationPlan* p,HANDLE worker){
    if(!j || j->poisoned || !j->lock || j->lock==INVALID_HANDLE_VALUE || !p || !worker || memcmp(&p->operation,j->header+8,16))return fail(ERROR_INVALID_PARAMETER);
    FILETIME created,exit,kernel,user;DWORD code=0;
    if(GetProcessId(worker)!=p->worker_pid || !GetProcessTimes(worker,&created,&exit,&kernel,&user) ||
       CompareFileTime(&created,&p->worker_created) || !GetExitCodeProcess(worker,&code) || code!=STILL_ACTIVE ||
       WaitForSingleObject(worker,0)!=WAIT_TIMEOUT)return fail(ERROR_REVISION_MISMATCH);
    HANDLE supervisor=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,p->supervisor_pid);
    if(!supervisor)return false;
    bool supervisor_ok=GetProcessTimes(supervisor,&created,&exit,&kernel,&user) && !CompareFileTime(&created,&p->supervisor_created) &&
        WaitForSingleObject(supervisor,0)==WAIT_TIMEOUT;CloseHandle(supervisor);
    if(!supervisor_ok)return fail(ERROR_REVISION_MISMATCH);
    L4UpdateState clear;if(!l4_update_state_read(&j->layout,&clear))return false;
    if(clear.window || clear.generation==~0ull || p->generation!=clear.generation+1)return fail(ERROR_REVISION_MISMATCH);
    FILETIME now;GetSystemTimeAsFileTime(&now);ULONGLONG utc=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;
    if(utc<p->armed_utc || utc>=p->deadline_utc)return fail(ERROR_TIME_SKEW);
    BYTE* bytes=NULL;DWORD size=0;if(!l4_communication_plan_encode(&j->layout,p,&bytes,&size))return false;
    bool ok=operation_binding(j,p);L4FileFence base={0},parents={0};
    if(ok)ok=l4_update_state_pin(&j->layout,&base) && l4_store_pin(j->directory,j->directory,true,&parents);
    HANDLE file=INVALID_HANDLE_VALUE;BYTE* existing=NULL;DWORD existing_size=0;bool published=false;
    if(ok && read_file(j->directory,L"communication.recovery",L4_COMMUNICATION_PLAN_LIMIT,&file,&existing,&existing_size)){
        published=existing_size==size && !memcmp(existing,bytes,size);CloseHandle(file);free(existing);
        if(!published){ok=false;SetLastError(ERROR_REVISION_MISMATCH);}
    }else if(ok && GetLastError()!=ERROR_FILE_NOT_FOUND)ok=false;
    if(ok)ok=immutable_file(j->directory,L"communication.decision.lock",NULL,0) && immutable_file(j->directory,L"communication.runner.lock",NULL,0);
    if(ok && !published)ok=l4_journal_append(j,70,bytes,size,NULL) && immutable_file(j->directory,L"communication.recovery",bytes,size);
    DWORD error=GetLastError();l4_store_unpin(&parents);l4_store_unpin(&base);free(bytes);return ok?true:fail(error?error:ERROR_INVALID_DATA);
}
#endif /* trusted producer; supervisor reader never publishes a plan */
void l4_communication_plan_close(L4CommunicationPin* pin){if(!pin)return;if(pin->file!=INVALID_HANDLE_VALUE)CloseHandle(pin->file);
    l4_store_unpin(&pin->parents);l4_store_unpin(&pin->base);free(pin->bytes);free(pin);}
bool l4_communication_plan_open(const L4Layout* roots,const wchar_t* operation,L4CommunicationPin** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;if(!roots || !operation || wcslen(operation)!=36)return fail(ERROR_INVALID_PARAMETER);
    wchar_t uuid[40],canonical[40],directory[MAX_PATH];GUID id;swprintf_s(uuid,40,L"{%ls}",operation);
    if(FAILED(CLSIDFromString(uuid,&id)) || StringFromGUID2(&id,canonical,40)!=39 || _wcsnicmp(canonical+1,operation,36))return fail(ERROR_INVALID_NAME);
    L4CommunicationPin* pin=calloc(1,sizeof(*pin));if(!pin)return fail(ERROR_NOT_ENOUGH_MEMORY);pin->file=INVALID_HANDLE_VALUE;pin->roots=*roots;
    bool ok=swprintf_s(directory,MAX_PATH,L"%ls\\%ls",roots->operations,operation)>0 && l4_update_state_pin(roots,&pin->base) && l4_store_pin(directory,directory,true,&pin->parents);
    if(ok)ok=read_file(directory,L"communication.recovery",L4_COMMUNICATION_PLAN_LIMIT,&pin->file,&pin->bytes,&pin->size) &&
        l4_communication_plan_decode(roots,pin->bytes,pin->size,&pin->plan) && !memcmp(&id,&pin->plan.operation,16);
    DWORD error=GetLastError();if(!ok){l4_communication_plan_close(pin);return fail(error?error:ERROR_INVALID_DATA);}*result=pin;return true;
}
const L4CommunicationPlan* l4_communication_pinned_plan(const L4CommunicationPin* pin){return pin?&pin->plan:NULL;}
bool l4_communication_plan_matches(const L4CommunicationPin* pin,const L4UpdateState* state){
    if(!pin || !state || state->window!=L4_UPDATE_COMMUNICATION)return fail(ERROR_INVALID_PARAMETER);
    const L4CommunicationPlan* p=&pin->plan;char owner[40]={0};const GUID* g=&p->operation;
    snprintf(owner,40,"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",g->Data1,g->Data2,g->Data3,
        g->Data4[0],g->Data4[1],g->Data4[2],g->Data4[3],g->Data4[4],g->Data4[5],g->Data4[6],g->Data4[7]);
    return !memcmp(owner,state->owner,40) && state->generation==p->generation && state->plan_sequence==p->sequence &&
        state->deadline_utc==p->deadline_utc?true:fail(ERROR_REVISION_MISMATCH);
}

struct L4CommunicationDecision {L4CommunicationPin* pin;wchar_t directory[MAX_PATH];HANDLE lock,runner;bool locked,running;};
void l4_communication_decision_release(L4CommunicationDecision* d){if(d && d->locked){OVERLAPPED io={0};UnlockFileEx(d->lock,0,1,0,&io);d->locked=false;}}
bool l4_communication_decision_relock(L4CommunicationDecision* d,DWORD timeout){
    if(!d || d->locked || !timeout || timeout>300000)return fail(ERROR_INVALID_PARAMETER);ULONGLONG deadline=GetTickCount64()+timeout;
    while(!d->locked){OVERLAPPED io={0};d->locked=LockFileEx(d->lock,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&io)!=0;
        if(d->locked)break;DWORD error=GetLastError();if(error!=ERROR_LOCK_VIOLATION)return false;
        ULONGLONG now=GetTickCount64();if(now>=deadline)return fail(ERROR_TIMEOUT);Sleep(deadline-now<10?(DWORD)(deadline-now):10);}
    if(GetTickCount64()>=deadline){l4_communication_decision_release(d);return fail(ERROR_TIMEOUT);}return true;
}
void l4_communication_decision_close(L4CommunicationDecision* d){if(!d)return;l4_communication_decision_release(d);
    if(d->running){OVERLAPPED io={0};UnlockFileEx(d->runner,0,1,0,&io);}if(d->runner!=INVALID_HANDLE_VALUE)CloseHandle(d->runner);
    if(d->lock!=INVALID_HANDLE_VALUE)CloseHandle(d->lock);l4_communication_plan_close(d->pin);free(d);
}
static bool open_lock(const wchar_t* directory,const wchar_t* leaf,HANDLE* result){
    wchar_t full[MAX_PATH];if(!path(directory,leaf,full))return fail(ERROR_FILENAME_EXCED_RANGE);
    *result=CreateFileW(full,GENERIC_READ|GENERIC_WRITE|READ_CONTROL,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    DWORD size=0;return *result!=INVALID_HANDLE_VALUE && checked(*result,full,0,&size);
}
bool l4_communication_decision_open(const L4Layout* roots,const wchar_t* operation,DWORD timeout,L4CommunicationDecision** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;if(!timeout || timeout>300000)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG deadline=GetTickCount64()+timeout;L4CommunicationDecision* d=calloc(1,sizeof(*d));if(!d)return fail(ERROR_NOT_ENOUGH_MEMORY);
    d->lock=d->runner=INVALID_HANDLE_VALUE;bool ok=l4_communication_plan_open(roots,operation,&d->pin);
    if(ok)ok=swprintf_s(d->directory,MAX_PATH,L"%ls\\%ls",roots->operations,operation)>0 &&
        open_lock(d->directory,L"communication.decision.lock",&d->lock) && open_lock(d->directory,L"communication.runner.lock",&d->runner);
    if(ok){ULONGLONG now=GetTickCount64();if(now>=deadline)ok=fail(ERROR_TIMEOUT);else ok=l4_communication_decision_relock(d,(DWORD)(deadline-now));}
    DWORD error=GetLastError();if(!ok){l4_communication_decision_close(d);return fail(error?error:ERROR_INVALID_DATA);}*result=d;return true;
}
static bool phase_valid(L4CommunicationPhase phase,DWORD error){return phase>=L4_COMM_DEC_STARTED && phase<=L4_COMM_DEC_FAILED &&
    ((phase==L4_COMM_DEC_UNCONFIRMED || phase==L4_COMM_DEC_FAILED)?error!=0:error==0);}
bool l4_communication_decision_read(L4CommunicationDecision* d,L4CommunicationPhase* phase,DWORD* error){
    if(!d || !d->locked || !phase || !error)return fail(ERROR_INVALID_PARAMETER);*phase=L4_COMM_DEC_WAIT;*error=0;
    HANDLE file;BYTE* b=NULL;DWORD size=0;if(!read_file(d->directory,L"communication.result",96,&file,&b,&size))return GetLastError()==ERROR_FILE_NOT_FOUND;
    const L4CommunicationPlan* p=&d->pin->plan;BYTE digest[32];
    bool ok=size==96 && !memcmp(b,"L4CMD01",8) && !memcmp(b+8,&p->operation,16) &&
        !memcmp(b+24,d->pin->bytes+d->pin->size-32,32) && l4_store_hash(b,64,NULL,0,digest) && !memcmp(digest,b+64,32);
    if(ok){*phase=(L4CommunicationPhase)l4_store_get32(b+56);*error=l4_store_get32(b+60);ok=phase_valid(*phase,*error);}
    free(b);CloseHandle(file);return ok?true:fail(ERROR_INVALID_DATA);
}
static bool decision_publish(L4CommunicationDecision* d,L4CommunicationPhase phase,DWORD error){
    BYTE b[96]={0};memcpy(b,"L4CMD01",8);memcpy(b+8,&d->pin->plan.operation,16);memcpy(b+24,d->pin->bytes+d->pin->size-32,32);
    l4_store_u32(b+56,phase);l4_store_u32(b+60,error);if(!l4_store_hash(b,64,NULL,0,b+64))return false;
    GUID id;wchar_t uuid[40],temp[MAX_PATH],target[MAX_PATH];
    if(FAILED(CoCreateGuid(&id)) || StringFromGUID2(&id,uuid,40)!=39 || swprintf_s(temp,MAX_PATH,L"%ls\\.%ls.communication.tmp",d->directory,uuid)<0 ||
       !path(d->directory,L"communication.result",target))return fail(ERROR_INVALID_NAME);
    HANDLE file;if(!private_create(temp,&file))return false;bool ok=l4_store_write(file,b,96) && FlushFileBuffers(file);DWORD code=GetLastError();if(!ok)discard(file);CloseHandle(file);
    if(ok){ok=MoveFileExW(temp,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;code=GetLastError();if(!ok)DeleteFileW(temp);}return ok?true:fail(code);
}
static bool decision_state(L4CommunicationDecision* d){
    L4UpdateState state;return l4_update_state_read(&d->pin->roots,&state) && l4_communication_plan_matches(d->pin,&state);
}
bool l4_communication_decision_begin(L4CommunicationDecision* d,ULONGLONG now,bool boot){
    L4CommunicationPhase phase;DWORD error;if(!l4_communication_decision_read(d,&phase,&error))return false;
    if(!decision_state(d))return false;
    const L4CommunicationPlan* p=&d->pin->plan;
    if(d->running || (phase!=L4_COMM_DEC_WAIT && !(phase==L4_COMM_DEC_STARTED && boot)))return fail(ERROR_INVALID_STATE);
    if(!boot && (now<p->armed_utc || now<p->deadline_utc))return fail(ERROR_NOT_READY);
    OVERLAPPED io={0};if(!LockFileEx(d->runner,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&io))return false;
    d->running=true;if(phase==L4_COMM_DEC_STARTED || decision_publish(d,L4_COMM_DEC_STARTED,0))return true;
    DWORD code=GetLastError();UnlockFileEx(d->runner,0,1,0,&io);d->running=false;return fail(code);
}
bool l4_communication_decision_finish(L4CommunicationDecision* d,L4CommunicationPhase desired,DWORD error,ULONGLONG now){
    if(!phase_valid(desired,error) || desired==L4_COMM_DEC_STARTED)return fail(ERROR_INVALID_PARAMETER);
    L4CommunicationPhase phase;DWORD previous;if(!l4_communication_decision_read(d,&phase,&previous))return false;
    if(phase==desired && error==previous)return true;
    if(!decision_state(d))return false;
    const L4CommunicationPlan* p=&d->pin->plan;
    if(desired==L4_COMM_DEC_COMMITTED){if(phase!=L4_COMM_DEC_WAIT || now<p->armed_utc || now>=p->deadline_utc)return fail(ERROR_INVALID_STATE);}
    else if(phase!=L4_COMM_DEC_STARTED || !d->running)return fail(ERROR_INVALID_STATE);
    return decision_publish(d,desired,error);
}
