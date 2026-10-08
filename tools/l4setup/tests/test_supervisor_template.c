/* Real held process epoch/config SD/codec, modeled SCM/token/source admission.
 * No live supervisor, service mutation, task/Job or helper/worker launch. */
#include "../src/supervisor_template.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/update_state.h"
#include "../../l4common/communication_plan.h"
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct SetupRemoteWorkerPlan{int unused;};struct SetupOperationPlan{int unused;};
static SetupOperationPlan model_operation;static L4ServiceSwitch model_switch;static L4UpdateState model_state;static BYTE* model_config;static DWORD model_config_size;
static const wchar_t* model_uuid=L"17730000-0000-4000-8000-000000000031";static int fault;static unsigned checks,failures,queries,loads;static ULONGLONG clock_now,utc_now;static volatile LONG model_cancel;
static bool started,communication_phase;static int started_fault;static unsigned actions;static L4RecoveryPlan model_recovery;
static L4CommunicationPlan model_communication;struct L4RecoveryGuard{int unused;};static L4RecoveryGuard model_guard;
struct L4CommunicationPin{int unused;};static L4CommunicationPin model_pin;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL %u %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static ULONGLONG fake_tick(void){return clock_now;}
static void fake_utc(LPFILETIME time){time->dwLowDateTime=(DWORD)utc_now;time->dwHighDateTime=(DWORD)(utc_now>>32);}
const wchar_t* setup_remote_worker_plan_uuid(const SetupRemoteWorkerPlan* p){(void)p;return model_uuid;}
const char* setup_remote_worker_plan_arch(const SetupRemoteWorkerPlan* p){(void)p;return "x86";}
ULONGLONG setup_remote_worker_plan_sequence(const SetupRemoteWorkerPlan* p){(void)p;return 14;}
bool l4_remote_host_self(const L4Layout* l,const wchar_t* uuid,const char* arch){(void)l;CHECK(!wcscmp(uuid,model_uuid) && !strcmp(arch,"x86"));return true;}
bool l4_journal_replay(L4Journal* j,L4JournalVisitor visit,void* c){DWORD kinds[]={92,93,60,61,62,20,63,10,64};for(unsigned i=0;i<9;i++)if(!visit(kinds[i],i==8?(started_fault==19?13:14):i+1,NULL,0,c))return false;
    if(fault==1)return visit(94,20,NULL,0,c);if(!started)return true;BYTE* bytes=NULL;DWORD size=0;if(!l4_recovery_encode(&j->layout,&model_recovery,&bytes,&size))return false;
    const wchar_t* xml=started_fault==10?L"different":L"modeled-task";DWORD n=(DWORD)wcslen(xml)*2;
    bool ok=started_fault!=9 || visit(67,15,xml,n,c);if(ok)ok=visit(66,15,bytes,size,c);if(ok && started_fault==6)ok=visit(66,16,bytes,size,c);free(bytes);
    if(ok && started_fault!=7)ok=visit(67,16,xml,n,c);if(ok && started_fault==8)ok=visit(68,17,"x",1,c);
    if(ok && ((communication_phase && started_fault!=18) || started_fault==17))ok=visit(70,17,"modeled exact plan",18,c);return ok;}
HANDLE l4_worker_job_process(const L4WorkerJob* worker){CHECK(worker==(const L4WorkerJob*)(ULONG_PTR)1);return GetCurrentProcess();}
bool l4_worker_job_verify(L4WorkerJob* worker,const L4RecoveryPlan* p){CHECK(worker==(L4WorkerJob*)(ULONG_PTR)1 && p->worker_pid==GetCurrentProcessId());return started_fault!=3;}
bool l4_recovery_open(const L4Layout* layout,const wchar_t* id,DWORD timeout,L4RecoveryGuard** out){(void)layout;CHECK(!wcscmp(id,model_uuid) && timeout==1000);if(started_fault==5)return false;*out=&model_guard;return true;}
const L4RecoveryPlan* l4_recovery_plan(const L4RecoveryGuard* g){CHECK(g==&model_guard);return &model_recovery;}
bool l4_recovery_action(L4RecoveryGuard* g,ULONGLONG now,bool boot,L4RecoveryAction* out){CHECK(g==&model_guard && now==utc_now && !boot);actions++;*out=started_fault==4 || (started_fault==21 && actions>1)?L4_RECOVERY_REQUIRED:L4_RECOVERY_WAIT;return true;}
void l4_recovery_release(L4RecoveryGuard* g){CHECK(g==&model_guard);}
bool l4_recovery_relock(L4RecoveryGuard* g,DWORD timeout){CHECK(g==&model_guard && timeout==1000);return true;}
void l4_recovery_close(L4RecoveryGuard* g){if(g)CHECK(g==&model_guard);}
bool l4_recovery_task_spec(const L4Layout* layout,const L4RecoveryPlan* p,const L4RecoveryHelper* helper,DWORD overhead,L4RecoveryTask* task){(void)layout;CHECK(p->worker_pid && helper->size==123 && overhead==2000);memset(task,0,sizeof(*task));wcscpy_s(task->xml,L4_RECOVERY_TASK_XML_LIMIT,L"modeled-task");return true;}
bool l4_recovery_task_audit(L4Journal* j,const L4RecoveryHelper* helper,DWORD overhead){(void)j;CHECK(helper->size==123 && overhead==2000);if(started_fault==20)model_state.window=1;return started_fault!=2;}
bool l4_communication_plan_open(const L4Layout* layout,const wchar_t* id,L4CommunicationPin** out){(void)layout;CHECK(!wcscmp(id,model_uuid));if(started_fault==11)return false;*out=&model_pin;return true;}
const L4CommunicationPlan* l4_communication_pinned_plan(const L4CommunicationPin* pin){CHECK(pin==&model_pin);return &model_communication;}
bool l4_communication_plan_verify_journal(L4Journal* j,const L4CommunicationPin* pin){(void)j;CHECK(pin==&model_pin);return started_fault!=12;}
void l4_communication_plan_close(L4CommunicationPin* pin){if(pin)CHECK(pin==&model_pin);}
bool l4_update_state_read(const L4Layout* l,L4UpdateState* state){(void)l;*state=model_state;return true;}
bool setup_update_load_operation(L4Journal* j,ULONGLONG seq,SetupOperationPlan** p){(void)j;CHECK(seq==14);loads++;if(fault==2){SetLastError(ERROR_CRC);return false;}*p=&model_operation;clock_now+=10;if(fault==17 && loads>1)utc_now+=600000000ULL;if(fault==18)clock_now+=600000;return true;}
void setup_operation_free(SetupOperationPlan* p){CHECK(!p || p==&model_operation);}
const L4ServiceSwitch* setup_operation_switch(const SetupOperationPlan* p,unsigned h,unsigned s){CHECK(p==&model_operation);return h==0 && s==3?&model_switch:NULL;}
static void put32(BYTE* b,DWORD n){for(unsigned i=0;i<4;i++)b[i]=(BYTE)(n>>(i*8));}
DWORD l4_store_get32(const BYTE* b){return (DWORD)b[0]|((DWORD)b[1]<<8)|((DWORD)b[2]<<16)|((DWORD)b[3]<<24);}
ULONGLONG l4_store_get64(const BYTE* b){return (ULONGLONG)l4_store_get32(b)|((ULONGLONG)l4_store_get32(b+4)<<32);}
bool l4_store_find_record(L4Journal* j,DWORD kind,ULONGLONG seq,BYTE** b,DWORD* size){(void)j;
    if(kind==64){CHECK(seq==14);*size=68;*b=calloc(1,*size);put32(*b+20,1);put32(*b+24,1);put32(*b+28,6);return true;}
    CHECK(kind==20 && seq==6);*size=model_config_size;*b=malloc(*size);memcpy(*b,model_config,*size);if(fault==3)(*b)[24]='X';if(fault==4)put32(*b+16,65537);if(fault==5)put32(*b,2);return true;
}
bool l4_config_verify(L4Journal* j,ULONGLONG seq,bool candidate){(void)j;CHECK(seq==6 && !candidate);if(fault==6){SetLastError(ERROR_RETRY);return false;}return true;}
static SC_HANDLE fake_manager(LPCWSTR a,LPCWSTR b,DWORD rights){(void)a;(void)b;CHECK(rights==SC_MANAGER_CONNECT);return (SC_HANDLE)(ULONG_PTR)123;}
static SC_HANDLE fake_service(SC_HANDLE m,LPCWSTR name,DWORD rights){CHECK(m==(SC_HANDLE)(ULONG_PTR)123 && !wcscmp(name,L"L4Superv") && rights==(SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS));return (SC_HANDLE)(ULONG_PTR)124;}
static BOOL fake_close(SC_HANDLE h){CHECK(h==(SC_HANDLE)(ULONG_PTR)123 || h==(SC_HANDLE)(ULONG_PTR)124);return TRUE;}
static BOOL fake_config(SC_HANDLE h,LPQUERY_SERVICE_CONFIGW c,DWORD cap,LPDWORD needed){(void)h;(void)cap;*needed=sizeof(*c);if(!c){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    memset(c,0,sizeof(*c));c->dwServiceType=fault==7?SERVICE_WIN32_SHARE_PROCESS:SERVICE_WIN32_OWN_PROCESS;c->dwStartType=fault==15?SERVICE_DEMAND_START:SERVICE_AUTO_START;
    c->lpBinaryPathName=model_switch.before.image_path;c->lpServiceStartName=fault==8?L"LocalService":L"LocalSystem";return TRUE;}
static BOOL fake_status(SC_HANDLE h,SC_STATUS_TYPE type,LPBYTE buffer,DWORD size,LPDWORD needed){(void)h;CHECK(type==SC_STATUS_PROCESS_INFO && size==sizeof(SERVICE_STATUS_PROCESS));*needed=size;SERVICE_STATUS_PROCESS* s=(SERVICE_STATUS_PROCESS*)buffer;memset(s,0,sizeof(*s));queries++;
    s->dwCurrentState=fault==9?SERVICE_STOPPED:SERVICE_RUNNING;s->dwProcessId=GetCurrentProcessId();if(fault==10 && queries>1)s->dwProcessId++;return TRUE;}
static BOOL fake_image(HANDLE p,DWORD flags,LPWSTR image,PDWORD size){CHECK(p && !flags);const wchar_t* name=L"C:\\PF\\Leo4\\Tools\\releases\\1.13.6\\l4superv\\l4superv.exe";wcscpy_s(image,*size,fault==11?L"C:\\wrong.exe":name);*size=(DWORD)wcslen(image);return TRUE;}
static BOOL fake_token(HANDLE token,TOKEN_INFORMATION_CLASS cls,LPVOID data,DWORD cap,PDWORD size){(void)token;
    if(cls==TokenUser){*size=sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE;CHECK(cap>=*size);TOKEN_USER* user=data;user->User.Sid=(BYTE*)data+sizeof(TOKEN_USER);DWORD n=SECURITY_MAX_SID_SIZE;return CreateWellKnownSid(fault==12?WinLocalServiceSid:WinLocalSystemSid,NULL,user->User.Sid,&n);}
    if(cls==TokenType){*size=sizeof(TOKEN_TYPE);*(TOKEN_TYPE*)data=fault==14?TokenImpersonation:TokenPrimary;return TRUE;}
    if(cls==TokenSessionId){*size=sizeof(DWORD);*(DWORD*)data=fault==13?1:0;return TRUE;}CHECK(false);return FALSE;
}
#define GetTickCount64 fake_tick
#define GetSystemTimeAsFileTime fake_utc
#define OpenSCManagerW fake_manager
#define OpenServiceW fake_service
#define CloseServiceHandle fake_close
#define QueryServiceConfigW fake_config
#define QueryServiceStatusEx fake_status
#define QueryFullProcessImageNameW fake_image
#define GetTokenInformation fake_token
#include "../src/supervisor_template.c"
#undef GetTickCount64
#undef GetSystemTimeAsFileTime
static void reset(void){fault=0;clock_now=1000;queries=loads=0;model_cancel=0;model_state.window=0;model_state.generation=7;FILETIME t;GetSystemTimeAsFileTime(&t);utc_now=((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
int wmain(void){setvbuf(stdout,NULL,_IONBF,0);L4Journal j={0};j.lock=(HANDLE)(ULONG_PTR)1;wcscpy_s(j.directory,MAX_PATH,L"C:\\model\\17730000-0000-4000-8000-000000000031");j.header[8]=1;
    CHECK(l4_layout_from_roots(&j.layout,L"C:\\PF\\Leo4\\Tools",L"C:\\PD\\Leo4\\Tools",L"1.13.6"));model_switch.layout=j.layout;
    wcscpy_s(model_switch.service,32,L"L4Superv");wcscpy_s(model_switch.before.image_path,2048,L"\"C:\\PF\\Leo4\\Tools\\releases\\1.13.6\\l4superv\\l4superv.exe\" --service");
    wcscpy_s(model_switch.after,2048,L"\"C:\\PF\\Leo4\\Tools\\releases\\1.13.7\\l4superv\\l4superv.exe\" --service");model_switch.before.start_type=SERVICE_AUTO_START;model_switch.before_size=42;model_switch.size=43;memset(model_switch.before_sha256,1,32);memset(model_switch.sha256,2,32);
    PSECURITY_DESCRIPTOR sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:SYG:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL));DWORD sd_size=GetSecurityDescriptorLength(sd);
    model_config_size=24+13+3+3+sd_size;model_config=calloc(1,model_config_size);put32(model_config,1);put32(model_config+4,1);put32(model_config+8,13);put32(model_config+12,3);put32(model_config+16,3);put32(model_config+20,sd_size);memcpy(model_config+24,"l4superv.jsonoldnew",19);memcpy(model_config+43,sd,sd_size);LocalFree(sd);
    FILETIME now;GetSystemTimeAsFileTime(&now);ULONGLONG armed=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;ULONGLONG deadline=armed+600000000ULL;SetupRemoteWorkerPlan worker={0};SetupSupervisorTemplate* t=NULL;
    reset();CHECK(setup_supervisor_template_prepare(&j,&worker,armed,deadline,300000,1000,&model_cancel,&t));const L4RecoveryPlan* p=setup_supervisor_template_plan(t);
    CHECK(p && p->sequence==14 && !p->worker_pid && !p->worker_created.dwLowDateTime && !p->worker_created.dwHighDateTime && p->supervisor_pid==GetCurrentProcessId());
    CHECK(p->old_config_size==3 && !memcmp(p->old_config,"old",3) && p->new_config_size==3 && !memcmp(p->new_config,"new",3));
    BYTE* encoded=NULL;DWORD size=0;CHECK(!l4_recovery_encode(&j.layout,p,&encoded,&size)); /* No worker => never publishable. */
    L4RecoveryPlan actual=*p;actual.worker_pid=GetCurrentProcessId();FILETIME e,k,u;CHECK(GetProcessTimes(GetCurrentProcess(),&actual.worker_created,&e,&k,&u));CHECK(l4_recovery_encode(&j.layout,&actual,&encoded,&size));L4RecoveryPlan decoded;CHECK(l4_recovery_decode(&j.layout,encoded,size,&decoded));free(encoded);
    CHECK(setup_supervisor_template_verify(&j,t));model_state.generation++;CHECK(!setup_supervisor_template_verify(&j,t));model_state.generation--;
    model_recovery=actual;L4RecoveryPlan pristine=actual;L4RecoveryHelper helper={123,{1}};started=true;
    model_communication.operation=actual.operation;model_communication.sequence=actual.sequence;model_communication.generation=8;
    model_communication.worker_pid=actual.worker_pid;model_communication.worker_created=actual.worker_created;
    model_communication.supervisor_pid=actual.supervisor_pid;model_communication.supervisor_created=actual.supervisor_created;
    model_communication.armed_utc=actual.armed_utc;model_communication.deadline_utc=actual.deadline_utc-60000000ULL;model_communication.budget.total_ms=5000;
    L4CommunicationPlan pristine_communication=model_communication;
    CHECK(!setup_supervisor_template_verify(&j,t));CHECK(setup_supervisor_template_verify_started(&j,t,(L4WorkerJob*)(ULONG_PTR)1,&helper,2000,false));
    communication_phase=true;actions=0;CHECK(setup_supervisor_template_verify_started(&j,t,(L4WorkerJob*)(ULONG_PTR)1,&helper,2000,true));
    for(int n=1;n<=21;n++){started_fault=n;model_recovery=pristine;model_communication=pristine_communication;actions=0;model_state.window=0;communication_phase=n>=11 && n!=17;
        if(n==1)model_recovery.old_sha256[0]^=1;if(n==13)model_communication.worker_pid++;if(n==14)model_communication.supervisor_created.dwLowDateTime++;
        if(n==15)model_communication.generation++;if(n==16)model_communication.deadline_utc=actual.deadline_utc-50000000ULL;
        CHECK(!setup_supervisor_template_verify_started(&j,t,(L4WorkerJob*)(ULONG_PTR)1,&helper,2000,communication_phase));}
    started=false;started_fault=0;actions=0;model_state.window=0;communication_phase=false;
    t->plan.supervisor_created.dwLowDateTime++;CHECK(!setup_supervisor_template_verify(&j,t));t->plan.supervisor_created.dwLowDateTime--;
    t->plan.worker_pid=1;CHECK(!setup_supervisor_template_verify(&j,t) && GetLastError()==ERROR_INVALID_STATE);t->plan.worker_pid=0;
    fault=6;CHECK(!setup_supervisor_template_verify(&j,t));fault=0;setup_supervisor_template_free(t);t=NULL;
    for(int i=1;i<=18;i++){if(i==16)continue;reset();fault=i;CHECK(!setup_supervisor_template_prepare(&j,&worker,armed,deadline,300000,1000,&model_cancel,&t) && !t);if(i==17)CHECK(GetLastError()==ERROR_TIME_SKEW);if(i==18)CHECK(GetLastError()==ERROR_TIMEOUT);}
    reset();model_state.window=1;CHECK(!setup_supervisor_template_prepare(&j,&worker,armed,deadline,300000,1000,&model_cancel,&t));
    reset();model_cancel=1;CHECK(!setup_supervisor_template_prepare(&j,&worker,armed,deadline,300000,1000,&model_cancel,&t) && GetLastError()==ERROR_CANCELLED);
    reset();CHECK(!setup_supervisor_template_prepare(&j,&worker,deadline,armed,300000,1000,&model_cancel,&t));CHECK(!setup_supervisor_template_prepare(&j,&worker,armed,deadline,300001,1000,&model_cancel,&t));
    free(model_config);printf("Supervisor recovery template: %u checks, %u failures; actual process/config SD/codec, modeled SCM/token/signature; no launch/arm/stop\n",checks,failures);return failures?1:0;
}
