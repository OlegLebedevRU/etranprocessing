/* FM filesystem/WinHTTP work lives in an owned child Job. Killing a blocked FM
 * operation never terminates l4con's MQTT/console thread or another suite tool. */
#include "fm_process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../leo4proxy/src/policy_json.h"

#define FM_IPC_MAGIC 0x464D0002
#define FM_RESULTS 4

typedef struct { int kind,status;char id[40],body[24576]; } FmMessage;
typedef struct {
    DWORD magic;bool pending,navigation,console_busy,busy;
    ULONGLONG heartbeat;
    RpcCommand command;
    char request[8193];
    unsigned read_index,write_index;
    FmMessage results[FM_RESULTS];
} FmShared;
typedef struct {
    HANDLE mapping,mutex,wake,stop,process,job;
    FmShared* shared;
    bool initialized,connected,terminating;
    ULONGLONG retry_at,deadline;
    char sn[128],lease[64];int port;
    FmResult result;FmNavigationResult navigation_result;void* context;
} FmHost;
static FmHost host;

static bool lock_shared(HANDLE mutex) { return WaitForSingleObject(mutex,100)==WAIT_OBJECT_0; }
static bool close_child(void) {
    /* Always destroy only our Job, even when filesystem or WinHTTP never returns. */
    host.terminating=true;
    if(host.job)TerminateJobObject(host.job,ERROR_CANCELLED);
    if(host.process && WaitForSingleObject(host.process,2000)!=WAIT_OBJECT_0)return false;
    if(host.shared)UnmapViewOfFile(host.shared);
    HANDLE handles[]={host.process,host.job,host.mapping,host.mutex,host.wake,host.stop};
    for(unsigned i=0;i<sizeof(handles)/sizeof(handles[0]);i++)if(handles[i])CloseHandle(handles[i]);
    host.process=host.job=host.mapping=host.mutex=host.wake=host.stop=NULL;
    host.shared=NULL;host.lease[0]=0;host.deadline=0;
    host.retry_at=GetTickCount64()+1000;
    host.terminating=false;return true;
}
static bool start_child(void) {
    SECURITY_ATTRIBUTES security={sizeof(security),NULL,TRUE};
    host.mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,sizeof(FmShared),NULL);
    host.mutex=CreateMutexW(&security,FALSE,NULL);host.wake=CreateEventW(&security,FALSE,FALSE,NULL);
    host.stop=CreateEventW(&security,TRUE,FALSE,NULL);host.job=CreateJobObjectW(NULL,NULL);
    host.shared=host.mapping?(FmShared*)MapViewOfFile(host.mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(FmShared)):NULL;
    if(!host.shared || !host.mutex || !host.wake || !host.stop || !host.job) {close_child();return false;}
    ZeroMemory(host.shared,sizeof(FmShared));host.shared->magic=FM_IPC_MAGIC;host.shared->heartbeat=GetTickCount64();
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit=1;
    if(!SetInformationJobObject(host.job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {close_child();return false;}
    wchar_t exe[MAX_PATH],command[2048];
    if(!GetModuleFileNameW(NULL,exe,MAX_PATH)) {close_child();return false;}
    swprintf_s(command,2048,L"\"%s\" --fm-worker %llu %llu %llu %llu %S %d",exe,
        (unsigned long long)(ULONG_PTR)host.mapping,(unsigned long long)(ULONG_PTR)host.mutex,
        (unsigned long long)(ULONG_PTR)host.wake,(unsigned long long)(ULONG_PTR)host.stop,host.sn,host.port);
    STARTUPINFOEXW startup={0};startup.StartupInfo.cb=sizeof(startup);
    SIZE_T bytes=0;InitializeProcThreadAttributeList(NULL,1,0,&bytes);
    startup.lpAttributeList=(LPPROC_THREAD_ATTRIBUTE_LIST)malloc(bytes);
    HANDLE inherited[]={host.mapping,host.mutex,host.wake,host.stop};
    bool attributes=startup.lpAttributeList && InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes);
    bool ok=attributes && UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),NULL,NULL);
    PROCESS_INFORMATION process={0};
    if(ok)ok=CreateProcessW(exe,command,NULL,NULL,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,NULL,NULL,&startup.StartupInfo,&process)!=0;
    if(attributes)DeleteProcThreadAttributeList(startup.lpAttributeList);free(startup.lpAttributeList);
    if(ok) {
        host.process=process.hProcess;
        ok=AssignProcessToJobObject(host.job,process.hProcess)!=0;
        if(ok)ok=ResumeThread(process.hThread)!=(DWORD)-1;
        if(!ok)TerminateProcess(process.hProcess,ERROR_CANCELLED);
        CloseHandle(process.hThread);
    }
    if(!ok)close_child();return ok;
}

bool fm_start(const char* sn,int port,HANDLE stop,FmResult result,void* context) {
    (void)stop;ZeroMemory(&host,sizeof(host));strcpy_s(host.sn,sizeof(host.sn),sn);
    host.port=port;host.result=result;host.context=context;host.initialized=true;return true;
}
void fm_set_navigation_result(FmNavigationResult result) {host.navigation_result=result;}
void fm_connection(bool connected) {
    host.connected=connected;
    if(!connected)close_child();
    else if(!host.process)start_child();
}
bool fm_busy(void) {
    if(host.terminating)return true;
    if(!host.shared)return false;
    if(!lock_shared(host.mutex))return true;
    bool busy=host.shared->busy || host.shared->pending;ReleaseMutex(host.mutex);return busy;
}
void fm_tick(void) {
    if(!host.initialized || !host.connected)return;
    if(host.terminating) {close_child();return;}
    if(!host.process) {if(GetTickCount64()>=host.retry_at)start_child();return;}
    if(WaitForSingleObject(host.process,0)==WAIT_OBJECT_0 || (host.deadline && GetTickCount64()>=host.deadline)) {close_child();return;}
    if(!lock_shared(host.mutex)) {close_child();return;}
    bool stalled=GetTickCount64()-host.shared->heartbeat>5000;
    if(!host.shared->busy && !host.shared->pending) {host.lease[0]=0;host.deadline=0;}
    FmMessage message;bool has=host.shared->read_index!=host.shared->write_index;
    if(has) {message=host.shared->results[host.shared->read_index%FM_RESULTS];host.shared->read_index++;}
    ReleaseMutex(host.mutex);
    if(stalled) {close_child();return;}
    if(has && rpc_uuid(message.id) && memchr(message.body,0,sizeof(message.body))) {
        if(message.kind==1)host.result(host.context,message.id,message.status,message.body);
        else if(message.kind==2 && host.navigation_result)host.navigation_result(host.context,message.id,message.body);
    }
}
bool fm_enqueue(const RpcCommand* command,bool console_busy) {
    if(!host.shared || console_busy || host.terminating)return false;
    if(command->method!=7021 && !(command->method==7023 && (!strcmp(command->fm_action,"start") || !strcmp(command->fm_action,"renew"))))return false;
    if(host.lease[0] && strcmp(command->session_id,host.lease))return false;
    if(command->method==7023 && !strcmp(command->fm_action,"renew") && !host.lease[0])return false;
    if(!lock_shared(host.mutex))return false;
    bool ok=!host.shared->pending;
    if(ok) {
        host.shared->command=*command;host.shared->navigation=false;host.shared->console_busy=console_busy;host.shared->pending=true;
        if(command->method==7023 && (!strcmp(command->fm_action,"start") || !strcmp(command->fm_action,"renew"))) {
            unsigned long long now=(unsigned long long)time(NULL),expiry=command->fm_expires_at;
            if(expiry<=now || expiry-now>90)ok=false;
            else {strcpy_s(host.lease,sizeof(host.lease),command->session_id);host.deadline=GetTickCount64()+(expiry-now)*1000;}
        }
        if(!ok)host.shared->pending=false;
    }
    ReleaseMutex(host.mutex);if(ok)SetEvent(host.wake);return ok;
}
bool fm_navigation(const char* payload,size_t length,bool console_busy) {
    if(host.terminating || console_busy || length>8192 || !length)return false;
    PolicyJson json;char action[16],id[40],lease[64];unsigned long long version=0,expires=0;
    if(!policy_json_parse(&json,payload,length) ||
       !policy_json_uint(&json,policy_json_field(&json,0,"v"),&version) || version!=2 ||
       !policy_json_string(&json,policy_json_field(&json,0,"action"),action,sizeof(action)))return false;
    if(!strcmp(action,"stop")) {
        if(!policy_json_string(&json,policy_json_field(&json,0,"command_id"),id,sizeof(id)) || !rpc_uuid(id) ||
           !policy_json_string(&json,policy_json_field(&json,0,"lease_id"),lease,sizeof(lease)) || !rpc_uuid(lease) ||
           !policy_json_uint(&json,policy_json_field(&json,0,"expires_at"),&expires) ||
           expires<=(unsigned long long)time(NULL) || expires>(unsigned long long)time(NULL)+10 ||
           (host.lease[0] && strcmp(host.lease,lease)))return false;
        if(!close_child())return false;
        char response[256];snprintf(response,sizeof(response),"{\"v\":2,\"command_id\":\"%s\",\"lease_id\":\"%s\",\"state\":\"completed\"}",id,lease);
        return host.navigation_result && host.navigation_result(host.context,id,response);
    }
    if(!host.shared)return false;
    if(!lock_shared(host.mutex))return false;
    bool ok=!host.shared->pending;
    if(ok) {memcpy(host.shared->request,payload,length);host.shared->request[length]=0;host.shared->navigation=true;host.shared->console_busy=console_busy;host.shared->pending=true;}
    ReleaseMutex(host.mutex);if(ok)SetEvent(host.wake);return ok;
}
void fm_shutdown(void) {close_child();host.initialized=false;host.connected=false;}

/* Child-side transport never calls into the parent's MQTT state. */
typedef struct {FmShared* shared;HANDLE mutex,stop;} Child;
static bool child_result(Child* child,int kind,const char* id,int status,const char* body) {
    if(strlen(body)>=24576 || !lock_shared(child->mutex)) {SetEvent(child->stop);return false;}
    bool ok=child->shared->write_index-child->shared->read_index<FM_RESULTS;
    if(ok) {
        FmMessage* result=&child->shared->results[child->shared->write_index%FM_RESULTS];
        result->kind=kind;result->status=status;strcpy_s(result->id,sizeof(result->id),id);strcpy_s(result->body,sizeof(result->body),body);
        child->shared->write_index++;
    }
    ReleaseMutex(child->mutex);if(!ok)SetEvent(child->stop);return ok;
}
static void child_rpc(void* context,const char* id,int status,const char* code) {child_result((Child*)context,1,id,status,code);}
static bool child_navigation(void* context,const char* id,const char* body) {return child_result((Child*)context,2,id,200,body);}
int fm_worker_main(int argc,wchar_t** argv) {
    if(argc!=8)return 2;
    HANDLE mapping=(HANDLE)(ULONG_PTR)_wcstoui64(argv[2],NULL,10),mutex=(HANDLE)(ULONG_PTR)_wcstoui64(argv[3],NULL,10);
    HANDLE wake=(HANDLE)(ULONG_PTR)_wcstoui64(argv[4],NULL,10),stop=(HANDLE)(ULONG_PTR)_wcstoui64(argv[5],NULL,10);
    int port=_wtoi(argv[7]);char sn[128];
    if(port<1 || port>65535 || !WideCharToMultiByte(CP_UTF8,0,argv[6],-1,sn,sizeof(sn),NULL,NULL))return 2;
    FmShared* shared=(FmShared*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(FmShared));
    if(!shared || shared->magic!=FM_IPC_MAGIC)return 2;
    Child child={shared,mutex,stop};
    if(!fm_child_start(sn,port,stop,child_rpc,&child))return 3;
    fm_child_set_navigation_result(child_navigation);fm_child_connection(true);
    while(WaitForSingleObject(stop,0)!=WAIT_OBJECT_0) {
        fm_child_tick();
        RpcCommand command;char request[8193];bool pending=false,navigation=false,console_busy=false;
        if(!lock_shared(mutex))break;
        shared->heartbeat=GetTickCount64();pending=shared->pending;
        if(pending) {command=shared->command;memcpy(request,shared->request,sizeof(request));navigation=shared->navigation;console_busy=shared->console_busy;shared->pending=false;}
        /* Keep busy asserted while moving a command from IPC to the worker. */
        shared->busy=pending || fm_child_busy();ReleaseMutex(mutex);
        if(pending) {
            if(navigation)fm_child_navigation(request,strlen(request),console_busy);
            else if(!fm_child_enqueue(&command,console_busy))child_rpc(&child,command.task_id,409,"fm_busy_or_disabled");
        }
        WaitForSingleObject(wake,100);
    }
    SetEvent(stop);fm_child_shutdown();
    /* The process now exits; no callback/worker can outlive this IPC mapping. */
    return 0;
}
