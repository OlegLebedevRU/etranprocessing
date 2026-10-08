#include "../worker_handoff_internal.h"
#include "../worker_job_internal.h"
#include "../journal_internal.h"
#include "update_state_fixture.h"
#include <objbase.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL handoff %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static bool resume_gate(void* context){(void)context;return true;}
static bool audit(void* context,L4Journal* j,const L4RecoveryHelper* helper,DWORD overhead){unsigned mode=*(unsigned*)context;CHECK(helper->size==123 && overhead==2000);
    if(mode==1 || mode==4){SetLastError(ERROR_NOT_READY);return false;}
    if(mode==5 || mode==6){GUID id;memcpy(&id,j->header+8,16);wchar_t uuid[40],operation[40];StringFromGUID2(&id,uuid,40);wcsncpy_s(operation,40,uuid+1,36);
        L4RecoveryGuard* guard=NULL;CHECK(l4_recovery_open(&j->layout,operation,1000,&guard));
        if(mode==5){FILETIME t;GetSystemTimeAsFileTime(&t);CHECK(l4_recovery_finish(guard,L4_RECOVERY_COMMITTED,0,((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime));}
        else{wchar_t name[96];swprintf_s(name,96,L"Global\\L4UpdateWorker.%ls",uuid);HANDLE job=OpenJobObjectW(JOB_OBJECT_SET_ATTRIBUTES,FALSE,name);CHECK(job);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_BREAKAWAY_OK;CHECK(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)));CloseHandle(job);}
        l4_recovery_close(guard);}
    return true;}
static int child(int argc,wchar_t** argv){
    if(argc!=8)return 20;L4Layout roots;if(!l4_layout_from_roots(&roots,argv[2],argv[3],L"1.13.2"))return 21;unsigned mode=(unsigned)_wtoi(argv[6]);
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,argv[5]),go=OpenEventW(SYNCHRONIZE,FALSE,argv[7]);if(!ready || !go)return 22;
    if(mode==3 && WaitForSingleObject(go,5000)!=WAIT_OBJECT_0)return 23;
    L4Journal* j=NULL;bool ok=l4_worker_accept_checked(&roots,argv[4],mode==2?100:5000,&j,audit,&mode);
    CHECK(ok==(mode==0 || mode==3));if(mode==2)CHECK(GetLastError()==ERROR_TIMEOUT);
    if(j){BYTE* receipt=NULL;DWORD size;CHECK(l4_store_find_record(j,69,j->sequence,&receipt,&size) && size==32);free(receipt);
        L4WorkerAdmission admission;ULONGLONG before=j->sequence;CHECK(l4_worker_recheck_checked(j,&admission,audit,&mode));CHECK(admission.sequence && admission.generation==0 && admission.supervisor_pid && j->sequence==before);
        CHECK(admission.parent_pid==admission.supervisor_pid && admission.parent_pid!=GetCurrentProcessId());
        CHECK(!CompareFileTime(&admission.parent_created,&admission.supervisor_created)); /* Fixture parent is supervisor; exact real ticket epoch. */
        CHECK(admission.helper.size==123);for(unsigned n=0;n<32;n++)CHECK(admission.helper.sha256[n]==1);
        CHECK(l4_worker_recheck_checked(j,&admission,audit,&mode) && j->sequence==before);
        L4UpdateState active={0};CHECK(l4_update_state_read(&roots,&active));
        CHECK(!l4_worker_recheck_active_checked(j,&active,&admission,audit,&mode));
        FILETIME now;GetSystemTimeAsFileTime(&now);ULONGLONG deadline=(((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime)+300000000ull;
        ULONGLONG plan_sequence=0;L4RecoveryGuard* current=NULL;CHECK(l4_recovery_open(&roots,argv[4],1000,&current));if(current)plan_sequence=l4_recovery_plan(current)->sequence;l4_recovery_close(current);
        CHECK(l4_update_state_publish(j,plan_sequence,0,1,deadline));CHECK(l4_update_state_read(&roots,&active));
        CHECK(!l4_worker_recheck_checked(j,&admission,audit,&mode));
        before=j->sequence;CHECK(l4_worker_recheck_active_checked(j,&active,&admission,audit,&mode));
        CHECK(admission.generation==1&&admission.sequence==plan_sequence&&j->sequence==before);
        L4UpdateState wrong=active;wrong.generation++;CHECK(!l4_worker_recheck_active_checked(j,&wrong,&admission,audit,&mode));CHECK(!admission.sequence);
        wrong=active;wrong.owner[0]=wrong.owner[0]=='a'?'b':'a';CHECK(!l4_worker_recheck_active_checked(j,&wrong,&admission,audit,&mode));
        unsigned refused=1;CHECK(!l4_worker_recheck_active_checked(j,&active,&admission,audit,&refused));CHECK(!admission.sequence);
        CHECK(l4_update_state_publish(j,plan_sequence,1,2,deadline+10000000ull));CHECK(l4_update_state_read(&roots,&active));
        CHECK(l4_worker_recheck_active_checked(j,&active,&admission,audit,&mode));CHECK(admission.generation==2);
        CHECK(l4_journal_append(j,999,"unknown",7,NULL));CHECK(!l4_worker_recheck_active_checked(j,&active,&admission,audit,&mode));CHECK(!admission.sequence);
        l4_journal_close(j);j=NULL;
        CHECK(!l4_worker_accept_checked(&roots,argv[4],100,&j,audit,&mode) && !j);}
    SetEvent(ready);CloseHandle(go);CloseHandle(ready);return failures?24:0;
}
int wmain(int argc,wchar_t** argv){if(argc>1 && !wcscmp(argv[1],L"--accept"))return child(argc,argv);
    wchar_t exe[MAX_PATH],directory[MAX_PATH];CHECK(GetModuleFileNameW(NULL,exe,MAX_PATH));wcscpy_s(directory,MAX_PATH,exe);*wcsrchr(directory,L'\\')=0;
    for(unsigned mode=0;mode<=6;mode++){
        UpdateFixture f;CHECK(update_fixture_init(&f));CHECK(update_fixture_put(&f,0));GUID id;CHECK(CoCreateGuid(&id)==S_OK);wchar_t uuid[40],operation[40],ready_name[128],go_name[128];StringFromGUID2(&id,uuid,40);wcsncpy_s(operation,40,uuid+1,36);
        swprintf_s(ready_name,128,L"Local\\L4Handoff.ready.%ls",uuid);swprintf_s(go_name,128,L"Local\\L4Handoff.go.%ls",uuid);HANDLE ready=CreateEventW(NULL,TRUE,FALSE,ready_name),go=CreateEventW(NULL,TRUE,FALSE,go_name);CHECK(ready && go);
        L4Journal* j=NULL;wchar_t lock[MAX_PATH];CHECK(l4_layout_data_path(&f.layout,L"update\\operations\\deployment.lock",lock));CHECK(GetFileAttributesW(lock)==INVALID_FILE_ATTRIBUTES);
        CHECK(!l4_journal_open(&f.layout,operation,false,&j));CHECK(GetFileAttributesW(lock)==INVALID_FILE_ATTRIBUTES);CHECK(l4_journal_open(&f.layout,operation,true,&j));if(!j)return 1;
        L4WorkerJob* owner=NULL;CHECK(l4_worker_job_create_id(&id,&owner));wchar_t command[2048];swprintf_s(command,2048,L"\"%ls\" --accept \"%ls\" \"%ls\" %ls %ls %u %ls",exe,f.layout.binaries,f.layout.data,operation,ready_name,mode,go_name);const wchar_t environment[]=L"L4_HANDOFF_TEST=1\0\0";
        CHECK(l4_worker_job_spawn(owner,exe,directory,command,environment));HANDLE held=NULL;CHECK(DuplicateHandle(GetCurrentProcess(),l4_worker_job_process(owner),GetCurrentProcess(),&held,SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,0));
        L4RecoveryPlan p={0};p.operation=id;p.worker_pid=GetProcessId(held);FILETIME e,k,u,t;CHECK(GetProcessTimes(held,&p.worker_created,&e,&k,&u));p.supervisor_pid=GetCurrentProcessId();CHECK(GetProcessTimes(GetCurrentProcess(),&p.supervisor_created,&e,&k,&u));GetSystemTimeAsFileTime(&t);p.armed_utc=((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;p.deadline_utc=p.armed_utc+600000000ull;p.recovery_ms=1000;p.start_type=SERVICE_AUTO_START;p.old_size=100;p.new_size=101;memset(p.old_sha256,1,32);memset(p.new_sha256,2,32);
        swprintf_s(p.before,2048,L"\"%ls\\releases\\1.13.2\\l4superv\\l4superv.exe\"",f.layout.binaries);swprintf_s(p.after,2048,L"\"%ls\\releases\\1.13.3\\l4superv\\l4superv.exe\"",f.layout.binaries);
        PSECURITY_DESCRIPTOR sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL));p.config_sd=sd;p.config_sd_size=GetSecurityDescriptorLength(sd);CHECK(l4_journal_append(j,64,"fixture",7,&p.sequence));CHECK(l4_recovery_prepare(j,&p,l4_worker_job_process(owner)));CHECK(l4_worker_job_resume_checked(owner,&p,resume_gate,NULL));
        CHECK(WaitForSingleObject(ready,20)==WAIT_TIMEOUT);L4RecoveryHelper helper={123,{0}};memset(helper.sha256,1,32);unsigned parent_mode=mode==1?1:0;
        if(mode==2){CHECK(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0);CHECK(j!=NULL);}
        else{bool ok=l4_worker_transfer_checked(&j,owner,&helper,2000,audit,&parent_mode);CHECK(ok==(mode!=1));CHECK(mode==1?j!=NULL:j==NULL);
            if(mode==1){CHECK(l4_worker_job_close(&owner,5000));}
            else{if(mode==3){L4Journal* wrong=NULL;CHECK(!l4_worker_accept_checked(&f.layout,operation,100,&wrong,audit,&parent_mode) && !wrong);SetEvent(go);}CHECK(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0);}}
        CHECK(WaitForSingleObject(held,5000)==WAIT_OBJECT_0);DWORD code=0;CHECK(GetExitCodeProcess(held,&code));CHECK(mode==1 || code==0);
        if(mode==0 || mode==3){CHECK(l4_journal_open(&f.layout,operation,false,&j));ULONGLONG sequence=j->sequence;CHECK(!l4_worker_transfer_checked(&j,owner,&helper,2000,audit,&parent_mode) && j && GetLastError()==ERROR_ALREADY_EXISTS);CHECK(j->sequence==sequence);l4_journal_close(j);j=NULL;}
        CHECK(l4_worker_job_close(&owner,5000));CloseHandle(held);
        if(j)l4_journal_close(j);LocalFree(sd);CloseHandle(ready);CloseHandle(go);CHECK(update_fixture_dispose(&f));
    }
    printf("Worker journal handoff: %u passed, %u failed; actual separate child/locks/ticket/receipt, modeled task audit, no SCM/tasks/network\n",checks-failures,failures);return failures?1:0;
}
