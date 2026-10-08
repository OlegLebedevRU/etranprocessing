#include "update_admission.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifndef L4CON_REMOTE_ADMISSION_ENABLED
/* Deployment still requires the owner's current signed catalog edge. Local
 * acceptance authority is never supplied by an RPC command or environment. */
#define L4CON_REMOTE_ADMISSION_ENABLED 1
#endif
#define ADMISSION_HOST_TIMEOUT 120000u
typedef struct {char task[37],version[32];} Request;
struct L4UpdateAdmission {
    L4Layout roots;HANDLE stop,wake,thread;CRITICAL_SECTION lock;
    bool pending,working,closing;Request request;L4AdmissionReply reply;void* context;
};
static bool fail(DWORD error){SetLastError(error);return false;}
bool update_admission_enabled(void){return L4CON_REMOTE_ADMISSION_ENABLED!=0;}
bool update_admission_busy(L4UpdateAdmission* a){
    if(!a || !update_admission_enabled())return false;
    EnterCriticalSection(&a->lock);bool busy=a->pending || a->working;LeaveCriticalSection(&a->lock);return busy;
}
#if L4CON_REMOTE_ADMISSION_ENABLED
static bool launch(L4UpdateAdmission* a,const Request* request,DWORD* error){
    wchar_t operation[37];L4Journal* journal=NULL;L4RemoteRequest saved={0};L4RemoteHost* host=NULL;L4RemoteHostReceipt receipt={0};
    const char* arch=
#ifdef _WIN64
        "x64";
#else
        "x86";
#endif
    bool ok=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,request->task,-1,operation,37)!=0;
    if(ok)for(unsigned i=0;i<36;i++)if(operation[i]>=L'A' && operation[i]<=L'F')operation[i]+=L'a'-L'A';
    ok=ok && l4_journal_open(&a->roots,operation,true,&journal) &&
        l4_remote_request_save(journal,L4_REMOTE_SUITE,request->version,&saved) &&
        l4_remote_host_launch(&journal,arch,ADMISSION_HOST_TIMEOUT,&host,&receipt);
    DWORD code=GetLastError();
    if(ok && (receipt.request.target!=L4_REMOTE_SUITE || strcmp(receipt.request.version,request->version) ||
        receipt.request.accepted_utc!=saved.accepted_utc || receipt.format_version!=2 || !receipt.updater_version[0] || !receipt.pid || !receipt.birth)){
        ok=false;code=ERROR_INVALID_DATA;
    }
    /* Failed admission is not an update result. Do not invent94/95 or retire a
     * live host: only its authoritative owner may complete/recover the task. */
    if(journal)l4_journal_close(journal);l4_remote_host_close(host);
    *error=ok?0:(code?code:ERROR_NOT_READY);return ok;
}
#endif
static DWORD WINAPI run(void* context){L4UpdateAdmission* a=context;HANDLE waits[]={a->stop,a->wake};
    for(;;){if(WaitForMultipleObjects(2,waits,FALSE,INFINITE)!=WAIT_OBJECT_0+1)break;
        Request request;EnterCriticalSection(&a->lock);
        if(a->closing){LeaveCriticalSection(&a->lock);break;}
        if(!a->pending){LeaveCriticalSection(&a->lock);continue;}
        request=a->request;a->pending=false;a->working=true;LeaveCriticalSection(&a->lock);
        DWORD error=ERROR_NOT_SUPPORTED;bool accepted=false;
#if L4CON_REMOTE_ADMISSION_ENABLED
        if(WaitForSingleObject(a->stop,0)==WAIT_TIMEOUT)accepted=launch(a,&request,&error);
        else error=ERROR_OPERATION_ABORTED;
#endif
        char json[256];int code=accepted?202:503;
        snprintf(json,sizeof(json),"{\"correlationData\":\"%s\",\"operation_id\":\"%s\",\"status_code\":%d,\"status\":\"%s\",\"error\":%lu}",
            request.task,request.task,code,accepted?"controller_accepted":"controller_not_accepted",error);
        /* The owner keeps callback context alive until close joins this thread. */
        a->reply(a->context,request.task,code,json);
        EnterCriticalSection(&a->lock);a->working=false;LeaveCriticalSection(&a->lock);
    }return 0;
}
bool update_admission_start(const L4Layout* roots,HANDLE stop,L4AdmissionReply reply,void* context,L4UpdateAdmission** output){
    if(output)*output=NULL;if(!roots || !stop || !reply || !output)return fail(ERROR_INVALID_PARAMETER);
    L4UpdateAdmission* a=calloc(1,sizeof(*a));if(!a)return fail(ERROR_NOT_ENOUGH_MEMORY);
    a->roots=*roots;a->stop=stop;a->reply=reply;a->context=context;InitializeCriticalSection(&a->lock);
    a->wake=CreateEventW(NULL,FALSE,FALSE,NULL);if(a->wake)a->thread=CreateThread(NULL,0,run,a,0,NULL);
    if(!a->thread){DWORD error=GetLastError();if(a->wake)CloseHandle(a->wake);DeleteCriticalSection(&a->lock);free(a);return fail(error);}
    *output=a;return true;
}
bool update_admission_submit(L4UpdateAdmission* a,const RpcCommand* command){
    if(!a || !command || command->method!=7031 || !memchr(command->task_id,0,sizeof(command->task_id)) ||
        !memchr(command->update_operation_id,0,sizeof(command->update_operation_id)) ||
        !memchr(command->update_version,0,sizeof(command->update_version)) ||
        !memchr(command->update_target,0,sizeof(command->update_target)) || !rpc_uuid(command->task_id) ||
        !_stricmp(command->task_id,"00000000-0000-0000-0000-000000000000") ||
        strcmp(command->update_operation_id,command->task_id) || !rpc_update_version(command->update_version) ||
        (strcmp(command->update_target,"suite") && strcmp(command->update_target,"updater")))return fail(ERROR_INVALID_PARAMETER);
    if(!strcmp(command->update_target,"updater") || !update_admission_enabled())return fail(ERROR_NOT_SUPPORTED);
    EnterCriticalSection(&a->lock);bool ok=!a->closing && !a->pending && !a->working && WaitForSingleObject(a->stop,0)==WAIT_TIMEOUT;
    if(ok){strcpy_s(a->request.task,37,command->task_id);strcpy_s(a->request.version,32,command->update_version);a->pending=true;
        if(!SetEvent(a->wake)){a->pending=false;ok=false;}}
    LeaveCriticalSection(&a->lock);return ok?true:fail(ERROR_BUSY);
}
bool update_admission_close(L4UpdateAdmission** output,DWORD timeout){
    if(!output || !*output)return true;L4UpdateAdmission* a=*output;
    EnterCriticalSection(&a->lock);a->closing=true;a->pending=false;SetEvent(a->wake);LeaveCriticalSection(&a->lock);
    /* A closing wake must terminate even when owner's stop event is not set. */
    if(WaitForSingleObject(a->thread,timeout)!=WAIT_OBJECT_0)return fail(ERROR_TIMEOUT);
    CloseHandle(a->thread);CloseHandle(a->wake);DeleteCriticalSection(&a->lock);free(a);*output=NULL;return true;
}
