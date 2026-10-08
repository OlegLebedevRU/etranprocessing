/* Composition fixture. Source/publisher/helper/task evidence is modeled here;
 * actual private Job/child/handoff tests remain separate native gates. */
#include "../src/remote_worker_start.c"
#include <shlobj.h>
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"uuid.lib")
static unsigned passed,failed,spawned,closed,transfers,communication,checks_seen;
static int fault;static bool enabled=true;static BYTE hash[32];static volatile LONG late_cancel;
static L4BootstrapPlan original;static L4RemoteRequest request;
static SetupRootAsset asset;static L4RecoveryPlan recovery;static L4RecoveryHelper helper;
static SetupWorkerEngine engine;
static HANDLE observed_child;
#define CHECK(x) do{if(x)passed++;else{failed++;printf("FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError());}}while(0)
static bool preflight(L4Journal* j,const SetupOperationPlan* p,const L4WorkerAdmission* a,const L4RecoveryHelper* h){(void)j;(void)p;(void)a;(void)h;return false;}
static DWORD execute(L4Journal** j,const SetupOperationPlan* p,const L4WorkerAdmission* a,const L4RecoveryHelper* h){(void)j;(void)p;(void)a;(void)h;return ERROR_CALL_NOT_IMPLEMENTED;}
const SetupWorkerEngine* setup_worker_compiled_engine(void){return enabled?&engine:NULL;}
bool setup_installed_source_verify(SetupInstalledSource* s){(void)s;checks_seen++;return fault==1?fail(ERROR_CRC):true;}
const L4BootstrapPlan* setup_installed_source_plan(const SetupInstalledSource* s){(void)s;return &original;}
const SetupRootManifest* setup_installed_source_root(const SetupInstalledSource* s){(void)s;return (const SetupRootManifest*)1;}
const SetupRootManifest* setup_installed_source_updater_root(const SetupInstalledSource* s){(void)s;return (const SetupRootManifest*)1;}
const char* setup_installed_source_updater_version(const SetupInstalledSource* s){(void)s;return "1.13.5";}
const char* setup_installed_source_version(const SetupInstalledSource* s){(void)s;return "1.13.6";}
const char* setup_installed_source_arch(const SetupInstalledSource* s){(void)s;return "x86";}
const L4AccessActors* setup_installed_source_actors(const SetupInstalledSource* s){(void)s;return (const L4AccessActors*)1;}
const SetupRootAsset* setup_root_installer(const SetupRootManifest* r){(void)r;return &asset;}
const BYTE* setup_root_identity(const SetupRootManifest* r){(void)r;return hash;}
const BYTE* setup_root_publisher(const SetupRootManifest* r){(void)r;return hash;}
bool setup_signed_executable(HANDLE f,const wchar_t* p,const BYTE h[32]){(void)f;(void)p;(void)h;return fault==2?fail(ERROR_INVALID_DATA):true;}
bool l4_remote_request_load(L4Journal* j,L4RemoteRequest* r){(void)j;*r=request;return true;}
bool l4_remote_host_load(L4Journal* j,L4RemoteHostReceipt* r){(void)j;memset(r,0,sizeof(*r));r->request=request;r->pid=GetCurrentProcessId();FILETIME b,e,k,u;GetProcessTimes(GetCurrentProcess(),&b,&e,&k,&u);r->birth=((ULONGLONG)b.dwHighDateTime<<32)|b.dwLowDateTime;
    r->format_version=2;strcpy_s(r->updater_version,32,"1.13.5");strcpy_s(r->arch,8,"x86");strcpy_s(r->source_version,32,"1.13.6");r->executable_size=asset.size;memcpy(r->executable_sha256,asset.sha256,32);if(fault==3)r->birth++;return true;}
bool l4_remote_host_self(const L4Layout* l,const wchar_t* u,const char* a){(void)l;(void)u;(void)a;return fault==4?fail(ERROR_ACCESS_DENIED):true;}
bool l4_remote_host_pin_self(const L4Layout* l,const wchar_t* u,const char* a,L4RemoteHost** h,L4RemoteHostImage* i){(void)l;(void)u;(void)a;*h=(L4RemoteHost*)1;memset(i,0,sizeof(*i));i->file=(HANDLE)1;i->size=asset.size;memcpy(i->sha256,asset.sha256,32);strcpy_s(i->updater_version,32,"1.13.5");wcscpy_s(i->path,MAX_PATH,L"C:\\Program Files\\Leo4\\Tools\\setup\\1.13.5\\l4setup.exe");return true;}
void l4_remote_host_close(L4RemoteHost* h){(void)h;}
bool setup_recovery_receipt_load(L4Journal* j,const char* a,SetupRecoveryReceipt** r){(void)j;(void)a;*r=(SetupRecoveryReceipt*)1;return fault==5?fail(ERROR_INVALID_DATA):true;}
const L4RecoveryHelper* setup_recovery_receipt_helper(const SetupRecoveryReceipt* r){return r?&helper:NULL;}
void setup_recovery_receipt_free(SetupRecoveryReceipt* r){(void)r;}
bool setup_supervisor_template_prepare(L4Journal* j,const SetupRemoteWorkerPlan* p,ULONGLONG a,ULONGLONG d,DWORD r,DWORD t,const volatile LONG* c,SetupSupervisorTemplate** o){(void)j;(void)p;(void)a;(void)d;(void)r;(void)t;(void)c;*o=(SetupSupervisorTemplate*)1;return true;}
bool setup_supervisor_template_verify(L4Journal* j,const SetupSupervisorTemplate* t){(void)j;(void)t;return fault==6?fail(ERROR_REVISION_MISMATCH):true;}
bool setup_supervisor_template_verify_started(L4Journal* j,const SetupSupervisorTemplate* t,L4WorkerJob* w,const L4RecoveryHelper* h,DWORD o,bool c){(void)j;(void)t;(void)w;(void)h;(void)o;return fault==(c?9:7)?fail(ERROR_REVISION_MISMATCH):true;}
const L4RecoveryPlan* setup_supervisor_template_plan(const SetupSupervisorTemplate* t){(void)t;return &recovery;}
void setup_supervisor_template_free(SetupSupervisorTemplate* t){(void)t;}
const wchar_t* setup_remote_worker_plan_uuid(const SetupRemoteWorkerPlan* p){(void)p;return L"12345678-1234-1234-1234-123456789abc";}
const char* setup_remote_worker_plan_arch(const SetupRemoteWorkerPlan* p){(void)p;return "x86";}
const L4BootstrapPlan* setup_remote_worker_plan_source(const SetupRemoteWorkerPlan* p){(void)p;return &original;}
const L4RemoteRequest* setup_remote_worker_plan_request(const SetupRemoteWorkerPlan* p){(void)p;return &request;}
ULONGLONG setup_remote_worker_plan_sequence(const SetupRemoteWorkerPlan* p){(void)p;return 14;}
bool l4_worker_start(L4Journal* j,const L4WorkerStart* r,L4WorkerJob** w,L4WorkerStartReport* s){(void)j;spawned++;CHECK(r->helper.size==helper.size);CHECK(wcsstr(r->command,L"--update-worker --source-version 1.13.6 --updater-version 1.13.5 --operation 12345678-1234-1234-1234-123456789abc --arch x86")!=NULL);CHECK(!wcscmp(r->directory,L"C:\\Program Files\\Leo4\\Tools\\setup\\1.13.5"));
    unsigned entries=0;wchar_t windows[MAX_PATH];CHECK(GetWindowsDirectoryW(windows,MAX_PATH)>2);bool drive=false;
    for(const wchar_t* e=r->environment;*e;e+=wcslen(e)+1){CHECK(wcsncmp(e,L"IOT_API_KEY",11)!=0 && wcsncmp(e,L"SW_SIGN",7)!=0);if(!wcsncmp(e,L"SystemDrive=",12)){CHECK(wcslen(e)==14 && !wcsncmp(e+12,windows,2));drive=true;}entries++;}CHECK(entries==4 && drive);*w=(L4WorkerJob*)1;s->stage=L4_START_RUNNING;s->plan_published=true;s->task_attempted=true;return true;}
bool l4_worker_job_close(L4WorkerJob** w,DWORD t){(void)t;closed++;*w=NULL;return fault==11?fail(ERROR_TIMEOUT):true;}
HANDLE l4_worker_job_process(const L4WorkerJob* w){(void)w;return observed_child;}
bool setup_update_prepare_communication(L4Journal* j,ULONGLONG s,L4WorkerJob* w,const L4AccessActors* a,const L4BootstrapChecks* c,ULONGLONG ar,ULONGLONG d,const L4CommunicationBudget* b,DWORD t){(void)j;(void)s;(void)w;(void)a;(void)c;(void)ar;(void)d;(void)b;(void)t;communication++;if(fault==12)Sleep(150);if(fault==13)InterlockedExchange(&late_cancel,1);return fault==8?fail(ERROR_TIMEOUT):true;}
bool l4_worker_transfer(L4Journal** j,L4WorkerJob* w,const L4RecoveryHelper* h,DWORD o){(void)w;(void)h;(void)o;transfers++;if(fault==10 || fault==11)return fail(ERROR_WRITE_FAULT);*j=NULL;return true;}
static bool probe(const L4BootstrapPlan* p,unsigned i,DWORD t,void* c){(void)p;(void)i;(void)t;(void)c;return false;}
static bool barrier(const L4BootstrapPlan* p,DWORD t,void* c){(void)p;(void)t;(void)c;return false;}
bool setup_remote_launch_failure_finish(L4Journal* j,DWORD error,DWORD stage,DWORD cleanup,L4RemoteLaunchFailure* result){
    (void)j;(void)cleanup;CHECK(error && stage>=L4_START_VALIDATE && stage<=L4_START_RUNNING);memset(result,0,sizeof(*result));
    return fault==11?fail(ERROR_WRITE_FAULT):true;
}
/* Real child and OS Known Folders under the production environment. No SCM,
 * SYSTEM, recovery task, journal or installed suite mutation in this test. */
static void known_folder_children(void){
    SetupRemoteWorkerLaunch p={0};wcscpy_s(p.layout.launchers,MAX_PATH,L"C:\\Program Files\\Leo4\\Tools\\bin");CHECK(environment(&p));
    wchar_t image[MAX_PATH],command[MAX_PATH+40],without_drive[2048]={0};CHECK(GetModuleFileNameW(NULL,image,MAX_PATH)>0);
    size_t used=0;for(const wchar_t* entry=p.environment;*entry;entry+=wcslen(entry)+1)if(wcsncmp(entry,L"SystemDrive=",12)){
        size_t size=wcslen(entry)+1;CHECK(used+size<2048);memcpy(without_drive+used,entry,size*sizeof(wchar_t));used+=size;
    }
    for(unsigned missing=0;missing<2;missing++){
        swprintf_s(command,_countof(command),L"\"%ls\" --known-folders",image);STARTUPINFOW si={sizeof(si)};PROCESS_INFORMATION child={0};
        bool created=CreateProcessW(image,command,NULL,NULL,FALSE,CREATE_UNICODE_ENVIRONMENT|CREATE_NO_WINDOW,missing?without_drive:p.environment,NULL,&si,&child)!=0;CHECK(created);
        if(created){DWORD wait=WaitForSingleObject(child.hProcess,10000),code=STILL_ACTIVE;CHECK(wait==WAIT_OBJECT_0);CHECK(GetExitCodeProcess(child.hProcess,&code));CHECK(missing?code!=0:code==0);
            if(wait!=WAIT_OBJECT_0){CHECK(TerminateProcess(child.hProcess,ERROR_CANCELLED));CHECK(WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0);}CloseHandle(child.hThread);CloseHandle(child.hProcess);}
    }
}
int main(int argc,char** argv){
    if(argc==2 && !strcmp(argv[1],"--wait-child")){Sleep(300);return 9;}
    if(argc==2 && !strcmp(argv[1],"--known-folders")){PWSTR path=NULL;HRESULT hr=SHGetKnownFolderPath(&FOLDERID_ProgramData,KF_FLAG_DONT_VERIFY,NULL,&path);bool ok=SUCCEEDED(hr)&&path&&wcslen(path)>2&&path[1]==L':'&&path[2]==L'\\';CoTaskMemFree(path);return ok?0:87;}
    known_folder_children();
    engine.preflight=preflight;engine.execute=execute;request.target=L4_REMOTE_SUITE;strcpy_s(request.version,32,"latest");request.accepted_utc=1;asset.size=123;helper.size=456;
    L4Journal journal={0};journal.lock=(HANDLE)1;wcscpy_s(journal.directory,MAX_PATH,L"C:\\private\\12345678-1234-1234-1234-123456789abc");wcscpy_s(journal.layout.launchers,MAX_PATH,L"C:\\Program Files\\Leo4\\Tools\\bin");original.layout=journal.layout;
    SetupRemoteWorkerPolicy policy={0};policy.timeout_ms=10000;policy.cleanup_ms=1000;policy.recovery_ms=1000;policy.overhead_ms=2000;
    policy.armed_utc=1;policy.communication_deadline_utc=20000;policy.supervisor_deadline_utc=5000000000ull;
    policy.communication.verify_ms=policy.communication.worker_ms=policy.communication.lock_ms=policy.communication.proxy_ms=policy.communication.broker_prepare_ms=policy.communication.channel_ms=100;
    policy.communication.mosquitto_ms=300000;policy.communication.total_ms=400000;
    SetupInstalledSource* source=(SetupInstalledSource*)1;const SetupRemoteWorkerPlan* plan=(const SetupRemoteWorkerPlan*)1;SetupRemoteWorkerLaunch* p=NULL;
    enabled=false;CHECK(!setup_remote_worker_launch_prepare(&journal,source,plan,&policy,NULL,&p));CHECK(GetLastError()==ERROR_CALL_NOT_IMPLEMENTED && !p && !spawned && !checks_seen);
    enabled=true;engine.execute=NULL;CHECK(!setup_remote_worker_launch_prepare(&journal,source,plan,&policy,NULL,&p));CHECK(GetLastError()==ERROR_CALL_NOT_IMPLEMENTED && !p);engine.execute=execute;
    for(fault=1;fault<=6;fault++){CHECK(!setup_remote_worker_launch_prepare(&journal,source,plan,&policy,NULL,&p));CHECK(!p && !spawned);}
    fault=0;volatile LONG cancel=1;CHECK(!setup_remote_worker_launch_prepare(&journal,source,plan,&policy,&cancel,&p));CHECK(GetLastError()==ERROR_CANCELLED && !p);
    L4BootstrapChecks checks={0};checks.probe=probe;checks.barrier=barrier;
    CHECK(setup_remote_worker_launch_prepare(&journal,source,plan,&policy,NULL,&p));
    L4Journal* denied=&journal;SetupRemoteWorkerLaunchReport denied_report;enabled=false;
    CHECK(!setup_remote_worker_launch_start(&denied,source,p,&checks,NULL,&denied_report));CHECK(denied_report.error==ERROR_CALL_NOT_IMPLEMENTED && !spawned && !p->attempted && denied==&journal);
    enabled=true;CHECK(!setup_remote_worker_launch_start(&denied,source,p,&checks,&cancel,&denied_report));CHECK(denied_report.error==ERROR_CANCELLED && !spawned && !p->attempted);
    p->root[0]=1;CHECK(!setup_remote_worker_launch_start(&denied,source,p,&checks,NULL,&denied_report));CHECK(denied_report.error==ERROR_REVISION_MISMATCH && !spawned);
    CHECK(setup_remote_worker_launch_close(&p,1000));
    for(int stage=0;stage<=11;stage++){
        fault=0;CHECK(setup_remote_worker_launch_prepare(&journal,source,plan,&policy,NULL,&p));CHECK(p && p->sequence==14 && p->image.size==123);
        L4Journal* j=&journal;SetupRemoteWorkerLaunchReport report;unsigned before=spawned;fault=stage;
        if(stage==0){CHECK(setup_remote_worker_launch_start(&j,source,p,&checks,NULL,&report));CHECK(!j && report.transferred && p->worker && !report.error);CHECK(!source_check(&journal,source,p));CHECK(GetLastError()==ERROR_INVALID_STATE);
            DWORD code=0;CHECK(!setup_remote_worker_launch_wait(p,100,&code));CHECK(GetLastError()==ERROR_INVALID_HANDLE && code==STILL_ACTIVE && p->worker);
            wchar_t image[MAX_PATH],command[MAX_PATH+32];STARTUPINFOW si={0};PROCESS_INFORMATION pi={0};si.cb=sizeof(si);
            CHECK(GetModuleFileNameW(NULL,image,MAX_PATH)!=0);swprintf_s(command,MAX_PATH+32,L"\"%ls\" --wait-child",image);
            bool created=CreateProcessW(image,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)!=0;CHECK(created);
            if(created){observed_child=pi.hProcess;CHECK(!setup_remote_worker_launch_wait(p,20,&code));CHECK(GetLastError()==ERROR_TIMEOUT && code==STILL_ACTIVE && p->worker);
                CHECK(setup_remote_worker_launch_wait(p,5000,&code));CHECK(code==9 && p->worker && p->transferred);
                CHECK(setup_remote_worker_launch_wait(p,100,&code));CHECK(code==9);
                CHECK(setup_remote_worker_launch_wait(p,SETUP_REMOTE_CONTROLLER_WAIT_MS,&code));CHECK(code==9);
                CHECK(!setup_remote_worker_launch_wait(p,SETUP_REMOTE_CONTROLLER_WAIT_MS+1,&code));CHECK(GetLastError()==ERROR_INVALID_PARAMETER && code==STILL_ACTIVE);
                CloseHandle(pi.hThread);CloseHandle(pi.hProcess);observed_child=NULL;}
        }
        else if(stage==5){ /* Receipt was authenticated before pinning, not reacquired. */ CHECK(setup_remote_worker_launch_start(&j,source,p,&checks,NULL,&report));CHECK(!j && report.transferred);}
        else {CHECK(!setup_remote_worker_launch_start(&j,source,p,&checks,NULL,&report));CHECK(j==&journal && !report.transferred && report.error);if(stage<=4 || stage==6)CHECK(spawned==before);else {CHECK(!p->worker && closed);CHECK(stage==11?!report.recovery_reported && report.result_error==ERROR_WRITE_FAULT:report.recovery_reported && !report.result_error);}if(stage==11)CHECK(report.cleanup_error==ERROR_TIMEOUT);}
        fault=0;CHECK(setup_remote_worker_launch_close(&p,1000));CHECK(!p);
    }
    for(fault=12;fault<=13;fault++){
        int saved=fault;fault=0;CHECK(setup_remote_worker_launch_prepare(&journal,source,plan,&policy,NULL,&p));fault=saved;
        L4Journal* j=&journal;SetupRemoteWorkerLaunchReport report;unsigned before=transfers;p->policy.timeout_ms=100;late_cancel=0;
        CHECK(!setup_remote_worker_launch_start(&j,source,p,&checks,&late_cancel,&report));CHECK(j==&journal && !p->worker && p->attempted && transfers==before);
        CHECK(report.error==(DWORD)(saved==12?ERROR_TIMEOUT:ERROR_CANCELLED));
        CHECK(!setup_remote_worker_launch_start(&j,source,p,&checks,NULL,&report));CHECK(GetLastError()==ERROR_INVALID_PARAMETER);
        CHECK(setup_remote_worker_launch_close(&p,1000));
    }
    SetupRemoteWorkerPolicy invalid=policy;invalid.communication.mosquitto_ms=100;CHECK(!setup_remote_worker_launch_prepare(&journal,source,plan,&invalid,NULL,&p));CHECK(GetLastError()==ERROR_INVALID_PARAMETER && !p);
    invalid=policy;invalid.supervisor_deadline_utc=invalid.communication_deadline_utc+4000000000ull;CHECK(!setup_remote_worker_launch_prepare(&journal,source,plan,&invalid,NULL,&p));CHECK(GetLastError()==ERROR_INVALID_PARAMETER && !p);
    invalid=policy;invalid.overhead_ms=1999;CHECK(!setup_remote_worker_launch_prepare(&journal,source,plan,&invalid,NULL,&p));CHECK(GetLastError()==ERROR_INVALID_PARAMETER && !p);
    invalid=policy;invalid.overhead_ms=60001;CHECK(!setup_remote_worker_launch_prepare(&journal,source,plan,&invalid,NULL,&p));CHECK(GetLastError()==ERROR_INVALID_PARAMETER && !p);
    CHECK(communication && transfers && checks_seen);printf("Remote worker composition: %u passed, %u failed\n",passed,failed);return failed?1:0;
}
