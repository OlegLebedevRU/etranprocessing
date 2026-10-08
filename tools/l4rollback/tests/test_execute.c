#include "../src/rollback.h"
#include "../../l4common/tests/update_state_fixture.h"
#include <sddl.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures,worker_calls,supervisor_calls;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL execute %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static L4Journal* active;static L4Layout test_roots;static const wchar_t* fixture_operation=L"17730000-0000-4000-8000-000000000001";static bool worker_bad,supervisor_bad;
static bool owned_worker(const L4RecoveryPlan* plan,ULONGLONG deadline){
    ++worker_calls;CHECK(plan && rollback_remaining(deadline));L4RecoveryGuard* reader=NULL;CHECK(l4_recovery_open(&test_roots,fixture_operation,100,&reader));
    L4RecoveryAction action;CHECK(l4_recovery_action(reader,plan->armed_utc,false,&action) && action==L4_RECOVERY_REQUIRED);
    CHECK(!l4_recovery_finish(reader,L4_RECOVERY_COMMITTED,0,plan->armed_utc));l4_recovery_close(reader);
    if(worker_bad){SetLastError(ERROR_ACCESS_DENIED);return false;}
    l4_journal_close(active);active=NULL;return true; /* Models observed worker exit releasing REAL deployment lock. */
}
static bool fixed_supervisor(const L4Layout* roots,const L4RecoveryPlan* plan,ULONGLONG deadline){
    ++supervisor_calls;CHECK(roots && plan && rollback_remaining(deadline));HANDLE competing=rollback_lock(roots,GetTickCount64()+20);
    CHECK(competing==INVALID_HANDLE_VALUE);if(competing!=INVALID_HANDLE_VALUE)CloseHandle(competing);
    if(supervisor_bad){SetLastError(ERROR_SERVICE_LOGON_FAILED);return false;}return true;
}
#define rollback_worker owned_worker
#define rollback_supervisor fixed_supervisor
#include "../src/execute.c"
static void scenario(unsigned mode){
    UpdateFixture fixture;CHECK(update_fixture_init(&fixture));CHECK(update_fixture_put(&fixture,0));test_roots=fixture.layout;active=NULL;
    CHECK(l4_journal_open(&fixture.layout,fixture_operation,true,&active));L4RecoveryPlan p={0};memcpy(&p.operation,active->header+8,16);p.worker_pid=GetCurrentProcessId();
    FILETIME e,k,u;CHECK(GetProcessTimes(GetCurrentProcess(),&p.worker_created,&e,&k,&u));p.supervisor_pid=p.worker_pid;p.supervisor_created=p.worker_created;
    p.armed_utc=utc();p.deadline_utc=p.armed_utc+600000000ull;p.recovery_ms=1000;p.start_type=SERVICE_AUTO_START;p.old_size=1;p.new_size=2;memset(p.old_sha256,1,32);memset(p.new_sha256,2,32);
    swprintf_s(p.before,2048,L"\"%ls\\releases\\1.13.2\\l4superv\\l4superv.exe\"",fixture.layout.binaries);swprintf_s(p.after,2048,L"\"%ls\\releases\\1.13.3\\l4superv\\l4superv.exe\"",fixture.layout.binaries);
    PSECURITY_DESCRIPTOR sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL));p.config_sd=sd;p.config_sd_size=GetSecurityDescriptorLength(sd);
    CHECK(l4_journal_append(active,64,"fixture",7,&p.sequence));CHECK(l4_recovery_prepare(active,&p,GetCurrentProcess()));
    worker_calls=supervisor_calls=0;worker_bad=mode==1;supervisor_bad=mode==2;
    if(mode==3){wchar_t runner[MAX_PATH];swprintf_s(runner,MAX_PATH,L"%ls\\supervisor.runner.lock",active->directory);CHECK(DeleteFileW(runner));}
    CHECK(rollback_execute(&fixture.layout,fixture_operation,true)==(mode==0));
    if(mode==0){CHECK(worker_calls==1 && supervisor_calls==1);CHECK(rollback_execute(&fixture.layout,fixture_operation,true));CHECK(worker_calls==1 && supervisor_calls==1);}
    if(mode==1)CHECK(worker_calls==1 && !supervisor_calls);
    if(mode==3)CHECK(!worker_calls && !supervisor_calls);
    L4RecoveryGuard* guard=NULL;CHECK(l4_recovery_open(&fixture.layout,fixture_operation,100,&guard));L4RecoveryAction action;CHECK(l4_recovery_action(guard,p.deadline_utc,true,&action));
    CHECK(action==(mode==0?L4_RECOVERY_DONE:mode==3?L4_RECOVERY_REQUIRED:L4_RECOVERY_BLOCKED));l4_recovery_close(guard);
    if(active){l4_journal_close(active);active=NULL;}LocalFree(sd);CHECK(update_fixture_dispose(&fixture));
}
int main(void){for(unsigned i=0;i<4;i++)scenario(i);
    printf("Helper execution/decision/deployment-lock ordering: %u passed, %u failed; real private result/locks, worker exit and SCM modeled\n",checks-failures,failures);return failures?1:0;}
