#include "communication_watch.h"
#include "communication_signals.h"
#include "../../l4common/communication_monitor.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/update_state_internal.h"
#include "../../l4common/supervisor_crash_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <objbase.h>
/* Private compiled registration. No RPC/config/test-result admission bit and no
 * public setter: the very callbacks used for restart recovery must be retained
 * by the current owner before mode3 can attest protection. */
typedef struct {
    bool (*open)(const L4Layout*,const L4UpdateState*,L4CommunicationBoot**);
    bool (*signals)(const L4Layout*,const L4CommunicationPin*,L4CommunicationBoot*,const L4UpdateState*,L4SignalProfile**,L4CommunicationSignals*);
    bool (*execute)(const L4Layout*,const L4UpdateState*,const L4CommunicationSignals*,L4CommunicationBoot*,L4CommunicationResult*);
} NativeBootRegistration;
static const NativeBootRegistration native_boot={l4_communication_boot_open,supervisor_signals_open_boot,l4_communication_execute_boot};
struct L4CommunicationWatch {
    L4Layout roots;HANDLE stop,thread;SRWLOCK state_lock;
    L4CommunicationMonitor* monitor;L4SignalProfile* profile;L4CommunicationPin* pin;
    const NativeBootRegistration* boot;
    DWORD error;bool stopped,boot_owned,startup_checked;
};
static bool fail(DWORD e){SetLastError(e);return false;}
static bool boot_registered(const L4CommunicationWatch* w){return w && w->boot==&native_boot && w->boot->open && w->boot->signals && w->boot->execute;}
static ULONGLONG utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static bool system_owner(void){HANDLE token=NULL,thread=NULL;BYTE user[512];DWORD size=0;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}if(GetLastError()!=ERROR_NO_TOKEN)return false;
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,user,sizeof(user),&size) && IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid);
    if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool original(const L4CommunicationPin* pin,const L4UpdateState* s){const L4CommunicationPlan* p=l4_communication_pinned_plan(pin);FILETIME c,e,k,u;
    ULONGLONG now=utc();return p && p->supervisor_pid==GetCurrentProcessId() && GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u) && !CompareFileTime(&c,&p->supervisor_created) &&
        l4_communication_native_budget_valid(&p->budget) && now>=p->armed_utc && now<p->deadline_utc &&
        (l4_communication_plan_matches(pin,s) || (!s->window && s->generation==p->generation-1));
}
static bool uuid_name(const wchar_t* name){if(wcslen(name)!=36)return false;wchar_t text[40],canonical[40];GUID id;
    swprintf_s(text,40,L"{%ls}",name);return SUCCEEDED(CLSIDFromString(text,&id)) && StringFromGUID2(&id,canonical,40)==39 && !_wcsnicmp(name,canonical+1,36);
}
/* Bounded history scan. Old/foreign plans cannot pass original epoch/generation.
 * More than one eligible original plan refuses instead of choosing a winner.
 * A matching active owner is addressed directly, without history enumeration. */
static bool discover(L4CommunicationWatch* w,L4CommunicationPin** output,wchar_t operation[40]){
    *output=NULL;L4UpdateState s;if(!l4_update_state_read(&w->roots,&s))return false;
    if(s.window){if(s.window!=L4_UPDATE_COMMUNICATION)return fail(ERROR_NOT_READY);
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.owner,-1,operation,40))return false;
        if(!l4_communication_plan_open(&w->roots,operation,output))return false;
        if(original(*output,&s))return true;l4_communication_plan_close(*output);*output=NULL;return fail(ERROR_NOT_READY);}
    L4FileFence parents={0};if(!l4_update_state_pin(&w->roots,&parents))return false;
    wchar_t pattern[MAX_PATH];if(swprintf_s(pattern,MAX_PATH,L"%ls\\*",w->roots.operations)<0){l4_store_unpin(&parents);return fail(ERROR_FILENAME_EXCED_RANGE);}
    WIN32_FIND_DATAW data;HANDLE search=FindFirstFileW(pattern,&data);bool ok=search!=INVALID_HANDLE_VALUE;unsigned seen=0;
    if(!ok && GetLastError()==ERROR_FILE_NOT_FOUND)ok=true;
    if(search!=INVALID_HANDLE_VALUE){do{if(!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L".."))continue;
        if(++seen>256){ok=fail(ERROR_TOO_MANY_NAMES);break;}
        if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT || !uuid_name(data.cFileName))continue;
        L4CommunicationPin* p=NULL;if(l4_communication_plan_open(&w->roots,data.cFileName,&p) && original(p,&s)){
            if(*output){l4_communication_plan_close(p);ok=fail(ERROR_DUP_NAME);break;}*output=p;wcscpy_s(operation,40,data.cFileName);
        }else l4_communication_plan_close(p);
    }while(FindNextFileW(search,&data));if(ok && GetLastError()!=ERROR_NO_MORE_FILES)ok=false;FindClose(search);}
    DWORD error=GetLastError();l4_store_unpin(&parents);if(!ok){l4_communication_plan_close(*output);*output=NULL;return fail(error);}
    return *output!=NULL?true:fail(ERROR_NOT_READY);
}
/* One startup snapshot, directly addressed active owner only. A foreign old
 * epoch is never routed through the ordinary original-owner monitor. Failure
 * retains ownership until process exit; no automatic re-admission timer. */
static bool startup_reconcile(L4CommunicationWatch* w,bool* checked){
    *checked=false;L4UpdateState state;if(!boot_registered(w) || !l4_update_state_read(&w->roots,&state))return false;
    if(state.window!=L4_UPDATE_COMMUNICATION){*checked=true;return false;}
    wchar_t operation[40];L4CommunicationPin* pin=NULL;
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,state.owner,-1,operation,40) || !l4_communication_plan_open(&w->roots,operation,&pin))return false;
    const L4CommunicationPlan* p=l4_communication_pinned_plan(pin);FILETIME c,e,k,u;
    bool own=p->supervisor_pid==GetCurrentProcessId() && GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u) && !CompareFileTime(&c,&p->supervisor_created);
    if(own){*checked=original(pin,&state);l4_communication_plan_close(pin);return false;}
    w->boot_owned=true;L4CommunicationBoot* permit=NULL;L4SignalProfile* profile=NULL;L4CommunicationSignals signals={0};L4CommunicationResult result={0};
    bool ok=WaitForSingleObject(w->stop,0)==WAIT_TIMEOUT && l4_communication_plan_matches(pin,&state) &&
        w->boot->open(&w->roots,&state,&permit) && w->boot->signals(&w->roots,pin,permit,&state,&profile,&signals) &&
        (WaitForSingleObject(w->stop,0)==WAIT_TIMEOUT?w->boot->execute(&w->roots,&state,&signals,permit,&result):fail(ERROR_CANCELLED));
    DWORD code=ok?ERROR_NOT_SUPPORTED:GetLastError();
    /* Synchronous executor has returned; only now release signal/image/runner
     * pins. STOP/close timeout never detaches this native call. */
    supervisor_signals_close(profile);l4_communication_boot_close(permit);l4_communication_plan_close(pin);
    AcquireSRWLockExclusive(&w->state_lock);w->error=code?code:ERROR_NOT_READY;ReleaseSRWLockExclusive(&w->state_lock);return true;
}
static DWORD WINAPI run(void* context){L4CommunicationWatch* w=context;
    bool checked=false;startup_reconcile(w,&checked);
    AcquireSRWLockExclusive(&w->state_lock);w->startup_checked=checked;ReleaseSRWLockExclusive(&w->state_lock);
    while(WaitForSingleObject(w->stop,0)==WAIT_TIMEOUT){
        if(w->monitor){bool done=false;L4CommunicationResult result;
            if(l4_communication_monitor_poll(w->monitor,&done,&result) && done){
                AcquireSRWLockExclusive(&w->state_lock);w->error=result.error?result.error:ERROR_NOT_SUPPORTED;ReleaseSRWLockExclusive(&w->state_lock);
                /* Keep completed ownership. Never retry/adopt another plan in
                 * this supervisor lifetime after cancellation/terminal result. */
            }
        }else if(w->startup_checked && !w->boot_owned){L4CommunicationPin* pin=NULL;wchar_t operation[40];DWORD code=ERROR_NOT_READY;
            if(discover(w,&pin,operation)){
                const L4CommunicationPlan* p=l4_communication_pinned_plan(pin);L4CommunicationDecision* d=NULL;L4CommunicationPhase phase;DWORD error;
                bool ok=l4_communication_worker_live(p) && l4_communication_decision_open(&w->roots,operation,p->budget.verify_ms,&d) &&
                    l4_communication_decision_read(d,&phase,&error) && phase==L4_COMM_DEC_WAIT;
                l4_communication_decision_close(d);L4SignalProfile* profile=NULL;L4CommunicationSignals signals={0};L4CommunicationMonitor* monitor=NULL;
                if(ok)ok=supervisor_signals_open(&w->roots,pin,p->budget.verify_ms,&profile,&signals) && l4_communication_worker_live(p) &&
                    l4_communication_monitor_start(&w->roots,operation,&signals,&monitor);
                code=ok?ERROR_NOT_SUPPORTED:GetLastError();
                if(ok){AcquireSRWLockExclusive(&w->state_lock);w->pin=pin;w->profile=profile;w->monitor=monitor;w->error=ERROR_NOT_SUPPORTED;ReleaseSRWLockExclusive(&w->state_lock);pin=NULL;profile=NULL;}
                supervisor_signals_close(profile);
            }else code=GetLastError();l4_communication_plan_close(pin);
            AcquireSRWLockExclusive(&w->state_lock);w->error=code?code:ERROR_NOT_READY;ReleaseSRWLockExclusive(&w->state_lock);
        }
        if(WaitForSingleObject(w->stop,100)!=WAIT_TIMEOUT)break;
    }
    /* Signals/pins remain owned until native executor actually exits. STOP does
     * not detach a claimed runner or free callback context on a timed-out wait. */
    for(;;){AcquireSRWLockExclusive(&w->state_lock);
        bool monitor_closed=!w->monitor || l4_communication_monitor_close(&w->monitor,1000);
        if(!monitor_closed)w->error=GetLastError();ReleaseSRWLockExclusive(&w->state_lock);if(monitor_closed)break;
    }
    AcquireSRWLockExclusive(&w->state_lock);supervisor_signals_close(w->profile);w->profile=NULL;l4_communication_plan_close(w->pin);w->pin=NULL;
    w->stopped=true;ReleaseSRWLockExclusive(&w->state_lock);return 0;
}
bool supervisor_communication_watch_start(const L4UpdateConsumer* consumer,L4CommunicationWatch** output){
    if(!output)return fail(ERROR_INVALID_PARAMETER);*output=NULL;if(!consumer || !consumer->enabled)return fail(ERROR_NOT_SUPPORTED);if(!system_owner())return false;
    L4CommunicationWatch* w=calloc(1,sizeof(*w));if(!w)return fail(ERROR_NOT_ENOUGH_MEMORY);w->roots=consumer->layout;w->boot=&native_boot;InitializeSRWLock(&w->state_lock);w->error=ERROR_NOT_READY;
    w->stop=CreateEventW(NULL,TRUE,FALSE,NULL);if(w->stop)w->thread=CreateThread(NULL,0,run,w,0,NULL);
    if(!w->thread){DWORD code=GetLastError();if(w->stop)CloseHandle(w->stop);free(w);return fail(code);}*output=w;return true;
}
DWORD supervisor_communication_watch_proof(L4CommunicationWatch* w,const L4UpdateState* expected,DWORD timeout,HANDLE cancel){
    if(!expected || expected->window!=L4_UPDATE_COMMUNICATION || !timeout || timeout>300000)return ERROR_INVALID_PARAMETER;
    if(cancel && WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT)return ERROR_CANCELLED;
    if(!w)return ERROR_NOT_SUPPORTED;
    if(!system_owner())return GetLastError();ULONGLONG end=GetTickCount64()+timeout;
    while(!TryAcquireSRWLockShared(&w->state_lock)){
        if(cancel && WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT)return ERROR_CANCELLED;
        if(GetTickCount64()>=end)return ERROR_TIMEOUT;Sleep(1);
    }
    DWORD result=ERROR_NOT_READY;L4UpdateState state;
    bool ok=boot_registered(w) && w->startup_checked && !w->boot_owned && !w->stopped && w->monitor && w->pin && w->profile &&
        w->thread && WaitForSingleObject(w->thread,0)==WAIT_TIMEOUT && WaitForSingleObject(w->stop,0)==WAIT_TIMEOUT &&
        l4_update_state_read(&w->roots,&state) && original(w->pin,&state) && l4_communication_plan_matches(w->pin,expected);
    ULONGLONG now=GetTickCount64();if(ok && now<end){
        const L4CommunicationPlan* plan=l4_communication_pinned_plan(w->pin);
        ULONGLONG current=utc();DWORD budget=plan->budget.verify_ms;
        ULONGLONG remaining=current<plan->deadline_utc?(plan->deadline_utc-current)/10000:0;
        if(remaining<budget)budget=(DWORD)remaining;
        ok=budget && l4_supervisor_crash_profile_current(plan->supervisor_pid,budget);
        now=GetTickCount64();if(ok)ok=now<end && l4_communication_monitor_proof(w->monitor,w->pin,expected,(DWORD)(end-now),cancel);
        if(!ok){result=GetLastError();if(!result)result=ERROR_NOT_READY;}
        if(ok)ok=boot_registered(w) && GetTickCount64()<end && WaitForSingleObject(w->stop,0)==WAIT_TIMEOUT &&
            l4_update_state_read(&w->roots,&state) && original(w->pin,&state) && l4_communication_plan_matches(w->pin,expected) &&
            l4_supervisor_crash_profile_current(plan->supervisor_pid,budget) && GetTickCount64()<end;
        if(ok)result=ERROR_SUCCESS;
    }else if(now>=end)result=ERROR_TIMEOUT;
    ReleaseSRWLockShared(&w->state_lock);return result?result:ERROR_SUCCESS;
}
DWORD supervisor_communication_watch_query(L4CommunicationWatch* w,const L4UpdateState* expected,DWORD timeout,HANDLE cancel){
    if(!expected || expected->window!=L4_UPDATE_COMMUNICATION || !timeout || timeout>300000)return ERROR_INVALID_PARAMETER;
    if(cancel && WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT)return ERROR_CANCELLED;
    if(!w)return ERROR_NOT_SUPPORTED;
    /* Technical protection only. Actual retained observer/worker/WAIT/epoch and
     * current SCM restart policy are freshly proved; no arming/scan/renewal.
     * Whole-update acceptance and remote executor activation remain separate. */
    return supervisor_communication_watch_proof(w,expected,timeout,cancel);
}
bool supervisor_communication_watch_close(L4CommunicationWatch** output,DWORD timeout){
    if(!output || timeout>3600000)return fail(ERROR_INVALID_PARAMETER);L4CommunicationWatch* w=*output;if(!w)return true;
    if(!SetEvent(w->stop))return false;if(WaitForSingleObject(w->thread,timeout)!=WAIT_OBJECT_0)return fail(ERROR_TIMEOUT);
    CloseHandle(w->thread);CloseHandle(w->stop);free(w);*output=NULL;return true;
}
