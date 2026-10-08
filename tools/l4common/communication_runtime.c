#include "communication_runtime.h"
#include "journal_internal.h"
#include "switch_decode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <tlhelp32.h>
typedef struct {
    const L4Layout* roots;const L4CommunicationPlan* plan;L4CommunicationPin* pin;
    const L4CommunicationSignals* signals;L4Journal* journal;
    L4CommunicationBoot* boot;const L4UpdateState* expected;
    L4ServiceSwitch pair[2];L4ReleaseFence* images[2];
    HANDLE processes[2];FILETIME created[2];DWORD pids[2];
} Native;
static bool fail(DWORD e){SetLastError(e);return false;}
static DWORD left(ULONGLONG end){ULONGLONG now=GetTickCount64();return now<end?(DWORD)(end-now):0;}
static ULONGLONG utc(void* context){(void)context;FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static ULONGLONG tick(void* context){(void)context;return GetTickCount64();}
static bool system_owner(void){HANDLE token=NULL;BYTE bytes[512];DWORD needed=0;
    HANDLE thread=NULL;if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}
    if(GetLastError()!=ERROR_NO_TOKEN)return false;
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,bytes,sizeof(bytes),&needed) && IsWellKnownSid(((TOKEN_USER*)bytes)->User.Sid,WinLocalSystemSid);
    if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool self_epoch(const L4CommunicationPlan* p){FILETIME c,e,k,u;
    return p->supervisor_pid==GetCurrentProcessId() && GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u) && !CompareFileTime(&c,&p->supervisor_created)?true:fail(ERROR_REVISION_MISMATCH);
}
static bool query(SC_HANDLE svc,const L4ServiceSwitch* p,bool old,SERVICE_STATUS_PROCESS* status){
    DWORD needed=0;QueryServiceConfigW(svc,NULL,0,&needed);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || needed<sizeof(QUERY_SERVICE_CONFIGW) || needed>65536)return fail(ERROR_INVALID_DATA);
    QUERY_SERVICE_CONFIGW* c=malloc(needed);if(!c)return fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=QueryServiceConfigW(svc,c,needed,&needed) && c->dwServiceType==SERVICE_WIN32_OWN_PROCESS && c->dwStartType==p->before.start_type &&
        c->lpServiceStartName && !wcscmp(c->lpServiceStartName,L"LocalSystem") && c->lpBinaryPathName &&
        (!wcscmp(c->lpBinaryPathName,p->before.image_path) || (!old && !wcscmp(c->lpBinaryPathName,p->after)));
    free(c);if(!ok)return fail(ERROR_REVISION_MISMATCH);
    return QueryServiceStatusEx(svc,SC_STATUS_PROCESS_INFO,(BYTE*)status,sizeof(*status),&needed)!=0;
}
static bool process_matches(HANDLE process,const L4ServiceSwitch* p,bool old){
    wchar_t image[MAX_PATH];DWORD size=MAX_PATH;const wchar_t *a=wcschr(p->before.image_path+1,L'"'),*b=wcschr(p->after+1,L'"');
    if(!a || !b || !QueryFullProcessImageNameW(process,0,image,&size))return false;
    bool match=(size==(DWORD)(a-p->before.image_path-1) && !_wcsnicmp(image,p->before.image_path+1,size)) ||
        (!old && size==(DWORD)(b-p->after-1) && !_wcsnicmp(image,p->after+1,size));
    if(!match)return fail(ERROR_REVISION_MISMATCH);
    HANDLE token=NULL;BYTE bytes[512];DWORD needed=0;bool ok=OpenProcessToken(process,TOKEN_QUERY,&token) &&
        GetTokenInformation(token,TokenUser,bytes,sizeof(bytes),&needed) && IsWellKnownSid(((TOKEN_USER*)bytes)->User.Sid,WinLocalSystemSid);
    if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);
}
/* Observe approved survivors even if SCM lost their PID; never terminate an
 * enumerated process. STOPPED alone is not permission to replace live bytes. */
static bool exclusive(const L4ServiceSwitch* p,DWORD allowed,ULONGLONG end){
    const wchar_t* exe=!wcscmp(p->service,L"Leo4Proxy")?L"leo4proxy.exe":L"mosquitto.exe";
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32W e={0};e.dwSize=sizeof(e);BOOL found=Process32FirstW(snap,&e);bool ok=found || GetLastError()==ERROR_NO_MORE_FILES;
    while(ok && found){if(e.th32ProcessID!=allowed && !_wcsicmp(e.szExeFile,exe)){
        HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,e.th32ProcessID);
        if(!h)ok=GetLastError()==ERROR_INVALID_PARAMETER;
        else{wchar_t image[MAX_PATH];DWORD n=MAX_PATH;bool read=QueryFullProcessImageNameW(h,0,image,&n)!=0;
            if(!read)ok=false;else{const wchar_t *a=wcschr(p->before.image_path+1,L'"'),*b=wcschr(p->after+1,L'"');
                bool approved=(a && n==(DWORD)(a-p->before.image_path-1) && !_wcsnicmp(image,p->before.image_path+1,n)) ||
                    (b && n==(DWORD)(b-p->after-1) && !_wcsnicmp(image,p->after+1,n));
                if(approved){DWORD ms=left(end);ok=ms && WaitForSingleObject(h,ms)==WAIT_OBJECT_0;if(!ok)SetLastError(ERROR_TIMEOUT);}}
            DWORD error=GetLastError();CloseHandle(h);if(!ok)SetLastError(error);}}
        if(ok && !left(end))ok=fail(ERROR_TIMEOUT);
        if(ok){found=Process32NextW(snap,&e);if(!found && GetLastError()!=ERROR_NO_MORE_FILES)ok=false;}}
    DWORD error=GetLastError();CloseHandle(snap);return ok?true:fail(error?error:ERROR_GEN_FAILURE);
}
static bool sleep_query(SC_HANDLE svc,const L4ServiceSwitch* p,bool old,SERVICE_STATUS_PROCESS* s,ULONGLONG end){
    DWORD ms=left(end);if(!ms)return fail(ERROR_TIMEOUT);Sleep(ms<10?ms:10);return query(svc,p,old,s);
}
static SC_HANDLE service(Native* n,unsigned i,DWORD access){
    SC_HANDLE m=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!m)return NULL;
    SC_HANDLE s=OpenServiceW(m,n->pair[i].service,SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|access);DWORD e=GetLastError();CloseServiceHandle(m);if(!s)SetLastError(e);return s;
}
static bool observe(Native* n,unsigned i,SC_HANDLE svc,SERVICE_STATUS_PROCESS* s,bool old){
    HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,s->dwProcessId);if(!h)return false;
    FILETIME c,e,k,u;bool ok=GetProcessTimes(h,&c,&e,&k,&u) && process_matches(h,&n->pair[i],old) && WaitForSingleObject(h,0)==WAIT_TIMEOUT;
    DWORD pid=s->dwProcessId;if(ok)ok=query(svc,&n->pair[i],old,s) && s->dwProcessId==pid;
    if(ok){if(n->processes[i])CloseHandle(n->processes[i]);n->processes[i]=h;n->pids[i]=pid;n->created[i]=c;}
    else{DWORD error=GetLastError();CloseHandle(h);SetLastError(error?error:ERROR_REVISION_MISMATCH);}return ok;
}
static bool stop(Native* n,unsigned i,DWORD ms){
    ULONGLONG end=tick(NULL)+ms;SC_HANDLE svc=service(n,i,SERVICE_STOP);if(!svc)return false;SERVICE_STATUS_PROCESS s={0};
    bool ok=query(svc,&n->pair[i],false,&s);
    while(ok && s.dwCurrentState!=SERVICE_STOPPED && !s.dwProcessId)ok=sleep_query(svc,&n->pair[i],false,&s,end);
    if(ok && s.dwCurrentState!=SERVICE_STOPPED){ok=observe(n,i,svc,&s,false);
        if(ok && s.dwCurrentState!=SERVICE_STOP_PENDING){SERVICE_STATUS status;ok=left(end) && ControlService(svc,SERVICE_CONTROL_STOP,&status);}}
    while(ok && s.dwCurrentState!=SERVICE_STOPPED){ok=sleep_query(svc,&n->pair[i],false,&s,end);
        if(ok && s.dwProcessId && s.dwProcessId!=n->pids[i])ok=fail(ERROR_REVISION_MISMATCH);}
    if(ok && n->processes[i]){DWORD budget=left(end);ok=budget && WaitForSingleObject(n->processes[i],budget)==WAIT_OBJECT_0;if(!ok)SetLastError(ERROR_TIMEOUT);}
    if(ok)ok=exclusive(&n->pair[i],0,end) && query(svc,&n->pair[i],false,&s) && s.dwCurrentState==SERVICE_STOPPED && left(end);
    DWORD error=GetLastError();CloseServiceHandle(svc);return ok?true:fail(error?error:ERROR_NOT_READY);
}
static bool start(Native* n,unsigned i,DWORD ms){
    ULONGLONG end=tick(NULL)+ms;SC_HANDLE svc=service(n,i,SERVICE_START);if(!svc)return false;SERVICE_STATUS_PROCESS s={0};
    bool ok=query(svc,&n->pair[i],true,&s) && s.dwCurrentState==SERVICE_STOPPED && exclusive(&n->pair[i],0,end) && left(end) && StartServiceW(svc,0,NULL);
    while(ok){ok=query(svc,&n->pair[i],true,&s);if(!ok || s.dwCurrentState==SERVICE_RUNNING)break;
        if(s.dwCurrentState!=SERVICE_START_PENDING){ok=fail(ERROR_SERVICE_NOT_ACTIVE);break;}ok=sleep_query(svc,&n->pair[i],true,&s,end);}
    if(ok)ok=s.dwProcessId && observe(n,i,svc,&s,true) && left(end);
    DWORD error=GetLastError();CloseServiceHandle(svc);return ok?true:fail(error?error:ERROR_NOT_READY);
}
static bool live(Native* n,unsigned i){
    SC_HANDLE svc=service(n,i,0);if(!svc)return false;SERVICE_STATUS_PROCESS s={0};FILETIME c,e,k,u;
    bool ok=n->processes[i] && query(svc,&n->pair[i],true,&s) && s.dwCurrentState==SERVICE_RUNNING && s.dwProcessId==n->pids[i] &&
        GetProcessTimes(n->processes[i],&c,&e,&k,&u) && !CompareFileTime(&c,&n->created[i]) && WaitForSingleObject(n->processes[i],0)==WAIT_TIMEOUT;
    DWORD error=GetLastError();CloseServiceHandle(svc);return ok?true:fail(error?error:ERROR_REVISION_MISMATCH);
}
static bool owner(Native* n){return n->boot?l4_communication_boot_check(n->boot,n->roots,n->expected):self_epoch(n->plan);}
static bool marker(Native* n){L4UpdateState s;return l4_update_state_read(n->roots,&s) && l4_communication_plan_matches(n->pin,&s) && (!n->boot || owner(n));}
static bool verify(const L4UpdateState* expected,DWORD ms,void* context){
    Native* n=context;ULONGLONG end=tick(NULL)+ms;
    if(!l4_communication_plan_matches(n->pin,expected) || !marker(n) || !owner(n))return false;
    for(unsigned i=0;i<2;i++){if(!n->images[i]){
        const wchar_t* command=n->pair[i].before.image_path;const wchar_t* q=wcschr(command+1,L'"');wchar_t image[MAX_PATH],prefix[MAX_PATH],version[32],actual[MAX_PATH];
        if(!q || q-command-1>=MAX_PATH)return fail(ERROR_INVALID_DATA);wcsncpy_s(image,MAX_PATH,command+1,(size_t)(q-command-1));
        swprintf_s(prefix,MAX_PATH,L"%ls\\releases\\",n->roots->binaries);size_t len=wcslen(prefix);const wchar_t* slash=wcschr(image+len,L'\\');
        if(wcsncmp(image,prefix,len) || !slash || slash-image-len>=32)return fail(ERROR_INVALID_DATA);
        wcsncpy_s(version,32,image+len,(size_t)(slash-image-len));L4Layout old;
        L4ReleaseFile f={i?L"mosquitto":L"leo4proxy",i?L"mosquitto.exe":L"leo4proxy.exe",n->pair[i].before_size,{0}};memcpy(f.sha256,n->pair[i].before_sha256,32);
        if(!l4_layout_from_roots(&old,n->roots->binaries,n->roots->data,version) || !l4_release_pin(&old,&f,&n->images[i],actual) || wcscmp(image,actual))return false;
    }
        SC_HANDLE svc=service(n,i,0);if(!svc)return false;SERVICE_STATUS_PROCESS s;bool ok=query(svc,&n->pair[i],false,&s);DWORD error=GetLastError();CloseServiceHandle(svc);if(!ok)return fail(error);
    }
    if(n->journal){BYTE* b=NULL;DWORD size=0;BYTE hash[32];bool ok=l4_store_find_record(n->journal,64,n->plan->sequence,&b,&size) &&
            l4_store_hash(b,size,NULL,0,hash) && !memcmp(hash,n->plan->operation_sha256,32);free(b);if(!ok)return fail(ERROR_REVISION_MISMATCH);
        for(unsigned i=0;i<2;i++){const DWORD kinds[]={10,20};for(unsigned k=0;k<2;k++){
            b=NULL;size=0;ULONGLONG seq=k?n->plan->config_sequence[i]:n->plan->switch_sequence[i];
            const BYTE* copy=k?n->plan->configs[i]:n->plan->switches[i];DWORD record_size=k?n->plan->config_size[i]:n->plan->switch_size[i];
            ok=l4_store_find_record(n->journal,kinds[k],seq,&b,&size) && size==record_size && !memcmp(b,copy,record_size);free(b);if(!ok)return fail(ERROR_REVISION_MISMATCH);}}}
    return left(end) && marker(n)?true:fail(ERROR_TIMEOUT);
}
static bool worker(DWORD ms,void* context){Native* n=context;return l4_communication_worker_exit(n->plan,ms);}
static bool lock(DWORD ms,void* context){Native* n=context;ULONGLONG end=tick(NULL)+ms;wchar_t id[40];L4UpdateState s;
    if(!l4_update_state_read(n->roots,&s) || !l4_communication_plan_matches(n->pin,&s) || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.owner,-1,id,40))return false;
    do{if(l4_journal_open(n->roots,id,false,&n->journal))return true;
        DWORD error=GetLastError();if(error!=ERROR_SHARING_VIOLATION && error!=ERROR_LOCK_VIOLATION)return false;
        DWORD budget=left(end);if(budget)Sleep(budget<10?budget:10);
    }while(left(end));return fail(ERROR_TIMEOUT);
}
static void unlock(void* context){Native* n=context;l4_journal_close(n->journal);n->journal=NULL;}
static bool step(L4CommunicationStep op,DWORD ms,void* context){Native* n=context;ULONGLONG end=tick(NULL)+ms;bool ok=marker(n);
    if(!ok)return false;
    switch(op){
    case L4_COMM_STOP_PROXY:ok=stop(n,0,ms);break;
    case L4_COMM_STOP_MOSQUITTO:ok=live(n,0) && stop(n,1,ms);break;
    case L4_COMM_RESTORE_PROXY:ok=l4_service_switch_rollback(&n->pair[0]);break;
    case L4_COMM_RESTORE_MOSQUITTO:
        {SC_HANDLE svc=service(n,1,0);SERVICE_STATUS_PROCESS s={0};ok=svc && query(svc,&n->pair[1],false,&s) && s.dwCurrentState==SERVICE_STOPPED;
        if(svc)CloseServiceHandle(svc);}
        ok=ok && live(n,0) && l4_config_rollback(n->journal,n->plan->config_sequence[0]) && left(end) &&
            l4_config_rollback(n->journal,n->plan->config_sequence[1]) && left(end) && l4_service_switch_rollback(&n->pair[1]);break;
    case L4_COMM_START_PROXY:ok=start(n,0,ms);break;
    case L4_COMM_START_MOSQUITTO:ok=live(n,0) && start(n,1,ms);break;
    case L4_COMM_PROBE_PROXY:case L4_COMM_PROBE_MOSQUITTO:{unsigned i=op==L4_COMM_PROBE_PROXY?0:1;
        ok=live(n,i) && n->signals->probe(i,n->pids[i],ms,n->signals->context) && live(n,i);break;}
    case L4_COMM_PROXY_CHANNELS:ok=live(n,0) && n->signals->channels(n->pids[0],ms,n->signals->context) && live(n,0);break;
    case L4_COMM_FINAL_BARRIER:ok=live(n,0) && live(n,1) && n->signals->barrier(ms,n->signals->context) && live(n,0) && live(n,1) &&
        l4_config_verify(n->journal,n->plan->config_sequence[0],false) && l4_config_verify(n->journal,n->plan->config_sequence[1],false);break;
    default:return fail(ERROR_INVALID_PARAMETER);}
    if(ok && (!left(end) || !marker(n)))return fail(ERROR_TIMEOUT);return ok;
}
static bool execute(const L4Layout* roots,const L4UpdateState* expected,const L4CommunicationSignals* signals,L4CommunicationBoot* boot,L4CommunicationResult* result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);memset(result,0,sizeof(*result));result->error=ERROR_INVALID_PARAMETER;
    ULONGLONG start_tick=tick(NULL);
    if(!roots || !expected || expected->owner[36] || expected->owner[37] || expected->owner[38] || expected->owner[39] ||
       !signals || !signals->probe || !signals->channels || !signals->barrier)return fail(ERROR_INVALID_PARAMETER);
    if(!system_owner()){result->error=GetLastError();return false;}
    if(boot && !l4_communication_boot_take(boot,&start_tick)){result->error=GetLastError();return false;}
    Native n={0};n.roots=roots;n.signals=signals;n.boot=boot;n.expected=expected;wchar_t id[40];L4CommunicationDecision* d=NULL;
    bool ok=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,expected->owner,-1,id,40)!=0 && l4_communication_plan_open(roots,id,&n.pin) && l4_communication_plan_matches(n.pin,expected);
    n.plan=l4_communication_pinned_plan(n.pin);
    ULONGLONG full_end=0,claim_end=0;
    if(ok){ok=owner(&n) && l4_communication_native_budget_valid(&n.plan->budget);
        full_end=start_tick+n.plan->budget.total_ms;claim_end=start_tick+n.plan->budget.verify_ms;
        if(ok && (full_end<start_tick || !left(claim_end)))ok=fail(ERROR_TIMEOUT);}
    for(unsigned i=0;ok && i<2;i++)ok=l4_switch_decode_bytes(roots,n.plan->switches[i],n.plan->switch_size[i],&n.pair[i]);
    if(ok){DWORD ms=left(claim_end);ok=ms && l4_communication_decision_open(roots,id,ms,&d) && owner(&n) && left(claim_end) && l4_communication_decision_begin(d,utc(NULL),boot!=NULL);}
    bool claimed=ok;
    if(ok){l4_communication_decision_release(d);L4CommunicationAttempt a={0};L4CommunicationRecoveryOps ops={tick,utc,verify,worker,lock,unlock,step,&n};
        L4CommunicationBudget execution=n.plan->budget;DWORD ms=left(full_end);
        if(!left(claim_end) || ms<=n.plan->budget.verify_ms){ok=false;result->error=ERROR_TIMEOUT;}
        else{execution.total_ms=ms-n.plan->budget.verify_ms;ok=l4_communication_recover(&a,expected,&execution,&ops,boot!=NULL,result);}}
    DWORD error=ok?0:(result->error!=ERROR_INVALID_PARAMETER?result->error:GetLastError());if(!ok && !error)error=ERROR_GEN_FAILURE;
    if(claimed){L4CommunicationPhase phase=ok?L4_COMM_DEC_RESTORED:result->outcome==L4_COMM_CONNECTIVITY_UNCONFIRMED?L4_COMM_DEC_UNCONFIRMED:L4_COMM_DEC_FAILED;
        DWORD ms=left(full_end);if(ms>n.plan->budget.verify_ms)ms=n.plan->budget.verify_ms;
        if(!ms || !l4_communication_decision_relock(d,ms) || !l4_communication_decision_finish(d,phase,ok?0:error,utc(NULL)) || !left(full_end)){
            ok=false;error=left(full_end)?GetLastError():ERROR_TIMEOUT;result->outcome=L4_COMM_FAILED;}}
    for(unsigned i=0;i<2;i++){if(n.processes[i])CloseHandle(n.processes[i]);l4_release_unpin(n.images[i]);}
    if(n.journal)unlock(&n);l4_communication_decision_close(d);l4_communication_plan_close(n.pin);
    result->error=ok?0:error;return ok?true:fail(error);
}
bool l4_communication_execute(const L4Layout* roots,const L4UpdateState* expected,const L4CommunicationSignals* signals,L4CommunicationResult* result){
    return execute(roots,expected,signals,NULL,result);
}
bool l4_communication_execute_boot(const L4Layout* roots,const L4UpdateState* expected,const L4CommunicationSignals* signals,L4CommunicationBoot* boot,L4CommunicationResult* result){
    if(!boot){if(result){memset(result,0,sizeof(*result));result->error=ERROR_INVALID_PARAMETER;}return fail(ERROR_INVALID_PARAMETER);}
    return execute(roots,expected,signals,boot,result);
}
