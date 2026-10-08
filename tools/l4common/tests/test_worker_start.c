#include "../worker_start_internal.h"
#include "../worker_job_internal.h"
#include "../journal_internal.h"
#include "update_state_fixture.h"
#include <objbase.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL worker start %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
typedef struct {unsigned mode,creates,arms,resumes;bool task;HANDLE child;const wchar_t* operation;} Model;
static bool create(void* context,L4Journal* j,L4WorkerJob** owner){Model* m=context;++m->creates;if(m->mode==1){SetLastError(ERROR_ACCESS_DENIED);return false;}
    GUID id;memcpy(&id,j->header+8,16);bool ok=l4_worker_job_create_id(&id,owner);if(ok && m->mode==3)j->poisoned=true;return ok;}
static bool finish(Model* m,L4Journal* j){L4RecoveryGuard* guard=NULL;if(!l4_recovery_open(&j->layout,m->operation,1000,&guard))return false;
    FILETIME t;GetSystemTimeAsFileTime(&t);bool ok=l4_recovery_finish(guard,L4_RECOVERY_COMMITTED,0,((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime);l4_recovery_close(guard);return ok;}
static bool arm(void* context,L4Journal* j,const L4RecoveryHelper* helper,DWORD overhead){Model* m=context;++m->arms;CHECK(helper && overhead==2000);
    L4RecoveryGuard* guard=NULL;CHECK(l4_recovery_open(&j->layout,m->operation,1000,&guard));if(!guard)return false;
    CHECK(DuplicateHandle(GetCurrentProcess(),OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,l4_recovery_plan(guard)->worker_pid),GetCurrentProcess(),&m->child,0,FALSE,DUPLICATE_SAME_ACCESS|DUPLICATE_CLOSE_SOURCE));l4_recovery_close(guard);
    if(m->mode==4 || m->mode==5){m->task=m->mode==5;SetLastError(ERROR_GEN_FAILURE);return false;}
    m->task=true;if(m->mode==7)CHECK(finish(m,j));return true;}
static bool gate(void* context){return ((Model*)context)->task;}
static bool resume(void* context,L4WorkerJob* owner,L4Journal* j,const L4RecoveryHelper* helper,DWORD overhead){Model* m=context;(void)helper;(void)overhead;++m->resumes;
    if(m->mode==6){SetLastError(ERROR_NOT_READY);return false;}L4RecoveryGuard* guard=NULL;if(!l4_recovery_open(&j->layout,m->operation,1000,&guard))return false;
    FILETIME t;GetSystemTimeAsFileTime(&t);L4RecoveryAction action;bool ok=l4_recovery_action(guard,((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime,false,&action) && action==L4_RECOVERY_WAIT;
    L4RecoveryPlan plan=*l4_recovery_plan(guard);l4_recovery_release(guard);if(ok)ok=l4_worker_job_resume_checked(owner,&plan,gate,m);l4_recovery_close(guard);
    if(ok && m->mode==8)CHECK(finish(m,j));return ok;}
int wmain(int argc,wchar_t** argv){if(argc==2 && !wcscmp(argv[1],L"--child")){Sleep(INFINITE);return 0;}
    wchar_t exe[MAX_PATH],directory[MAX_PATH];CHECK(GetModuleFileNameW(NULL,exe,MAX_PATH));wcscpy_s(directory,MAX_PATH,exe);*wcsrchr(directory,L'\\')=0;
    for(unsigned mode=0;mode<=15;mode++){
        UpdateFixture fixture;CHECK(update_fixture_init(&fixture));CHECK(update_fixture_put(&fixture,0));GUID id;CHECK(CoCreateGuid(&id)==S_OK);wchar_t uuid[40],operation[40];CHECK(StringFromGUID2(&id,uuid,40)==39);wcsncpy_s(operation,40,uuid+1,36);
        L4Journal* j=NULL;CHECK(l4_journal_open(&fixture.layout,operation,true,&j));if(!j)return 1;
        wchar_t command[MAX_PATH+32];swprintf_s(command,_countof(command),L"\"%ls\" --child",exe);const wchar_t environment[]=L"L4_WORKER_START_TEST=1\0\0";
        L4WorkerStart request={0};request.executable=mode==2?L"Z:\\missing-worker.exe":exe;request.directory=directory;request.command=command;request.environment=environment;request.cleanup_ms=5000;request.overhead_ms=2000;request.helper.size=123;memset(request.helper.sha256,1,32);
        L4RecoveryPlan* p=&request.recovery;p->operation=id;CHECK(l4_journal_append(j,64,"fixture",7,&p->sequence));p->supervisor_pid=GetCurrentProcessId();FILETIME e,k,u,t;CHECK(GetProcessTimes(GetCurrentProcess(),&p->supervisor_created,&e,&k,&u));GetSystemTimeAsFileTime(&t);p->armed_utc=((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;p->deadline_utc=p->armed_utc+600000000ull;p->recovery_ms=1000;p->start_type=SERVICE_AUTO_START;p->old_size=100;p->new_size=101;memset(p->old_sha256,1,32);memset(p->new_sha256,2,32);
        swprintf_s(p->before,2048,L"\"%ls\\releases\\1.13.2\\l4superv\\l4superv.exe\"",fixture.layout.binaries);swprintf_s(p->after,2048,L"\"%ls\\releases\\1.13.3\\l4superv\\l4superv.exe\"",fixture.layout.binaries);
        PSECURITY_DESCRIPTOR sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL));p->config_sd=sd;p->config_sd_size=GetSecurityDescriptorLength(sd);
        if(mode==9)request.overhead_ms=1999;
        if(mode==10)CHECK(update_fixture_put(&fixture,1));
        if(mode==11)p->operation.Data1^=1;
        if(mode==12)p->worker_pid=1;
        if(mode==13)p->deadline_utc=p->armed_utc;
        if(mode==14)p->sequence++;
        if(mode==15)memset(request.helper.sha256,0,32);
        Model model={0};model.mode=mode;model.operation=operation;L4WorkerStartOps ops={&model,create,arm,resume};L4WorkerJob* owner=NULL;L4WorkerStartReport report;
        bool ok=l4_worker_start_run(j,&request,&owner,&report,&ops);CHECK(ok==(mode==0));CHECK(!report.cleanup_error);CHECK(ok?owner!=NULL:owner==NULL);
        L4WorkerStartStage expected=mode==0?L4_START_RUNNING:mode==1?L4_START_JOB:mode==2?L4_START_SPAWN:mode==3?L4_START_PLAN:mode<=5?L4_START_ARM:mode<=7?L4_START_RESUME:mode==8?L4_START_VERIFY:L4_START_VALIDATE;
        CHECK(report.stage==expected);if(mode==4 || mode==5)CHECK(report.error==ERROR_GEN_FAILURE);if(mode==8)CHECK(report.error==ERROR_INVALID_STATE);
        if(mode==0){CHECK(report.stage==L4_START_RUNNING && report.plan_published && report.task_attempted && !report.error);CHECK(model.creates==1 && model.arms==1 && model.resumes==1);CHECK(l4_worker_job_close(&owner,5000));}
        else{CHECK(report.error!=0);CHECK(mode>=9?!model.creates:model.creates==1);CHECK(mode<4 || mode>=9?!model.arms:model.arms==1);}
        if(model.child){CHECK(WaitForSingleObject(model.child,0)==WAIT_OBJECT_0);CloseHandle(model.child);}
        L4UpdateState state;CHECK(l4_update_state_read(&fixture.layout,&state) && state.window==(mode==10?1u:0u));
        if(report.plan_published){ULONGLONG before=j->sequence;unsigned creates=model.creates;CHECK(!l4_worker_start_run(j,&request,&owner,&report,&ops));CHECK(!owner && model.creates==creates && j->sequence==before && report.error==ERROR_ALREADY_EXISTS);}
        LocalFree(sd);l4_journal_close(j);CHECK(update_fixture_dispose(&fixture));
    }
    printf("SYSTEM worker-start composition: %u passed, %u failed; real Job/worker/journal/recovery, modeled scheduler, no SCM/tasks/network\n",checks-failures,failures);return failures?1:0;
}
