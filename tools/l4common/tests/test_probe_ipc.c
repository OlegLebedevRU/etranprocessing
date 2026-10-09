#include "../probe_ipc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sddl.h>
#include <limits.h>
#ifndef L4_PROBE_PREFIX
#define L4_PROBE_PREFIX L"L4HealthTest"
#endif
static volatile LONG calls;
typedef struct {LONG health,fresh;LONG not_ready;DWORD health_error,fresh_error,delay;} BarrierFixture;
static DWORD barrier_callback(DWORD mode,DWORD timeout,HANDLE cancel,void* context){
    BarrierFixture* f=context;
    if(!mode){LONG count=InterlockedIncrement(&f->health);if(f->health_error)return f->health_error;
        return count<=f->not_ready?ERROR_NOT_READY:ERROR_SUCCESS;}
    assert(mode==1 && f->health>f->not_ready);InterlockedIncrement(&f->fresh);
    if(f->delay)WaitForSingleObject(cancel,timeout+f->delay);return f->fresh_error;
}
static DWORD dying_callback(DWORD mode,DWORD timeout,HANDLE cancel,void* context){
    (void)timeout;(void)cancel;(void)context;assert(!mode);
    if(InterlockedIncrement(&calls)==2)ExitProcess(0);return ERROR_NOT_READY;
}
static void test_barrier_process(void){
    wchar_t image[MAX_PATH],command[MAX_PATH+40];assert(GetModuleFileNameW(NULL,image,MAX_PATH));
    swprintf_s(command,MAX_PATH+40,L"\"%ls\" --barrier-child",image);STARTUPINFOW startup={sizeof(startup)};PROCESS_INFORMATION child={0};
    assert(CreateProcessW(image,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&child));CloseHandle(child.hThread);
    /* A pipe belonging to another actual process cannot satisfy our epoch. */
    assert(!l4_probe_barrier_call(GetCurrentProcessId(),2000));assert(GetLastError()==ERROR_REVISION_MISMATCH);
    assert(!l4_probe_barrier_call(child.dwProcessId,2000));
    assert(WaitForSingleObject(child.hProcess,3000)==WAIT_OBJECT_0);DWORD exit_code=1;assert(GetExitCodeProcess(child.hProcess,&exit_code) && !exit_code);CloseHandle(child.hProcess);
    puts("Barrier: actual child pipe PID mismatch and original process death refuse advancement PASS");
}
static void test_barrier(void){
    BarrierFixture f={0};L4ProbeServer* server=NULL;DWORD pid=GetCurrentProcessId();
    assert(l4_probe_server_start(L"con",barrier_callback,&f,&server));
    f.not_ready=3;assert(l4_probe_barrier_call(pid,2000));assert(f.health==4 && f.fresh==1);
    f=(BarrierFixture){0};f.not_ready=LONG_MAX;ULONGLONG start=GetTickCount64();
    assert(!l4_probe_barrier_call(pid,150));assert(GetLastError()==ERROR_TIMEOUT);assert(f.health>1 && !f.fresh && GetTickCount64()-start<600);
    f=(BarrierFixture){0};f.health_error=ERROR_ACCESS_DENIED;
    assert(!l4_probe_barrier_call(pid,1000));assert(GetLastError()==ERROR_ACCESS_DENIED && f.health==1 && !f.fresh);
    f=(BarrierFixture){0};f.not_ready=2;f.fresh_error=ERROR_NOT_READY;
    assert(!l4_probe_barrier_call(pid,1000));assert(GetLastError()==ERROR_NOT_READY && f.health==3 && f.fresh==1);
    f=(BarrierFixture){0};f.not_ready=2;f.delay=20;start=GetTickCount64();
    assert(!l4_probe_barrier_call(pid,200));assert(GetLastError()==ERROR_TIMEOUT && f.health==3 && f.fresh==1 && GetTickCount64()-start<600);
    l4_probe_server_stop(server);
    assert(!l4_probe_barrier_call(0,100));assert(GetLastError()==ERROR_INVALID_PARAMETER);
    assert(!l4_probe_barrier_call(pid,0));assert(GetLastError()==ERROR_INVALID_PARAMETER);
    assert(!l4_probe_barrier_call(pid,300001));assert(GetLastError()==ERROR_INVALID_PARAMETER);
    puts("Barrier: bounded NOT_READY health wait, one fresh exchange, hard failure and late response refusal PASS");
}
static DWORD evidence_callback(DWORD timeout,HANDLE cancel,void* context,L4LinkProbeEvidence* evidence){
    (void)timeout;(void)cancel;assert(context==&calls);InterlockedIncrement(&calls);memset(evidence,0,sizeof(*evidence));return ERROR_NOT_READY;
}
static DWORD callback(DWORD mode,DWORD timeout,HANDLE cancel,void* context){
    assert(context==&calls);InterlockedIncrement(&calls);if(!mode)return ERROR_SUCCESS;
    WaitForSingleObject(cancel,timeout+20);return ERROR_SUCCESS; /* Deliberately late success. */
}
static DWORD drain_callback(const L4UpdateState* expected,DWORD timeout,HANDLE cancel,void* context){
    (void)timeout;(void)cancel;assert(context==&calls);
    return expected->generation==7?ERROR_SUCCESS:ERROR_REVISION_MISMATCH;
}
static DWORD WINAPI wrong_echo_server(void* context){
    HANDLE pipe=context;assert(ConnectNamedPipe(pipe,NULL) || GetLastError()==ERROR_PIPE_CONNECTED);
    BYTE request[88],response[76]={0};DWORD count=0;
    assert(ReadFile(pipe,request,sizeof(request),&count,NULL) && count==sizeof(request));
    memcpy(response+4,request+16,72);response[52]^=1; /* Success for a different generation. */
    assert(WriteFile(pipe,response,sizeof(response),&count,NULL) && count==sizeof(response));
    BYTE receipt;ReadFile(pipe,&receipt,1,&count,NULL); /* Client refuses and closes. */
    DisconnectNamedPipe(pipe);return 0;
}
static void test_wrong_echo(const L4UpdateState* expected,bool recovery){
    wchar_t name[128];swprintf_s(name,128,L"\\\\.\\pipe\\%ls_superv_Health_v1",L4_PROBE_PREFIX);
    PSECURITY_DESCRIPTOR sd=NULL;assert(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",SDDL_REVISION_1,&sd,NULL));
    SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};HANDLE pipe=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,128,128,0,&sa);LocalFree(sd);assert(pipe!=INVALID_HANDLE_VALUE);
    HANDLE thread=CreateThread(NULL,0,wrong_echo_server,pipe,0,NULL);assert(thread);
    assert(!(recovery?l4_probe_recovery_call(GetCurrentProcessId(),expected,1000):l4_probe_drain_call(L"superv",GetCurrentProcessId(),expected,1000)));assert(GetLastError()==ERROR_REVISION_MISMATCH);
    assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);CloseHandle(thread);CloseHandle(pipe);
}
typedef struct {HANDLE pipe;bool wrong_nonce;} EvidenceFixture;
static DWORD WINAPI evidence_reply_fixture(void* context){
    EvidenceFixture* f=context;assert(ConnectNamedPipe(f->pipe,NULL) || GetLastError()==ERROR_PIPE_CONNECTED);
    BYTE request[56],response[44+L4_LINK_EVIDENCE_BYTES]={0};DWORD n=0;assert(ReadFile(f->pipe,request,sizeof(request),&n,NULL) && n==sizeof(request));
    memcpy(response+4,request+16,40);if(f->wrong_nonce)response[4]^=1;
    L4LinkProbeEvidence e={0};strcpy_s(e.request_nonce,40,"11111111-1111-4111-8111-111111111111");strcpy_s(e.rsp_correlation,40,e.request_nonce);strcpy_s(e.eva_request_nonce,40,e.request_nonce);
    strcpy_s(e.event_nonce,40,"22222222-2222-4222-8222-222222222222");strcpy_s(e.eva_correlation,40,e.event_nonce);
    e.req_sent_utc=100;e.rsp_received_utc=200;e.evt_sent_utc=300;e.eva_received_utc=400;e.event_id=e.echo_event_id=123;
    assert(l4_link_evidence_encode(&e,response+44));assert(WriteFile(f->pipe,response,sizeof(response),&n,NULL) && n==sizeof(response));
    BYTE receipt;ReadFile(f->pipe,&receipt,1,&n,NULL);DisconnectNamedPipe(f->pipe);return 0;
}
static void test_evidence_replay(bool wrong_nonce){
    wchar_t name[128];swprintf_s(name,128,L"\\\\.\\pipe\\%ls_con_Health_v1",L4_PROBE_PREFIX);
    EvidenceFixture f={0};f.wrong_nonce=wrong_nonce;f.pipe=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,512,128,0,NULL);assert(f.pipe!=INVALID_HANDLE_VALUE);
    HANDLE thread=CreateThread(NULL,0,evidence_reply_fixture,&f,0,NULL);assert(thread);L4LinkProbeEvidence out;
    assert(!l4_probe_evidence_call(GetCurrentProcessId(),1000,&out));assert(GetLastError()==(DWORD)(wrong_nonce?ERROR_REVISION_MISMATCH:ERROR_INVALID_DATA));assert(!out.req_sent_utc);
    assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);CloseHandle(thread);CloseHandle(f.pipe);
}
int main(int argc,char** argv){
    if(argc==2 && !strcmp(argv[1],"--barrier-child")){L4ProbeServer* child=NULL;assert(l4_probe_server_start(L"con",dying_callback,NULL,&child));Sleep(10000);l4_probe_server_stop(child);return 1;}
    L4ProbeServer* server=NULL,*duplicate=NULL;assert(!l4_probe_server_start(L"invalid",callback,(void*)&calls,&server));
    assert(l4_probe_server_start(L"con",callback,(void*)&calls,&server));assert(!l4_probe_server_start(L"con",callback,(void*)&calls,&duplicate));
    assert(!l4_probe_call(L"con",GetCurrentProcessId()+1,0,1000));assert(GetLastError()==ERROR_REVISION_MISMATCH);
    HANDLE primary=NULL,restricted=NULL,impersonation=NULL;assert(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE,&primary));
    BYTE admin[SECURITY_MAX_SID_SIZE];DWORD size=sizeof(admin);assert(CreateWellKnownSid(WinBuiltinAdministratorsSid,NULL,admin,&size));SID_AND_ATTRIBUTES disabled={admin,0};
    assert(CreateRestrictedToken(primary,DISABLE_MAX_PRIVILEGE,1,&disabled,0,NULL,0,NULL,&restricted));
    assert(DuplicateTokenEx(restricted,TOKEN_QUERY|TOKEN_IMPERSONATE,NULL,SecurityImpersonation,TokenImpersonation,&impersonation));
    assert(SetThreadToken(NULL,impersonation));assert(!l4_probe_call(L"con",GetCurrentProcessId(),0,1000));assert(GetLastError()==ERROR_ACCESS_DENIED);
    assert(!l4_probe_barrier_call(GetCurrentProcessId(),1000));assert(GetLastError()==ERROR_ACCESS_DENIED);assert(SetThreadToken(NULL,NULL));
    CloseHandle(impersonation);CloseHandle(restricted);CloseHandle(primary);
    bool ok=l4_probe_call(L"con",GetCurrentProcessId(),0,1000);if(!ok)printf("IPC failure %lu, calls=%ld\n",GetLastError(),calls);assert(ok);assert(calls==1);
    ULONGLONG before=GetTickCount64();assert(!l4_probe_call(L"con",GetCurrentProcessId(),1,100));assert(GetTickCount64()-before<500);
    L4UpdateState expected={0};strcpy_s(expected.owner,40,"17730000-0000-4000-8000-000000000001");
    expected.generation=7;expected.plan_sequence=64;expected.window=1;FILETIME time;GetSystemTimeAsFileTime(&time);expected.deadline_utc=(((ULONGLONG)time.dwHighDateTime<<32)|time.dwLowDateTime)+600000000ull;
    assert(!l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,1000));assert(GetLastError()==ERROR_NOT_SUPPORTED);
    l4_probe_server_stop(server);server=NULL;
    assert(l4_probe_server_start_ex(L"con",callback,drain_callback,(void*)&calls,&server));
    assert(l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,1000));
    test_wrong_echo(&expected,false);test_wrong_echo(&expected,true);
    L4ProbeServer* supervisor=NULL;
    assert(l4_probe_server_start_ex(L"superv",callback,drain_callback,(void*)&calls,&supervisor));
    assert(!l4_probe_recovery_call(GetCurrentProcessId(),&expected,1000));assert(GetLastError()==ERROR_NOT_SUPPORTED);
    l4_probe_server_stop(supervisor);supervisor=NULL;
    assert(l4_probe_server_start_update(L"superv",callback,drain_callback,drain_callback,(void*)&calls,&supervisor));
    assert(l4_probe_recovery_call(GetCurrentProcessId(),&expected,1000));
    expected.generation=8;assert(!l4_probe_recovery_call(GetCurrentProcessId(),&expected,1000));assert(GetLastError()==ERROR_REVISION_MISMATCH);
    expected.generation=7;expected.window=2;assert(!l4_probe_recovery_call(GetCurrentProcessId(),&expected,1000));assert(GetLastError()==ERROR_INVALID_PARAMETER);
    expected.window=1;assert(!l4_probe_recovery_call(GetCurrentProcessId()+1,&expected,1000));assert(GetLastError()==ERROR_REVISION_MISMATCH);
    expected.deadline_utc=1;assert(!l4_probe_recovery_call(GetCurrentProcessId(),&expected,1000));assert(GetLastError()==ERROR_TIMEOUT);
    GetSystemTimeAsFileTime(&time);expected.deadline_utc=(((ULONGLONG)time.dwHighDateTime<<32)|time.dwLowDateTime)+600000000ull;
    l4_probe_server_stop(supervisor);
    expected.generation=8;assert(!l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,1000));assert(GetLastError()==ERROR_REVISION_MISMATCH);
    expected.generation=7;expected.owner[36]='x';assert(!l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,1000));assert(GetLastError()==ERROR_INVALID_PARAMETER);
    expected.owner[36]=0;assert(!l4_probe_drain_call(L"con",GetCurrentProcessId()+1,&expected,1000));assert(GetLastError()==ERROR_REVISION_MISMATCH);
    l4_probe_server_stop(server);assert(!l4_probe_call(L"con",GetCurrentProcessId(),0,50));assert(GetLastError()==ERROR_TIMEOUT);
    assert(l4_probe_server_start_evidence(L"con",callback,drain_callback,NULL,evidence_callback,(void*)&calls,&server));
    L4LinkProbeEvidence evidence;memset(&evidence,0xa5,sizeof(evidence));LONG previous=calls;
    assert(!l4_probe_evidence_call(GetCurrentProcessId()+1,1000,&evidence));assert(GetLastError()==ERROR_REVISION_MISMATCH);assert(calls==previous && !evidence.req_sent_utc);
    assert(!l4_probe_evidence_call(GetCurrentProcessId(),1000,&evidence));
    /* Ordinary BA caller cannot request proof; actual SYSTEM callback refuses
     * incomplete evidence instead. Both leave the output empty. */
    assert(GetLastError()==ERROR_ACCESS_DENIED || GetLastError()==ERROR_NOT_READY);assert(!evidence.req_sent_utc);
    assert(!l4_probe_evidence_call(GetCurrentProcessId(),1000,NULL));assert(GetLastError()==ERROR_INVALID_PARAMETER);
    l4_probe_server_stop(server);
    test_evidence_replay(true);test_evidence_replay(false);test_barrier();test_barrier_process();
    assert(!l4_probe_call(L"con",0,0,100));assert(!l4_probe_call(L"con",GetCurrentProcessId(),2,100));
    puts("Private probe IPC: actual pipe, duplicate/PID refusal, deadline/late success, stop/cleanup, v2 drain/recovery identity echo, unarmed/old supervisor refusal PASS");return 0;
}
