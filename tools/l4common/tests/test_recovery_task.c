#include "../recovery_task_internal.h"
#include "../journal_internal.h"
#include "update_state_fixture.h"
#include <objbase.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL scheduler %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
/* Native gate is read-only COM definition parsing. No folder/task registration. */
bool l4_recovery_task_canonical_local(const wchar_t* xml,wchar_t** result);
typedef struct {
    L4Journal* journal;L4Layout roots;const wchar_t* operation;bool present,unknown_create,bad_acl,bad_folder,bad_read,bad_create,terminal,drift;
    unsigned creates,reads,helpers;unsigned fail_helper;wchar_t* xml;
} Model;
static bool unlocked(Model* m){L4RecoveryGuard* guard=NULL;bool ok=l4_recovery_open(&m->roots,m->operation,100,&guard);CHECK(ok);if(guard)l4_recovery_close(guard);return ok;}
static bool helper(void* context,const L4Layout* roots,const L4RecoveryHelper* identity){Model* m=context;CHECK(roots && identity);++m->helpers;
    if(!unlocked(m))return false;return m->helpers!=m->fail_helper;}
static bool folder(void* context,bool create){Model* m=context;(void)create;return !m->bad_folder && unlocked(m);}
static bool canonical(void* context,const wchar_t* xml,wchar_t** result){(void)context;*result=_wcsdup(xml);return *result!=NULL;}
static bool read_task(void* context,const L4RecoveryTask* task,bool* present,wchar_t** xml,wchar_t** sd){
    Model* m=context;CHECK(task && !wcsncmp(task->path,L4_RECOVERY_TASK_FOLDER,wcslen(L4_RECOVERY_TASK_FOLDER)));++m->reads;
    *present=m->present;*xml=*sd=NULL;if(m->bad_read)return false;if(!*present)return true;
    *xml=_wcsdup(m->xml);*sd=_wcsdup(m->bad_acl?L"O:SYG:SYD:P(A;;FA;;;SY)(A;;FA;;;BU)":L4_RECOVERY_TASK_SDDL);
    if(m->drift && m->reads>=2)(*xml)[0]=L'!';return *xml && *sd;
}
static bool create_task(void* context,const L4RecoveryTask* task){
    Model* m=context;++m->creates;CHECK(!m->present);BYTE* intent=NULL;DWORD size=0;
    CHECK(l4_store_find_record(m->journal,67,m->journal->sequence,&intent,&size));CHECK(size==wcslen(task->xml)*2 && !memcmp(intent,task->xml,size));free(intent);
    if(m->bad_create)return false;m->present=true;free(m->xml);m->xml=_wcsdup(task->xml);
    if(m->terminal){L4RecoveryGuard* guard=NULL;CHECK(l4_recovery_open(&m->roots,m->operation,100,&guard));
        const L4RecoveryPlan* p=l4_recovery_plan(guard);CHECK(l4_recovery_finish(guard,L4_RECOVERY_COMMITTED,0,p->armed_utc));l4_recovery_close(guard);}
    return !m->unknown_create;
}
static void reset(Model* m){m->creates=m->reads=m->helpers=0;m->unknown_create=m->bad_acl=m->bad_folder=m->bad_read=m->bad_create=m->terminal=m->drift=false;m->fail_helper=0;}
int main(void){
    CHECK(l4_recovery_task_system_account(L"S-1-5-18"));
    CHECK(!l4_recovery_task_system_account(NULL));CHECK(!l4_recovery_task_system_account(L""));
    CHECK(!l4_recovery_task_system_account(L"S-1-5-19"));CHECK(!l4_recovery_task_system_account(L"S-1-5-32-544"));
    CHECK(!l4_recovery_task_system_account(L"L4-No-Such-System-Account-7031"));
    BYTE system_sid[SECURITY_MAX_SID_SIZE];DWORD system_size=sizeof(system_sid);wchar_t account[256],domain[256],qualified[514];DWORD account_size=256,domain_size=256;SID_NAME_USE use;
    CHECK(CreateWellKnownSid(WinLocalSystemSid,NULL,system_sid,&system_size));
    CHECK(LookupAccountSidW(NULL,system_sid,account,&account_size,domain,&domain_size,&use));
    CHECK(l4_recovery_task_system_account(account));
    CHECK(swprintf_s(qualified,514,L"%ls\\%ls",domain,account)>0);CHECK(l4_recovery_task_system_account(qualified));
    wchar_t oversized[514];for(unsigned i=0;i<513;i++)oversized[i]=L'x';oversized[513]=0;CHECK(!l4_recovery_task_system_account(oversized));
    UpdateFixture fixture;CHECK(update_fixture_init(&fixture));CHECK(update_fixture_put(&fixture,0));
    const wchar_t* id=L"17730000-0000-4000-8000-000000000001";L4Journal* j=NULL;CHECK(l4_journal_open(&fixture.layout,id,true,&j));if(!j)return 1;
    L4RecoveryPlan p={0};memcpy(&p.operation,j->header+8,16);p.worker_pid=GetCurrentProcessId();FILETIME e,k,u,t;CHECK(GetProcessTimes(GetCurrentProcess(),&p.worker_created,&e,&k,&u));
    p.supervisor_pid=p.worker_pid;p.supervisor_created=p.worker_created;GetSystemTimeAsFileTime(&t);p.armed_utc=((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;
    p.deadline_utc=p.armed_utc+600000000ull;p.recovery_ms=1234;p.start_type=SERVICE_AUTO_START;p.old_size=100;p.new_size=101;memset(p.old_sha256,1,32);memset(p.new_sha256,2,32);
    swprintf_s(p.before,2048,L"\"%ls\\releases\\1.13.2\\l4superv\\l4superv.exe\"",fixture.layout.binaries);swprintf_s(p.after,2048,L"\"%ls\\releases\\1.13.3\\l4superv\\l4superv.exe\"",fixture.layout.binaries);
    PSECURITY_DESCRIPTOR sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL));p.config_sd=sd;p.config_sd_size=GetSecurityDescriptorLength(sd);
    CHECK(l4_journal_append(j,64,"fixture",7,&p.sequence));CHECK(l4_recovery_prepare(j,&p,GetCurrentProcess()));L4RecoveryHelper inventory={123,{0}};memset(inventory.sha256,0xa5,32);
    L4RecoveryTask spec;CHECK(l4_recovery_task_spec(&fixture.layout,&p,&inventory,2100,&spec));CHECK(spec.execution_seconds==4 && spec.deadline_utc>=p.deadline_utc && spec.deadline_utc-p.deadline_utc<10000000ull);
    CHECK(wcsstr(spec.xml,L"<TimeTrigger") && wcsstr(spec.xml,L"<BootTrigger") && wcsstr(spec.xml,L"--boot") && wcsstr(spec.xml,L"<AllowStartOnDemand>false</AllowStartOnDemand>"));
    CHECK(wcsstr(spec.xml,L"<ExecutionTimeLimit>PT4S</ExecutionTimeLimit>") && !wcsstr(spec.xml,L"<RestartOnFailure>"));
    CHECK(!l4_recovery_task_spec(&fixture.layout,&p,&inventory,0,&spec));CHECK(!l4_recovery_task_spec(&fixture.layout,&p,&inventory,60001,&spec));
    CHECK(!l4_recovery_task_spec(&fixture.layout,&p,&inventory,1999,&spec));
    CHECK(l4_recovery_task_spec(&fixture.layout,&p,&inventory,2100,&spec));
    L4RecoveryPlan rounded=p;rounded.deadline_utc=(p.deadline_utc/10000000ull)*10000000ull;rounded.armed_utc=rounded.deadline_utc-10000000ull;
    L4RecoveryTask exact;CHECK(l4_recovery_task_spec(&fixture.layout,&rounded,&inventory,2100,&exact));CHECK(exact.deadline_utc==rounded.deadline_utc);
    rounded.deadline_utc++;CHECK(l4_recovery_task_spec(&fixture.layout,&rounded,&inventory,2100,&exact));CHECK(exact.deadline_utc==rounded.deadline_utc+9999999ull);
    rounded=p;rounded.deadline_utc=~0ull;CHECK(!l4_recovery_task_spec(&fixture.layout,&rounded,&inventory,2100,&exact));
    L4Layout ampersand;wchar_t programs[MAX_PATH];swprintf_s(programs,MAX_PATH,L"%ls\\Programs & XML",fixture.root);
    CHECK(l4_layout_from_roots(&ampersand,programs,fixture.layout.data,L"1.13.2"));rounded=p;
    swprintf_s(rounded.before,2048,L"\"%ls\\releases\\1.13.2\\l4superv\\l4superv.exe\"",ampersand.binaries);swprintf_s(rounded.after,2048,L"\"%ls\\releases\\1.13.3\\l4superv\\l4superv.exe\"",ampersand.binaries);
    CHECK(l4_recovery_task_spec(&ampersand,&rounded,&inventory,2100,&exact));CHECK(wcsstr(exact.xml,L"Programs &amp; XML") && !wcsstr(exact.xml,L"Programs & XML"));
    L4RecoveryTask other;inventory.sha256[0]^=1;CHECK(l4_recovery_task_spec(&fixture.layout,&p,&inventory,2100,&other));CHECK(wcscmp(spec.xml,other.xml));inventory.sha256[0]^=1;
    CHECK(l4_recovery_task_check_acl(L4_RECOVERY_TASK_SDDL,false));CHECK(l4_recovery_task_check_acl(L4_RECOVERY_FOLDER_SDDL,true));
    CHECK(!l4_recovery_task_check_acl(L4_RECOVERY_TASK_SDDL,true));CHECK(!l4_recovery_task_check_acl(L"O:SYG:SYD:(A;;FA;;;SY)(A;;FA;;;BA)",false));
    CHECK(!l4_recovery_task_check_acl(L"O:BAG:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)",false));CHECK(!l4_recovery_task_check_acl(L"O:SYG:SYD:P(A;;FA;;;SY)(A;;FA;;;BU)",false));
    CHECK(!l4_recovery_task_check_acl(L"O:SYG:SYD:P(A;;FR;;;SY)(A;;FA;;;BA)",false));
    wchar_t *normalized=NULL,*again=NULL;CHECK(l4_recovery_task_canonical_local(spec.xml,&normalized));
    if(normalized){CHECK(l4_recovery_task_canonical_local(normalized,&again));CHECK(again && !wcscmp(normalized,again));printf("Native TaskDefinition XML/normalization accepted; no task/folder writes\n");}
    free(normalized);free(again);
    Model model={0};model.journal=j;model.roots=fixture.layout;model.operation=id;L4RecoveryTaskOps ops={&model,helper,folder,canonical,read_task,create_task};
    ULONGLONG seq=j->sequence;CHECK(!l4_recovery_task_run(j,&inventory,2100,false,&ops));CHECK(!model.creates && j->sequence==seq);
    reset(&model);CHECK(l4_recovery_task_run(j,&inventory,2100,true,&ops));CHECK(model.creates==1 && model.reads==3 && model.helpers==2);seq=j->sequence;
    reset(&model);CHECK(l4_recovery_task_run(j,&inventory,2100,true,&ops));CHECK(!model.creates && j->sequence==seq);
    reset(&model);CHECK(l4_recovery_task_run(j,&inventory,2100,false,&ops));CHECK(!model.creates && j->sequence==seq);
    CHECK(!l4_recovery_task_run(j,&inventory,3100,true,&ops));CHECK(j->sequence==seq); /* Changed execution budget never updates existing task. */
    reset(&model);model.bad_acl=true;CHECK(!l4_recovery_task_run(j,&inventory,2100,true,&ops));CHECK(!model.creates && j->sequence==seq);
    reset(&model);model.bad_folder=true;CHECK(!l4_recovery_task_run(j,&inventory,2100,true,&ops));CHECK(!model.creates && j->sequence==seq);
    reset(&model);model.fail_helper=1;CHECK(!l4_recovery_task_run(j,&inventory,2100,true,&ops));CHECK(!model.creates && j->sequence==seq);
    reset(&model);model.fail_helper=2;CHECK(!l4_recovery_task_run(j,&inventory,2100,true,&ops));CHECK(!model.creates && j->sequence==seq);
    reset(&model);model.drift=true;CHECK(!l4_recovery_task_run(j,&inventory,2100,true,&ops));CHECK(!model.creates && j->sequence==seq);
    reset(&model);model.bad_read=true;CHECK(!l4_recovery_task_run(j,&inventory,2100,true,&ops));CHECK(!model.creates && j->sequence==seq);
    reset(&model);model.present=false;model.unknown_create=true;CHECK(!l4_recovery_task_run(j,&inventory,2100,true,&ops));CHECK(model.present && model.creates==1);seq=j->sequence;
    reset(&model);CHECK(l4_recovery_task_run(j,&inventory,2100,true,&ops));CHECK(!model.creates && j->sequence==seq);
    reset(&model);model.present=false;model.bad_create=true;CHECK(!l4_recovery_task_run(j,&inventory,2100,true,&ops));CHECK(!model.present && model.creates==1);
    reset(&model);model.terminal=true;CHECK(!l4_recovery_task_run(j,&inventory,2100,true,&ops));CHECK(model.creates==1);seq=j->sequence;
    reset(&model);CHECK(!l4_recovery_task_run(j,&inventory,2100,true,&ops));CHECK(!model.creates && j->sequence==seq);
    free(model.xml);l4_journal_close(j);LocalFree(sd);CHECK(update_fixture_dispose(&fixture));
    printf("Recovery scheduler ownership/intent/readback/immutable budgets: %u passed, %u failed; registration modeled only\n",checks-failures,failures);return failures?1:0;
}
