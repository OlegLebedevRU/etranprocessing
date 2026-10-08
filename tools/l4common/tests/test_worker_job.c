#include "../worker_job_internal.h"
#include "../journal_internal.h"
#include "../communication_runtime.h"
#include "../../l4rollback/src/rollback.h"
#include <objbase.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures,audits;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL worker Job %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
DWORD rollback_remaining(ULONGLONG deadline){ULONGLONG now=GetTickCount64();return now<deadline?(DWORD)(deadline-now):0;}
static bool gate(void* context){++audits;return *(bool*)context;}
static bool drift_gate(void* context){++audits;JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_BREAKAWAY_OK;return SetInformationJobObject((HANDLE)context,JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=0;}
typedef struct {L4WorkerJob* owner;const L4RecoveryPlan* plan;HANDLE entered,finish;volatile LONG calls;bool result;} Race;
static bool race_gate(void* context){Race* race=context;InterlockedIncrement(&race->calls);SetEvent(race->entered);return WaitForSingleObject(race->finish,5000)==WAIT_OBJECT_0;}
static DWORD WINAPI resume_race(void* context){Race* race=context;race->result=l4_worker_job_resume_checked(race->owner,race->plan,race_gate,race);return 0;}
static void epoch(L4RecoveryPlan* plan,HANDLE process){plan->worker_pid=GetProcessId(process);FILETIME e,k,u;CHECK(GetProcessTimes(process,&plan->worker_created,&e,&k,&u));}
static HANDLE event(const wchar_t* name){return CreateEventW(NULL,TRUE,FALSE,name);}
static bool child_mode(int argc,wchar_t** argv){
    if(argc<4 || (wcscmp(argv[1],L"--child") && wcscmp(argv[1],L"--leaf")))return false;
    bool root=!wcscmp(argv[1],L"--child");if((root && argc!=5) || (!root && argc!=4))ExitProcess(16);
    /* Fresh explicit environment, no inherited parent canary. */
    wchar_t value[32];if(GetEnvironmentVariableW(L"L4_WORKER_PARENT_CANARY",value,32) || !GetEnvironmentVariableW(L"L4_WORKER_TEST",value,32))ExitProcess(12);
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,argv[2]);if(!ready)ExitProcess(13);
    HANDLE job=OpenJobObjectW(JOB_OBJECT_QUERY,FALSE,root?argv[4]:argv[3]);BOOL member=FALSE;
    if(!job || !IsProcessInJob(GetCurrentProcess(),job,&member) || !member)ExitProcess(15);CloseHandle(job);
    if(root){
        wchar_t exe[MAX_PATH],command[1024];GetModuleFileNameW(NULL,exe,MAX_PATH);swprintf_s(command,1024,L"\"%ls\" --leaf %ls %ls",exe,argv[3],argv[4]);
        STARTUPINFOW startup={0};startup.cb=sizeof(startup);PROCESS_INFORMATION pi={0};
        if(!CreateProcessW(exe,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&pi))ExitProcess(14);
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    }
    SetEvent(ready);CloseHandle(ready);Sleep(INFINITE);return true;
}
int wmain(int argc,wchar_t** argv){
    if(child_mode(argc,argv))return 0;
    GUID id;CHECK(SUCCEEDED(CoCreateGuid(&id)));GUID zero={0};L4WorkerJob* owner=NULL;
    CHECK(!l4_worker_job_create_id(NULL,&owner));CHECK(!l4_worker_job_create_id(&zero,&owner));CHECK(!l4_worker_job_create(NULL,&owner));
    CHECK(l4_worker_job_create_id(&id,&owner));if(!owner)return 1;
    L4WorkerJob* duplicate=NULL;CHECK(!l4_worker_job_create_id(&id,&duplicate));CHECK(!duplicate && GetLastError()==ERROR_ALREADY_EXISTS);
    wchar_t uuid[40],name[96];CHECK(StringFromGUID2(&id,uuid,40)==39);swprintf_s(name,96,L"Global\\L4UpdateWorker.%ls",uuid);
    HANDLE job=OpenJobObjectW(JOB_OBJECT_QUERY|JOB_OBJECT_SET_ATTRIBUTES|READ_CONTROL|WRITE_DAC,FALSE,name);CHECK(job);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};CHECK(QueryInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits),NULL));CHECK(limits.BasicLimitInformation.LimitFlags==JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE);
    DWORD flags=0;CHECK(GetHandleInformation(job,&flags) && !(flags&HANDLE_FLAG_INHERIT));BOOL parent=TRUE;CHECK(IsProcessInJob(GetCurrentProcess(),job,&parent) && !parent);
    wchar_t exe[MAX_PATH],directory[MAX_PATH],command[1024],first[128],second[128];CHECK(GetModuleFileNameW(NULL,exe,MAX_PATH));wcscpy_s(directory,MAX_PATH,exe);*wcsrchr(directory,L'\\')=0;
    swprintf_s(first,128,L"Local\\L4WorkerJob.test.%lu.%llu",GetCurrentProcessId(),GetTickCount64());swprintf_s(second,128,L"%ls.leaf",first);
    HANDLE ready=event(first),leaf=event(second);CHECK(ready && leaf);
    swprintf_s(command,1024,L"\"%ls\" --child %ls %ls %ls",exe,first,second,name);const wchar_t environment[]=L"L4_WORKER_TEST=1\0\0";
    CHECK(SetEnvironmentVariableW(L"L4_WORKER_PARENT_CANARY",L"must-not-inherit"));
    CHECK(!l4_worker_job_spawn(owner,exe,directory,command,NULL));CHECK(!l4_worker_job_spawn(owner,L"Z:\\missing-worker.exe",directory,command,environment));CHECK(!l4_worker_job_process(owner));
    swprintf_s(command,1024,L"\"%ls\" --child %ls %ls %ls",exe,first,second,name);
    CHECK(l4_worker_job_spawn(owner,exe,directory,command,environment));HANDLE worker=l4_worker_job_process(owner);CHECK(worker);
    if(!worker){CloseHandle(job);l4_worker_job_close(&owner,5000);CloseHandle(ready);CloseHandle(leaf);return 1;}
    HANDLE held=NULL;CHECK(DuplicateHandle(GetCurrentProcess(),worker,GetCurrentProcess(),&held,SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,0));
    CHECK(GetHandleInformation(worker,&flags) && !(flags&HANDLE_FLAG_INHERIT));CHECK(WaitForSingleObject(ready,30)==WAIT_TIMEOUT);
    BOOL member=FALSE;CHECK(IsProcessInJob(worker,job,&member) && member);L4RecoveryPlan plan={0};plan.operation=id;epoch(&plan,worker);CHECK(l4_worker_job_verify(owner,&plan));
    L4RecoveryPlan bad=plan;bad.worker_pid++;CHECK(!l4_worker_job_verify(owner,&bad));bad=plan;bad.worker_created.dwLowDateTime^=1;CHECK(!l4_worker_job_verify(owner,&bad));bad=plan;bad.operation.Data1^=1;CHECK(!l4_worker_job_verify(owner,&bad));
    CHECK(!l4_worker_job_spawn(owner,exe,directory,command,environment));bool accept=false;
    CHECK(!l4_worker_job_resume_checked(owner,&plan,gate,&accept));CHECK(audits==1 && WaitForSingleObject(ready,30)==WAIT_TIMEOUT);
    CHECK(!l4_worker_job_resume_checked(owner,&plan,drift_gate,job));CHECK(audits==2 && WaitForSingleObject(ready,30)==WAIT_TIMEOUT);
    limits.BasicLimitInformation.LimitFlags|=JOB_OBJECT_LIMIT_BREAKAWAY_OK;CHECK(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)));CHECK(!l4_worker_job_verify(owner,&plan));
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;CHECK(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)));CHECK(l4_worker_job_verify(owner,&plan));
    PSECURITY_DESCRIPTOR sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GR;;;BU)",SDDL_REVISION_1,&sd,NULL));
    CHECK(SetKernelObjectSecurity(job,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd));LocalFree(sd);CHECK(!l4_worker_job_verify(owner,&plan));
    wchar_t policy[160];swprintf_s(policy,160,L"D:P(A;;0x%lx;;;SY)(A;;0x%lx;;;BA)",(DWORD)JOB_OBJECT_ALL_ACCESS,(DWORD)JOB_OBJECT_ALL_ACCESS);
    CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(policy,SDDL_REVISION_1,&sd,NULL));CHECK(SetKernelObjectSecurity(job,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd));LocalFree(sd);
    Race first_race={owner,&plan,CreateEventW(NULL,TRUE,FALSE,NULL),CreateEventW(NULL,TRUE,FALSE,NULL),0,false};CHECK(first_race.entered && first_race.finish);
    HANDLE winner=CreateThread(NULL,0,resume_race,&first_race,0,NULL);CHECK(winner);CHECK(WaitForSingleObject(first_race.entered,5000)==WAIT_OBJECT_0);
    Race second_race=first_race;second_race.calls=0;HANDLE loser=CreateThread(NULL,0,resume_race,&second_race,0,NULL);CHECK(loser);CHECK(WaitForSingleObject(loser,5000)==WAIT_OBJECT_0);CHECK(!second_race.result && !second_race.calls);
    CHECK(WaitForSingleObject(ready,30)==WAIT_TIMEOUT);SetEvent(first_race.finish);CHECK(WaitForSingleObject(winner,5000)==WAIT_OBJECT_0);CHECK(first_race.result && first_race.calls==1);
    CloseHandle(winner);CloseHandle(loser);CloseHandle(first_race.entered);CloseHandle(first_race.finish);
    accept=true;CHECK(!l4_worker_job_resume_checked(owner,&plan,gate,&accept));CHECK(audits==2);
    CHECK(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0);CHECK(WaitForSingleObject(leaf,5000)==WAIT_OBJECT_0);
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting={0};CHECK(QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),NULL));printf("Owned worker/descendant/OS console processes: %lu\n",accounting.ActiveProcesses);CHECK(accounting.ActiveProcesses>=2);
    CHECK(rollback_worker(&plan,GetTickCount64()+5000));CHECK(WaitForSingleObject(held,0)==WAIT_OBJECT_0);CHECK(!l4_worker_job_verify(owner,&plan));
    CHECK(QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),NULL));CHECK(!accounting.ActiveProcesses);
    CHECK(l4_worker_job_close(&owner,5000));CHECK(!owner);CHECK(l4_worker_job_close(&owner,5000));CloseHandle(held);CloseHandle(job);
    CHECK(CoCreateGuid(&id)==S_OK);CHECK(l4_worker_job_create_id(&id,&owner));swprintf_s(command,1024,L"\"%ls\" --leaf %ls ignored",exe,first);CHECK(l4_worker_job_spawn(owner,exe,directory,command,environment));
    worker=l4_worker_job_process(owner);CHECK(DuplicateHandle(GetCurrentProcess(),worker,GetCurrentProcess(),&held,SYNCHRONIZE,FALSE,0));CHECK(!l4_worker_job_close(&owner,0) && owner);CHECK(l4_worker_job_close(&owner,5000));CHECK(WaitForSingleObject(held,0)==WAIT_OBJECT_0);CloseHandle(held);
    CHECK(CoCreateGuid(&id)==S_OK);CHECK(StringFromGUID2(&id,uuid,40)==39);swprintf_s(name,96,L"Global\\L4UpdateWorker.%ls",uuid);
    HANDLE collision=CreateEventW(NULL,TRUE,FALSE,name);CHECK(collision);CHECK(!l4_worker_job_create_id(&id,&owner) && !owner);CHECK(SetEvent(collision) && WaitForSingleObject(collision,0)==WAIT_OBJECT_0);CloseHandle(collision);
    /* Independent window1 adapter: real original UUID Job/descendant exit;
     * helper source/binary remains unchanged. Wrong UUID cannot kill bare worker. */
    CHECK(CoCreateGuid(&id)==S_OK);CHECK(l4_worker_job_create_id(&id,&owner));
    CHECK(StringFromGUID2(&id,uuid,40)==39);swprintf_s(name,96,L"Global\\L4UpdateWorker.%ls",uuid);
    CHECK(ResetEvent(ready) && ResetEvent(leaf));
    swprintf_s(command,1024,L"\"%ls\" --child %ls %ls %ls",exe,first,second,name);
    CHECK(l4_worker_job_spawn(owner,exe,directory,command,environment));worker=l4_worker_job_process(owner);
    plan.operation=id;epoch(&plan,worker);CHECK(l4_worker_job_resume_checked(owner,&plan,gate,&accept));
    CHECK(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0 && WaitForSingleObject(leaf,5000)==WAIT_OBJECT_0);
    L4CommunicationPlan communication={0};communication.operation=id;communication.worker_pid=plan.worker_pid;communication.worker_created=plan.worker_created;
    CHECK(l4_communication_worker_live(&communication));
    L4CommunicationPlan stale=communication;stale.worker_created.dwLowDateTime^=1;CHECK(!l4_communication_worker_live(&stale));
    CHECK(!l4_communication_worker_exit(&communication,0));
    L4CommunicationPlan foreign=communication;CHECK(CoCreateGuid(&foreign.operation)==S_OK);
    CHECK(!l4_communication_worker_live(&foreign));CHECK(l4_communication_worker_live(&communication));
    CHECK(!l4_communication_worker_exit(&foreign,1000));CHECK(WaitForSingleObject(worker,0)==WAIT_TIMEOUT);
    CHECK(l4_communication_worker_exit(&communication,5000));CHECK(WaitForSingleObject(worker,0)==WAIT_OBJECT_0);
    CHECK(!l4_communication_worker_live(&communication));
    job=OpenJobObjectW(JOB_OBJECT_QUERY,FALSE,name);CHECK(job);
    CHECK(QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),NULL) && !accounting.ActiveProcesses);
    CloseHandle(job);CHECK(l4_worker_job_close(&owner,5000));
    CloseHandle(ready);CloseHandle(leaf);CHECK(SetEnvironmentVariableW(L"L4_WORKER_PARENT_CANARY",NULL));
    printf("Producer worker Job: %u passed, %u failed; real suspended child/descendant + helper kill, modeled task-audit gate, no SCM/tasks/network\n",checks-failures,failures);return failures?1:0;
}
