/* Actual child process/image/epoch/survivor + protected journal/file pins;
 * SCM and SYSTEM/native-KnownFolder/profile facts modeled, no service mutation. */
#include "../src/remote_service.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/tests/update_state_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sddl.h>
static unsigned checks,failures,controls,starts;static DWORD current_state,current_pid;
static bool side,token_ok=true;static int stop_fault,start_fault;static L4ServiceSwitch member;
static UpdateFixture fixture;static L4Journal* journal;static L4UpdateState fixture_state;
static HANDLE exit_event;static PROCESS_INFORMATION old_child,new_child,extra_child;
static wchar_t event_name[100],source_path[MAX_PATH],target_path[MAX_PATH],other_path[MAX_PATH];
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL service line%u %s error=%lu\n",__LINE__,#x,GetLastError());}}while(0)
bool l4_bootstrap_validate(const L4BootstrapPlan* p){(void)p;CHECK(false);SetLastError(ERROR_NOT_SUPPORTED);return false;}
static BOOL token_open(HANDLE p,DWORD a,PHANDLE t){(void)p;(void)a;*t=(HANDLE)(ULONG_PTR)3;return TRUE;}
static BOOL token_info(HANDLE t,TOKEN_INFORMATION_CLASS c,void* b,DWORD n,DWORD* used){(void)t;
    if(c==TokenUser){*used=sizeof(TOKEN_USER);if(n<*used)return FALSE;memset(b,0,*used);((TOKEN_USER*)b)->User.Sid=(PSID)1;return TRUE;}
    if(c==TokenSessionId){*used=sizeof(DWORD);if(n<*used)return FALSE;*(DWORD*)b=token_ok?0:1;return TRUE;}return FALSE;}
static BOOL wellknown(PSID s,WELL_KNOWN_SID_TYPE t){(void)s;(void)t;return token_ok;}
static BOOL thread_token(HANDLE t,DWORD a,BOOL self,PHANDLE token){(void)t;(void)a;(void)self;(void)token;SetLastError(ERROR_NO_TOKEN);return FALSE;}
static BOOL token_close(HANDLE h){return h==(HANDLE)(ULONG_PTR)3?TRUE:CloseHandle(h);}
static bool native_layout(L4Layout* out,const wchar_t* version){return l4_layout_from_roots(out,fixture.layout.binaries,fixture.layout.data,version);}
static SC_HANDLE manager_open(LPCWSTR a,LPCWSTR b,DWORD c){(void)a;(void)b;(void)c;return (SC_HANDLE)1;}
static SC_HANDLE service_open(SC_HANDLE a,LPCWSTR b,DWORD c){(void)a;(void)c;return !wcscmp(b,member.service)?(SC_HANDLE)2:NULL;}
static BOOL service_close(SC_HANDLE a){(void)a;return TRUE;}
static BOOL config_query(SC_HANDLE h,LPQUERY_SERVICE_CONFIGW c,DWORD n,DWORD* used){(void)h;*used=sizeof(*c);if(!c || n<*used){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    memset(c,0,sizeof(*c));c->dwServiceType=SERVICE_WIN32_OWN_PROCESS;c->dwStartType=SERVICE_AUTO_START;c->lpServiceStartName=L"LocalSystem";c->lpBinaryPathName=side?member.after:member.before.image_path;return TRUE;}
static BOOL status_query(SC_HANDLE h,SC_STATUS_TYPE t,BYTE* b,DWORD n,DWORD* used){(void)h;(void)t;*used=sizeof(SERVICE_STATUS_PROCESS);if(n<*used)return FALSE;SERVICE_STATUS_PROCESS* s=(SERVICE_STATUS_PROCESS*)b;memset(s,0,sizeof(*s));s->dwServiceType=SERVICE_WIN32_OWN_PROCESS;s->dwCurrentState=current_state;s->dwProcessId=current_pid;return TRUE;}
static BOOL control_service(SC_HANDLE h,DWORD code,LPSERVICE_STATUS s){(void)h;(void)code;memset(s,0,sizeof(*s));controls++;
    if(stop_fault==2){current_state=SERVICE_RUNNING;current_pid=extra_child.dwProcessId;return TRUE;}
    current_state=SERVICE_STOPPED;current_pid=0;if(stop_fault!=1)SetEvent(exit_event);return TRUE;}
static bool spawn(const wchar_t* image,PROCESS_INFORMATION* child){wchar_t command[600];swprintf_s(command,600,L"\"%ls\" --child \"%ls\"",image,event_name);STARTUPINFOW s={sizeof(s)};return CreateProcessW(image,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&s,child)!=0;}
static BOOL start_service(SC_HANDLE h,DWORD n,LPCWSTR* args){(void)h;(void)n;(void)args;starts++;if(start_fault==2){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}ResetEvent(exit_event);CHECK(spawn(start_fault==1?other_path:side?target_path:source_path,&new_child));
    current_state=start_fault==3?SERVICE_START_PENDING:SERVICE_RUNNING;current_pid=start_fault==3?0:new_child.dwProcessId;return TRUE;}
static bool apply(const L4ServiceSwitch* s){CHECK(s && current_state==SERVICE_STOPPED);side=true;if(start_fault==4){SetLastError(ERROR_WRITE_FAULT);return false;}return true;}
static bool rollback(const L4ServiceSwitch* s){CHECK(s && current_state==SERVICE_STOPPED);side=false;return true;}
#define OpenProcessToken token_open
#define GetTokenInformation token_info
#define IsWellKnownSid wellknown
#define OpenThreadToken thread_token
#define CloseHandle token_close
#define l4_layout_resolve native_layout
#define OpenSCManagerW manager_open
#define OpenServiceW service_open
#define CloseServiceHandle service_close
#define QueryServiceConfigW config_query
#define QueryServiceStatusEx status_query
#define ControlService control_service
#define StartServiceW start_service
#define l4_service_switch_apply apply
#define l4_service_switch_rollback rollback
#include "../src/remote_service.c"
#undef CloseHandle
static bool file_hash(const wchar_t* path,ULONGLONG* size,BYTE hash[32]){HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);if(f==INVALID_HANDLE_VALUE)return false;LARGE_INTEGER n;bool ok=GetFileSizeEx(f,&n) && n.QuadPart>0 && n.QuadPart<MAXDWORD;BYTE* b=ok?malloc((size_t)n.QuadPart):NULL;DWORD read=0;if(ok)ok=b && ReadFile(f,b,(DWORD)n.QuadPart,&read,NULL) && read==(DWORD)n.QuadPart && l4_store_hash(b,read,NULL,0,hash);free(b);CloseHandle(f);if(ok)*size=n.QuadPart;return ok;}
static bool protected_fixture(const wchar_t* path){PSECURITY_DESCRIPTOR sd=NULL;HANDLE previous=NULL;
    bool ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",SDDL_REVISION_1,&sd,NULL) && l4_layout_owner_begin(&previous);
    if(ok){ok=SetFileSecurityW(path,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd)!=0;bool ended=l4_layout_owner_end(previous);ok=ok && ended;}if(sd)LocalFree(sd);return ok;}
static void child_close(PROCESS_INFORMATION* p){if(p->hProcess){SetEvent(exit_event);CHECK(WaitForSingleObject(p->hProcess,5000)==WAIT_OBJECT_0);CloseHandle(p->hProcess);if(p->hThread)CloseHandle(p->hThread);memset(p,0,sizeof(*p));}}
static void scenario(unsigned mode){
    CHECK(update_fixture_init(&fixture));CHECK(update_fixture_put(&fixture,0));CHECK(l4_journal_open(&fixture.layout,L"17730000-0000-4000-8000-000000000001",true,&journal));
    memset(&member,0,sizeof(member));CHECK(l4_layout_from_roots(&member.layout,fixture.layout.binaries,fixture.layout.data,L"1.13.3"));CHECK(l4_layout_prepare(&member.layout));
    unsigned index=mode==11?3:mode==12?1:mode==13?2:0;
    const wchar_t* dirs[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};const wchar_t* exes[]={L"leo4proxy.exe",L"mosquitto.exe",L"l4con.exe",L"l4superv.exe"};const wchar_t* names[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};
    CHECK(CreateDirectoryW(fixture.layout.release,NULL));CHECK(protected_fixture(fixture.layout.release));CHECK(CreateDirectoryW(member.layout.release,NULL));CHECK(protected_fixture(member.layout.release));
    wchar_t dir[MAX_PATH],self[MAX_PATH];swprintf_s(dir,MAX_PATH,L"%ls\\%ls",fixture.layout.release,dirs[index]);CHECK(CreateDirectoryW(dir,NULL));CHECK(protected_fixture(dir));swprintf_s(dir,MAX_PATH,L"%ls\\%ls",member.layout.release,dirs[index]);CHECK(CreateDirectoryW(dir,NULL));CHECK(protected_fixture(dir));
    CHECK(l4_layout_component(&fixture.layout,dirs[index],exes[index],source_path));CHECK(l4_layout_component(&member.layout,dirs[index],exes[index],target_path));CHECK(GetModuleFileNameW(NULL,self,MAX_PATH));wcscpy_s(other_path,MAX_PATH,self);
    CHECK(CopyFileW(self,source_path,TRUE));CHECK(CopyFileW(self,target_path,TRUE));CHECK(protected_fixture(source_path));CHECK(protected_fixture(target_path));CHECK(file_hash(source_path,&member.before_size,member.before_sha256));CHECK(file_hash(target_path,&member.size,member.sha256));
    swprintf_s(event_name,100,L"Local\\l4-service-fixture-%lu-%llu",GetCurrentProcessId(),GetTickCount64());exit_event=CreateEventW(NULL,TRUE,FALSE,event_name);CHECK(exit_event);
    wcscpy_s(member.service,32,names[index]);member.before.installed=true;member.before.start_type=SERVICE_AUTO_START;wcscpy_s(member.before.account,256,L"LocalSystem");
    swprintf_s(member.before.image_path,2048,L"\"%ls\" --child \"%ls\"",source_path,event_name);swprintf_s(member.after,2048,L"\"%ls\" --child \"%ls\"",target_path,event_name);
    ULONGLONG reference;CHECK(l4_journal_save_switch(journal,&member,&reference));memset(&fixture_state,0,sizeof(fixture_state));strcpy_s(fixture_state.owner,40,"17730000-0000-4000-8000-000000000001");fixture_state.window=1;fixture_state.generation=1;fixture_state.plan_sequence=64;fixture_state.deadline_utc=utc()+600000000ull;
    BYTE encoded[L4_UPDATE_STATE_SIZE];CHECK(l4_update_state_encode(&fixture_state,encoded));CHECK(l4_update_state_replace(&fixture.layout,encoded,false));
    side=false;controls=starts=0;stop_fault=start_fault=0;token_ok=true;CHECK(spawn(source_path,&old_child));current_pid=old_child.dwProcessId;current_state=SERVICE_RUNNING;
    SetupRemoteService* service=NULL;
    if(mode==1){token_ok=false;CHECK(!setup_remote_service_open(journal,reference,&member,&fixture_state,10000,&service) && !service);token_ok=true;}
    else if(mode==2){L4ServiceSwitch bad=member;bad.size++;CHECK(!setup_remote_service_open(journal,reference,&bad,&fixture_state,10000,&service));CHECK(GetLastError()==ERROR_REVISION_MISMATCH && !service);}
    else {
        CHECK(setup_remote_service_open(journal,reference,&member,&fixture_state,10000,&service));if(service){DWORD pid=0;FILETIME birth;SetupRemoteServiceObservation observed;CHECK(setup_remote_service_running(service,&pid,&birth) && pid==old_child.dwProcessId);
            CHECK(setup_remote_service_observe(service,&observed) && observed.phase==SETUP_SERVICE_RUNNING_OLD && observed.pid==pid && !CompareFileTime(&observed.created,&birth) && observed.switch_reference==reference && !memcmp(&observed.state,&fixture_state,sizeof(fixture_state)));
            if(mode==11){L4UpdateState next=fixture_state;next.window=2;next.generation++;ULONGLONG recovery=next.deadline_utc+10000000ull;
                CHECK(!setup_remote_service_rebind_state(service,&next,recovery));CHECK(GetLastError()==ERROR_REVISION_MISMATCH);
                L4UpdateState bad=next;bad.generation++;CHECK(!setup_remote_service_rebind_state(service,&bad,recovery));CHECK(!setup_remote_service_rebind_state(service,&next,next.deadline_utc));
                CHECK(l4_update_state_encode(&next,encoded) && l4_update_state_replace(&fixture.layout,encoded,false));
                CHECK(setup_remote_service_rebind_state(service,&next,recovery));CHECK(service->pid==old_child.dwProcessId && service->expected.window==2);
                CHECK(!setup_remote_service_rebind_state(service,&next,recovery));fixture_state=next;}
            if(mode==3){stop_fault=1;CHECK(!setup_remote_service_stop(service,40));CHECK(GetLastError()==ERROR_TIMEOUT && WaitForSingleObject(old_child.hProcess,0)==WAIT_TIMEOUT);CHECK(!setup_remote_service_observe(service,&observed) && !observed.pid && controls==1);SetEvent(exit_event);CHECK(setup_remote_service_stop(service,1000));CHECK(setup_remote_service_observe(service,&observed) && observed.phase==SETUP_SERVICE_STOPPED_OLD);}
            else if(mode==4){CHECK(spawn(source_path,&extra_child));stop_fault=2;CHECK(!setup_remote_service_stop(service,1000));CHECK(GetLastError()==ERROR_REVISION_MISMATCH && service->pid==old_child.dwProcessId);CHECK(WaitForSingleObject(old_child.hProcess,0)==WAIT_TIMEOUT);CHECK(!setup_remote_service_observe(service,&observed));}
            else {
                CHECK(setup_remote_service_stop(service,5000));CHECK(WaitForSingleObject(old_child.hProcess,0)==WAIT_OBJECT_0 && controls==1);
                CHECK(setup_remote_service_observe(service,&observed) && observed.phase==SETUP_SERVICE_STOPPED_OLD && observed.pid==old_child.dwProcessId);
                if(mode==5){start_fault=4;CHECK(!setup_remote_service_switch(service,true,10000));CHECK(side);CHECK(setup_remote_service_switch(service,false,10000));CHECK(!side);start_fault=0;}
                CHECK(setup_remote_service_switch(service,true,10000));CHECK(side);
                CHECK(setup_remote_service_observe(service,&observed) && observed.phase==SETUP_SERVICE_STOPPED_TARGET && observed.pid==old_child.dwProcessId);
                if(mode==14){start_fault=2;CHECK(!setup_remote_service_start(service,true,1000) && GetLastError()==ERROR_ACCESS_DENIED);CHECK(!service->process && service->prior_process && !service->start_issued);CHECK(setup_remote_service_observe(service,&observed) && observed.phase==SETUP_SERVICE_STOPPED_TARGET && observed.pid==old_child.dwProcessId);CHECK(setup_remote_service_switch(service,false,10000));CHECK(setup_remote_service_observe(service,&observed) && observed.phase==SETUP_SERVICE_STOPPED_OLD);start_fault=0;CHECK(setup_remote_service_start(service,false,5000));CHECK(setup_remote_service_observe(service,&observed) && observed.phase==SETUP_SERVICE_RUNNING_OLD && observed.pid==new_child.dwProcessId);}
                else if(mode==6){ResetEvent(exit_event);CHECK(spawn(source_path,&extra_child));CHECK(!setup_remote_service_start(service,true,1000));CHECK(GetLastError()==ERROR_BUSY && !starts);CHECK(!setup_remote_service_observe(service,&observed));}
                else if(mode==7){start_fault=1;CHECK(!setup_remote_service_start(service,true,1000));CHECK(GetLastError()==ERROR_REVISION_MISMATCH && !service->process);}
                else if(mode==8 || mode==10){start_fault=3;CHECK(!setup_remote_service_start(service,true,1000));CHECK(GetLastError()==ERROR_TIMEOUT && !service->process && service->start_attempted && service->start_issued);
                    if(mode==8){current_state=SERVICE_RUNNING;current_pid=new_child.dwProcessId;CHECK(setup_remote_service_observe_start(service,1000));CHECK(setup_remote_service_running(service,&pid,&birth) && pid==new_child.dwProcessId);CHECK(starts==1);CHECK(setup_remote_service_observe(service,&observed) && observed.phase==SETUP_SERVICE_RUNNING_TARGET && observed.pid==pid);}
                    else{child_close(&new_child);Sleep(100);ResetEvent(exit_event);CHECK(spawn(target_path,&extra_child));current_state=SERVICE_RUNNING;current_pid=extra_child.dwProcessId;
                        CHECK(!setup_remote_service_observe_start(service,1000));CHECK(GetLastError()==ERROR_REVISION_MISMATCH && !service->process && starts==1);}}
                else if(mode==9){fixture_state.generation++;CHECK(l4_update_state_encode(&fixture_state,encoded) && l4_update_state_replace(&fixture.layout,encoded,false));CHECK(!setup_remote_service_start(service,true,1000));CHECK(GetLastError()==ERROR_REVISION_MISMATCH && !starts);}
                else {CHECK(setup_remote_service_start(service,true,5000));CHECK(setup_remote_service_running(service,&pid,&birth) && pid==new_child.dwProcessId && pid!=old_child.dwProcessId);CHECK(setup_remote_service_observe(service,&observed) && observed.phase==SETUP_SERVICE_RUNNING_TARGET && observed.pid==pid);
                    start_fault=0;CHECK(setup_remote_service_stop(service,5000));child_close(&new_child);CHECK(setup_remote_service_switch(service,false,10000));CHECK(setup_remote_service_start(service,false,5000));CHECK(setup_remote_service_running(service,&pid,&birth));CHECK(setup_remote_service_observe(service,&observed) && observed.phase==SETUP_SERVICE_RUNNING_OLD && observed.pid==pid);}
            }setup_remote_service_close(service);
        }
    }
    child_close(&old_child);child_close(&new_child);child_close(&extra_child);CloseHandle(exit_event);l4_journal_close(journal);journal=NULL;CHECK(update_fixture_dispose(&fixture));
}
int wmain(int argc,wchar_t** argv){if(argc==3 && !wcscmp(argv[1],L"--child")){HANDLE event=OpenEventW(SYNCHRONIZE,FALSE,argv[2]);if(!event)return 2;DWORD waited=WaitForSingleObject(event,30000);CloseHandle(event);return waited==WAIT_OBJECT_0?0:3;}
    for(unsigned i=0;i<15;i++)scenario(i);printf("Remote service adapter: %u checks, %u failures; actual process epochs/exits/survivors/file pins/journal, SCM/SYSTEM/native roots modeled; no installed services\n",checks,failures);return failures?1:0;}
