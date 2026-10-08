#include "communication_monitor.h"
#include <stdlib.h>
#include <string.h>
#include <objbase.h>
struct L4CommunicationMonitor {
    L4Layout roots;L4CommunicationPin* pin;L4CommunicationSignals signals;
    L4UpdateState expected;HANDLE stop,thread,ready;SRWLOCK result_lock;ULONGLONG monotonic_end,monotonic_grace_end;
    L4CommunicationResult result;bool finished;
};
static bool fail(DWORD e){SetLastError(e);return false;}
static ULONGLONG utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static bool system_owner(void){HANDLE token=NULL;BYTE bytes[512];DWORD needed=0;
    HANDLE thread=NULL;if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}
    if(GetLastError()!=ERROR_NO_TOKEN)return false;
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,bytes,sizeof(bytes),&needed) && IsWellKnownSid(((TOKEN_USER*)bytes)->User.Sid,WinLocalSystemSid);
    if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool self(const L4CommunicationPlan* p){FILETIME c,e,k,u;
    return p && p->supervisor_pid==GetCurrentProcessId() && GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u) && !CompareFileTime(&c,&p->supervisor_created)?true:fail(ERROR_REVISION_MISMATCH);
}
/* UTC/Tick reads are sequential and their API grids are coarse. This fixed
 * allowance classifies skew only; it never authorizes an early UTC action or
 * extends the admitted deadline/live proof. A persistent clock rollback fails. */
#define L4_COMM_CLOCK_GRID_MS 32ull
static bool clock_sample(L4CommunicationMonitor* m,const L4CommunicationPlan* p,ULONGLONG* now,ULONGLONG* tick){
    *now=utc();*tick=GetTickCount64();
    if(*now<p->armed_utc)return fail(ERROR_TIME_SKEW);
    if(*tick>=m->monotonic_end){
        /* The tick may cross the deadline after the first UTC read. */
        *now=utc();
        if(*now<p->armed_utc)return fail(ERROR_TIME_SKEW);
        if(*now<p->deadline_utc && *tick>=m->monotonic_grace_end)return fail(ERROR_TIME_SKEW);
    }
    return true;
}
static DWORD WINAPI run(void* context){
    L4CommunicationMonitor* m=context;const L4CommunicationPlan* p=l4_communication_pinned_plan(m->pin);
    L4CommunicationResult result={L4_COMM_FAILED,ERROR_NOT_READY,0};bool observed=false;
    for(;;){
        if(WaitForSingleObject(m->stop,0)!=WAIT_TIMEOUT){result.error=ERROR_CANCELLED;break;}
        L4UpdateState s;if(!l4_update_state_read(&m->roots,&s)){result.error=GetLastError();break;}
        if(l4_communication_plan_matches(m->pin,&s))observed=true;
        else if(observed || s.window || s.generation!=p->generation-1){result.error=ERROR_REVISION_MISMATCH;break;}
        ULONGLONG now,tick;if(!clock_sample(m,p,&now,&tick)){result.error=GetLastError();break;}
        if(now>=p->deadline_utc){
            if(observed)l4_communication_execute(&m->roots,&m->expected,&m->signals,&result);
            /* No active marker by deadline means no claim/service action. */
            else result.error=ERROR_TIMEOUT;break;
        }
        if(!SetEvent(m->ready)){result.error=GetLastError();break;}
        /* UTC recheck caps forward jumps; fixed monotonic elapsed cap also
         * prevents a wall-clock adjustment from extending the admitted window. */
        ULONGLONG delta=p->deadline_utc-now;delta=delta/10000+(delta%10000!=0);DWORD ms=delta<100?(DWORD)delta:100;
        if(tick>=m->monotonic_end && now<p->deadline_utc){
            ULONGLONG remaining=m->monotonic_grace_end-tick;
            if(remaining<ms)ms=(DWORD)remaining;
        }
        if(WaitForSingleObject(m->stop,ms)!=WAIT_TIMEOUT){result.error=ERROR_CANCELLED;break;}
    }
    AcquireSRWLockExclusive(&m->result_lock);m->result=result;m->finished=true;ReleaseSRWLockExclusive(&m->result_lock);return 0;
}
bool l4_communication_monitor_start(const L4Layout* roots,const wchar_t* operation,const L4CommunicationSignals* signals,L4CommunicationMonitor** output){
    if(!output)return fail(ERROR_INVALID_PARAMETER);*output=NULL;
    if(!roots || !operation || !signals || !signals->probe || !signals->channels || !signals->barrier)return fail(ERROR_INVALID_PARAMETER);
    if(!system_owner())return false;
    L4CommunicationMonitor* m=calloc(1,sizeof(*m));if(!m)return fail(ERROR_NOT_ENOUGH_MEMORY);
    m->roots=*roots;m->signals=*signals;InitializeSRWLock(&m->result_lock);
    bool ok=l4_communication_plan_open(roots,operation,&m->pin);const L4CommunicationPlan* p=l4_communication_pinned_plan(m->pin);
    if(ok)ok=self(p) && l4_communication_native_budget_valid(&p->budget);
    ULONGLONG now=utc();if(ok && (now<p->armed_utc || now>=p->deadline_utc))ok=fail(ERROR_TIMEOUT);
    L4UpdateState s;if(ok)ok=l4_update_state_read(roots,&s) && (l4_communication_plan_matches(m->pin,&s) || (!s.window && s.generation==p->generation-1));
    if(ok){ULONGLONG delta=p->deadline_utc-now,ms=delta/10000+(delta%10000!=0),tick=GetTickCount64();
        if(tick>~0ull-L4_COMM_CLOCK_GRID_MS || ms>~0ull-tick-L4_COMM_CLOCK_GRID_MS)ok=fail(ERROR_ARITHMETIC_OVERFLOW);
        else{m->monotonic_end=tick+ms;m->monotonic_grace_end=m->monotonic_end+L4_COMM_CLOCK_GRID_MS;}
        m->expected.window=1;m->expected.generation=p->generation;m->expected.plan_sequence=p->sequence;m->expected.deadline_utc=p->deadline_utc;
        if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,operation,-1,m->expected.owner,40,NULL,NULL))ok=false;}
    for(unsigned i=0;ok && i<36;i++)if(m->expected.owner[i]>='A' && m->expected.owner[i]<='F')m->expected.owner[i]+=32;
    /* Wait state only. Existing STARTED/terminal/corrupt results cannot rearm. */
    L4CommunicationDecision* decision=NULL;
    if(ok)ok=l4_communication_decision_open(roots,operation,p->budget.verify_ms,&decision);
    L4CommunicationPhase phase;DWORD error;if(ok)ok=l4_communication_decision_read(decision,&phase,&error) && phase==L4_COMM_DEC_WAIT;
    l4_communication_decision_close(decision);
    if(ok){m->stop=CreateEventW(NULL,TRUE,FALSE,NULL);ok=m->stop!=NULL;}
    if(ok){m->ready=CreateEventW(NULL,TRUE,FALSE,NULL);ok=m->ready!=NULL;}
    if(ok){m->thread=CreateThread(NULL,0,run,m,0,NULL);ok=m->thread!=NULL;}
    if(!ok){DWORD code=GetLastError();if(m->ready)CloseHandle(m->ready);if(m->stop)CloseHandle(m->stop);l4_communication_plan_close(m->pin);free(m);return fail(code?code:ERROR_NOT_READY);}
    *output=m;return true;
}
bool l4_communication_monitor_poll(L4CommunicationMonitor* m,bool* finished,L4CommunicationResult* result){
    if(!m || !finished || !result)return fail(ERROR_INVALID_PARAMETER);
    AcquireSRWLockShared(&m->result_lock);*finished=m->finished;*result=m->result;ReleaseSRWLockShared(&m->result_lock);return true;
}
static bool monitor_current(L4CommunicationMonitor* m,const L4CommunicationPin* pin,const L4UpdateState* expected,HANDLE cancel){
    const L4CommunicationPlan* own=l4_communication_pinned_plan(m->pin),*other=l4_communication_pinned_plan(pin);
    ULONGLONG now=utc();bool finished=false;L4CommunicationResult result;
    if(cancel && WaitForSingleObject(cancel,0)!=WAIT_TIMEOUT)return fail(ERROR_CANCELLED);
    if(!self(own) || !other || memcmp(&own->operation,&other->operation,sizeof(GUID)) ||
        own->sequence!=other->sequence || own->generation!=other->generation || own->armed_utc!=other->armed_utc || own->deadline_utc!=other->deadline_utc ||
        own->worker_pid!=other->worker_pid || CompareFileTime(&own->worker_created,&other->worker_created) ||
        own->supervisor_pid!=other->supervisor_pid || CompareFileTime(&own->supervisor_created,&other->supervisor_created) ||
        memcmp(&own->budget,&other->budget,sizeof(own->budget)) || own->con_pid!=other->con_pid ||
        CompareFileTime(&own->con_created,&other->con_created) || strcmp(own->thumbprint,other->thumbprint) ||
        memcmp(own->operation_sha256,other->operation_sha256,32) || !l4_communication_plan_matches(m->pin,expected) ||
        !l4_communication_plan_matches(pin,expected))return fail(ERROR_REVISION_MISMATCH);
    if(now<own->armed_utc || now>=own->deadline_utc || GetTickCount64()>=m->monotonic_end)return fail(ERROR_TIMEOUT);
    if(!m->thread || !m->stop || !m->ready || WaitForSingleObject(m->thread,0)!=WAIT_TIMEOUT ||
        WaitForSingleObject(m->stop,0)!=WAIT_TIMEOUT || WaitForSingleObject(m->ready,0)!=WAIT_OBJECT_0 ||
        !l4_communication_monitor_poll(m,&finished,&result) || finished)return fail(ERROR_NOT_READY);
    L4UpdateState current;if(!l4_update_state_read(&m->roots,&current))return false;
    if(!l4_communication_plan_matches(m->pin,&current) && (current.window || current.generation!=own->generation-1))return fail(ERROR_REVISION_MISMATCH);
    return l4_communication_worker_live(own);
}
bool l4_communication_monitor_proof(L4CommunicationMonitor* m,const L4CommunicationPin* pin,const L4UpdateState* expected,DWORD timeout,HANDLE cancel){
    if(!m || !pin || !expected || !timeout || timeout>300000)return fail(ERROR_INVALID_PARAMETER);
    if(!system_owner() || !monitor_current(m,pin,expected,cancel))return false;
    ULONGLONG end=GetTickCount64()+timeout;const L4CommunicationPlan* p=l4_communication_pinned_plan(m->pin);
    ULONGLONG now=utc();if(now>=p->deadline_utc)return fail(ERROR_TIMEOUT);
    ULONGLONG delta=p->deadline_utc-now;DWORD remaining=delta/10000<timeout?(DWORD)(delta/10000):timeout;
    wchar_t operation[40];if(!remaining || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,m->expected.owner,-1,operation,40))return fail(ERROR_TIMEOUT);
    L4CommunicationDecision* d=NULL;bool ok=l4_communication_decision_open(&m->roots,operation,remaining,&d);
    L4CommunicationPhase phase;DWORD error;if(ok){ok=l4_communication_decision_read(d,&phase,&error);if(ok && phase!=L4_COMM_DEC_WAIT)ok=fail(ERROR_INVALID_STATE);}
    l4_communication_decision_close(d);
    if(ok)ok=GetTickCount64()<end && monitor_current(m,pin,expected,cancel);
    return ok?true:fail(GetLastError()?GetLastError():ERROR_NOT_READY);
}
bool l4_communication_monitor_close(L4CommunicationMonitor** output,DWORD timeout){
    if(!output || timeout>3600000)return fail(ERROR_INVALID_PARAMETER);L4CommunicationMonitor* m=*output;if(!m)return true;
    if(!SetEvent(m->stop))return false;
    if(WaitForSingleObject(m->thread,timeout)!=WAIT_OBJECT_0)return fail(ERROR_TIMEOUT);
    CloseHandle(m->thread);CloseHandle(m->stop);CloseHandle(m->ready);l4_communication_plan_close(m->pin);free(m);*output=NULL;return true;
}
