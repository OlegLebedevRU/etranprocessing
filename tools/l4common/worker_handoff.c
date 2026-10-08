#include "worker_handoff_internal.h"
#include "journal_internal.h"
#include "update_state.h"
#include "update_state_internal.h"
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TICKET_SIZE 144u
static bool fail(DWORD error){SetLastError(error);return false;}
static ULONGLONG utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static bool system_user(void){HANDLE token=NULL;BYTE user[512];DWORD count=0;bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,user,sizeof(user),&count) && IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid);if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);}
static bool live(HANDLE process,DWORD pid,const FILETIME* epoch){FILETIME c,e,k,u;return process && GetProcessId(process)==pid && GetProcessTimes(process,&c,&e,&k,&u) && !CompareFileTime(&c,epoch) && WaitForSingleObject(process,0)==WAIT_TIMEOUT?true:fail(ERROR_INVALID_STATE);}
static bool clear(L4Journal* j,ULONGLONG generation,bool exact,L4UpdateState* out){if(!l4_update_state_read(&j->layout,out))return false;return !out->window && (!exact || generation==out->generation)?true:fail(ERROR_INVALID_STATE);}
static bool guard(L4Journal* j,L4RecoveryGuard** out){GUID id;memcpy(&id,j->header+8,16);wchar_t uuid[40],operation[40];if(StringFromGUID2(&id,uuid,40)!=39)return fail(ERROR_INVALID_DATA);wcsncpy_s(operation,40,uuid+1,36);return l4_recovery_open(&j->layout,operation,1000,out);}
static bool waiting(L4RecoveryGuard* g){L4RecoveryAction action;if(!l4_recovery_action(g,utc(),false,&action))return false;return action==L4_RECOVERY_WAIT?true:fail(ERROR_INVALID_STATE);}
static bool digest(const L4Layout* roots,const L4RecoveryPlan* plan,BYTE hash[32]){BYTE* bytes=NULL;DWORD size=0;if(!l4_recovery_encode(roots,plan,&bytes,&size))return false;memcpy(hash,bytes+size-32,32);free(bytes);return true;}
typedef struct {unsigned tickets,receipts;ULONGLONG ticket_sequence,receipt_sequence;} History;
static bool count(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){(void)bytes;(void)size;History* h=context;if(kind==68){h->tickets++;h->ticket_sequence=sequence;}if(kind==69){h->receipts++;h->receipt_sequence=sequence;}return true;}
static bool history(L4Journal* j,unsigned mode){History h={0};if(!l4_journal_replay(j,count,&h))return false;return h.tickets==(mode?1u:0u) && h.receipts==(mode==2?1u:0u)?true:fail(ERROR_ALREADY_EXISTS);}
bool l4_worker_transfer_checked(L4Journal** pointer,L4WorkerJob* worker,const L4RecoveryHelper* helper,DWORD overhead,L4HandoffAudit audit,void* context){
    if(!pointer || !*pointer || !worker || !helper || !audit)return fail(ERROR_INVALID_PARAMETER);L4Journal* j=*pointer;
    if(!history(j,false))return false;
    L4RecoveryGuard* g=NULL;if(!guard(j,&g))return false;L4RecoveryPlan plan=*l4_recovery_plan(g);L4UpdateState state={0};L4RecoveryTask spec;
    bool ok=waiting(g) && l4_worker_job_verify(worker,&plan) && clear(j,0,false,&state) && l4_recovery_task_spec(&j->layout,&plan,helper,overhead,&spec);
    BYTE ticket[TICKET_SIZE]={0};FILETIME parent={0},e,k,u;
    if(ok)ok=GetProcessTimes(GetCurrentProcess(),&parent,&e,&k,&u) && digest(&j->layout,&plan,ticket+56);
    if(ok){memcpy(ticket,"L4HAND01",8);memcpy(ticket+8,j->header+8,16);l4_store_u64(ticket+24,plan.sequence);l4_store_u32(ticket+32,plan.worker_pid);memcpy(ticket+36,&plan.worker_created,8);
        l4_store_u32(ticket+44,GetCurrentProcessId());memcpy(ticket+48,&parent,8);l4_store_u64(ticket+88,state.generation);l4_store_u64(ticket+96,helper->size);memcpy(ticket+104,helper->sha256,32);l4_store_u32(ticket+136,overhead);}
    l4_recovery_release(g);
    if(ok)ok=audit(context,j,helper,overhead) && l4_worker_job_verify(worker,&plan) && clear(j,l4_store_get64(ticket+88),true,&state) && l4_recovery_relock(g,1000) && waiting(g);
    if(ok)ok=l4_journal_append(j,68,ticket,sizeof(ticket),NULL);
    DWORD error=GetLastError();l4_recovery_close(g);if(!ok)return fail(error?error:ERROR_INVALID_STATE);
    l4_journal_close(j);*pointer=NULL;return true;
}
static bool job_profile(HANDLE job){BYTE* sd=NULL;DWORD size=0;BOOL member=FALSE;JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};
    bool ok=l4_store_security(job,true,&sd,&size) && IsProcessInJob(GetCurrentProcess(),job,&member) && member && QueryInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits),NULL) && limits.BasicLimitInformation.LimitFlags==JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    free(sd);return ok?true:fail(ERROR_ACCESS_DENIED);}
static bool self_job(const L4RecoveryPlan* plan,HANDLE* held){
    *held=NULL;wchar_t uuid[40],name[96];if(StringFromGUID2(&plan->operation,uuid,40)!=39 || swprintf_s(name,96,L"Global\\L4UpdateWorker.%ls",uuid)<0)return fail(ERROR_INVALID_NAME);
    HANDLE job=OpenJobObjectW(JOB_OBJECT_QUERY|READ_CONTROL,FALSE,name);if(!job)return false;
    if(!job_profile(job)){CloseHandle(job);return false;}*held=job;return true;
}
typedef struct {const L4UpdateState* expected;bool receipt,window;L4UpdateState last;} ActiveHistory;
static bool active_visit(DWORD kind,ULONGLONG seq,const void* data,DWORD size,void* context){
    ActiveHistory* h=context;const BYTE* b=data;
    if(!h->receipt){
        switch(kind){case 92:case 93:case 60:case 61:case 62:case 63:case 20:case 10:case 64:case 66:case 67:case 70:case 68:return true;
            case 69:if(size!=32)return fail(ERROR_INVALID_DATA);h->receipt=true;return true;default:return fail(ERROR_INVALID_STATE);}
    }
    if(kind==65){
        if(size!=112 || memcmp(b,"L4UPD01",8) || l4_store_get32(b+52))return fail(ERROR_INVALID_DATA);BYTE hash[32];
        if(!l4_store_hash(b,80,NULL,0,hash) || memcmp(hash,b+80,32))return fail(ERROR_CRC);
        L4UpdateState state={0};memcpy(state.owner,b+8,40);state.window=l4_store_get32(b+48);state.generation=l4_store_get64(b+56);
        state.plan_sequence=l4_store_get64(b+64);state.deadline_utc=l4_store_get64(b+72);
        BYTE canonical[112];if(!l4_update_state_encode(&state,canonical) || memcmp(canonical,b,112) || !state.window ||
            strcmp(state.owner,h->expected->owner) || state.plan_sequence!=h->expected->plan_sequence)return fail(ERROR_INVALID_DATA);
        if(h->window && (h->last.window!=1 || state.window!=2 || h->last.generation==~0ull || state.generation!=h->last.generation+1))return fail(ERROR_INVALID_STATE);
        if(!h->window && state.window!=1)return fail(ERROR_INVALID_STATE);h->last=state;h->window=true;return true;
    }
    if(!h->window)return fail(ERROR_INVALID_STATE);
    if(kind==21 || kind==22)return size==12?true:fail(ERROR_INVALID_DATA);
    if(kind==107){
        DWORD observed=size>=16?l4_store_get32(b+12):0;
        if(size!=112 || memcmp(b,"L4CFG001",8) || l4_store_get32(b+8)!=1 || observed<1 || observed>2 || l4_store_get32(b+16) ||
            l4_store_get32(b+20)!=h->last.window || l4_store_get64(b+24)!=h->last.generation || l4_store_get64(b+32)!=h->last.deadline_utc ||
            l4_store_get64(b+40)!=h->expected->plan_sequence || !l4_store_get64(b+48) || !l4_store_get32(b+56) || !l4_store_get64(b+64) ||
            !l4_store_get64(b+72) || l4_store_get64(b+72)>=seq || l4_store_get32(b+80)>1)return fail(ERROR_INVALID_DATA);
        for(unsigned i=60;i<64;i++)if(b[i])return fail(ERROR_INVALID_DATA);for(unsigned i=84;i<112;i++)if(b[i])return fail(ERROR_INVALID_DATA);
        return true;
    }
    if(kind==100 || kind==101 || kind==104 || kind==105 || kind==106){
        if(size!=112 || memcmp(b,"L4SCA001",8) || l4_store_get32(b+8)!=1 || l4_store_get32(b+16)>=4 ||
            l4_store_get32(b+20)!=h->last.window || l4_store_get64(b+24)!=h->last.generation || l4_store_get64(b+32)!=h->last.deadline_utc ||
            l4_store_get64(b+40)!=h->expected->plan_sequence || !l4_store_get64(b+48) || !l4_store_get32(b+56) || !l4_store_get64(b+64))return fail(ERROR_INVALID_DATA);
        for(unsigned i=60;i<64;i++)if(b[i])return fail(ERROR_INVALID_DATA);
        unsigned reserved=80;if(kind==104 || kind==105 || kind==106){DWORD phase=l4_store_get32(b+80);if(phase<1 || phase>4)return fail(ERROR_INVALID_DATA);reserved=84;}
        for(unsigned i=reserved;i<112;i++)if(b[i])return fail(ERROR_INVALID_DATA);
        return true; /* Action ordering/direction/reference semantics owned by executor. */
    }
    return fail(ERROR_INVALID_STATE); /* Includes all terminal/unknown records. */
}
static bool active_state(L4Journal* j,const L4UpdateState* expected,ULONGLONG generation,const L4RecoveryPlan* plan,L4UpdateState* out){
    BYTE canonical[112];if(!expected || !l4_update_state_encode(expected,canonical) || expected->window<1 || expected->window>2 ||
        generation>~0ull-expected->window || expected->generation!=generation+expected->window || expected->plan_sequence!=plan->sequence ||
        expected->deadline_utc>=plan->deadline_utc || utc()>=expected->deadline_utc)return fail(ERROR_INVALID_STATE);
    wchar_t uuid[40];char owner[40];if(StringFromGUID2(&plan->operation,uuid,40)!=39)return fail(ERROR_INVALID_DATA);
    for(unsigned i=0;i<36;i++){wchar_t c=uuid[i+1];owner[i]=(char)((c>=L'A'&&c<=L'F')?c+32:c);}owner[36]=0;
    if(strcmp(owner,expected->owner) || !l4_update_state_read(&j->layout,out) || memcmp(out->owner,expected->owner,40) ||
        out->window!=expected->window || out->generation!=expected->generation || out->plan_sequence!=expected->plan_sequence || out->deadline_utc!=expected->deadline_utc)return fail(ERROR_REVISION_MISMATCH);
    return true;
}
static bool admit(L4Journal* j,L4HandoffAudit audit,void* context,L4WorkerAdmission* admission,const L4UpdateState* active){
    if(!history(j,admission?2u:1u))return false;ULONGLONG ticket_sequence=j->sequence;
    BYTE* receipt=NULL;DWORD receipt_size=0;if(admission){History h={0};if(!l4_journal_replay(j,count,&h) || !l4_store_find_record(j,69,active?h.receipt_sequence:j->sequence,&receipt,&receipt_size))return false;ticket_sequence=h.ticket_sequence;}
    if(active){ActiveHistory h={0};h.expected=active;
        if(!admission || !l4_journal_replay(j,active_visit,&h) || !h.receipt || !h.window || memcmp(h.last.owner,active->owner,40) ||
            h.last.window!=active->window || h.last.generation!=active->generation || h.last.plan_sequence!=active->plan_sequence || h.last.deadline_utc!=active->deadline_utc){free(receipt);return fail(ERROR_INVALID_STATE);}}
    BYTE* ticket=NULL;DWORD size=0;if(!l4_store_find_record(j,68,ticket_sequence,&ticket,&size)){free(receipt);return false;}
    bool ok=size==TICKET_SIZE && !memcmp(ticket,"L4HAND01",8) && !memcmp(ticket+8,j->header+8,16) && !l4_store_get32(ticket+140);
    L4RecoveryGuard* g=NULL;HANDLE parent=NULL,job=NULL;L4RecoveryHelper helper={0};L4UpdateState state={0};BYTE hash[32];FILETIME worker_epoch={0},parent_epoch={0};DWORD overhead=0;
    if(ok && admission)ok=receipt_size==32 && l4_store_hash(ticket,size,NULL,0,hash) && !memcmp(hash,receipt,32);free(receipt);
    if(ok)ok=guard(j,&g);const L4RecoveryPlan* plan=g?l4_recovery_plan(g):NULL;
    if(ok){memcpy(&worker_epoch,ticket+36,8);memcpy(&parent_epoch,ticket+48,8);helper.size=l4_store_get64(ticket+96);memcpy(helper.sha256,ticket+104,32);overhead=l4_store_get32(ticket+136);
        parent=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,l4_store_get32(ticket+44));
        ok=plan->worker_pid==l4_store_get32(ticket+32) && !CompareFileTime(&plan->worker_created,&worker_epoch) && plan->sequence==l4_store_get64(ticket+24) &&
            live(GetCurrentProcess(),plan->worker_pid,&worker_epoch) && plan->worker_pid!=l4_store_get32(ticket+44) && live(parent,l4_store_get32(ticket+44),&parent_epoch) &&
            digest(&j->layout,plan,hash) && !memcmp(hash,ticket+56,32) && waiting(g) && self_job(plan,&job) &&
            (active?active_state(j,active,l4_store_get64(ticket+88),plan,&state):clear(j,l4_store_get64(ticket+88),true,&state));}
    if(g)l4_recovery_release(g);
    L4RecoveryTask spec;if(ok)ok=l4_recovery_task_spec(&j->layout,plan,&helper,overhead,&spec);
    if(ok)ok=audit(context,j,&helper,overhead) && live(parent,l4_store_get32(ticket+44),&parent_epoch) && job_profile(job) &&
        (active?active_state(j,active,l4_store_get64(ticket+88),plan,&state):clear(j,l4_store_get64(ticket+88),true,&state)) && l4_recovery_relock(g,1000) && waiting(g);
    if(ok && !admission)ok=l4_store_hash(ticket,size,NULL,0,hash) && l4_journal_append(j,69,hash,32,NULL);
    if(ok && admission){admission->sequence=plan->sequence;admission->generation=state.generation;admission->deadline_utc=plan->deadline_utc;admission->old_size=plan->old_size;admission->new_size=plan->new_size;
        admission->parent_pid=l4_store_get32(ticket+44);admission->parent_created=parent_epoch;
        admission->helper=helper;
        admission->supervisor_pid=plan->supervisor_pid;admission->supervisor_created=plan->supervisor_created;admission->start_type=plan->start_type;
        wcscpy_s(admission->before,2048,plan->before);wcscpy_s(admission->after,2048,plan->after);memcpy(admission->old_sha256,plan->old_sha256,32);memcpy(admission->new_sha256,plan->new_sha256,32);}
    DWORD error=GetLastError();free(ticket);if(job)CloseHandle(job);if(parent)CloseHandle(parent);l4_recovery_close(g);return ok?true:fail(error?error:ERROR_INVALID_DATA);
}
bool l4_worker_accept_checked(const L4Layout* roots,const wchar_t* operation,DWORD timeout,L4Journal** out,L4HandoffAudit audit,void* context){
    if(!out)return fail(ERROR_INVALID_PARAMETER);*out=NULL;if(!roots || !operation || !audit || timeout<1 || timeout>300000)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG deadline=GetTickCount64()+timeout;L4Journal* j=NULL;
    while(!l4_journal_open(roots,operation,false,&j)){DWORD error=GetLastError();if(error!=ERROR_SHARING_VIOLATION && error!=ERROR_LOCK_VIOLATION)return false;ULONGLONG now=GetTickCount64();if(now>=deadline)return fail(ERROR_TIMEOUT);Sleep(deadline-now<10?(DWORD)(deadline-now):10);}
    bool ok=GetTickCount64()<deadline && admit(j,audit,context,NULL,NULL) && GetTickCount64()<deadline;DWORD error=GetLastError();
    if(!ok){l4_journal_close(j);return fail(GetTickCount64()>=deadline?ERROR_TIMEOUT:error?error:ERROR_INVALID_STATE);}*out=j;return true;
}
static bool audit(void* context,L4Journal* j,const L4RecoveryHelper* helper,DWORD overhead){(void)context;return l4_recovery_task_audit(j,helper,overhead);}
bool l4_worker_transfer(L4Journal** j,L4WorkerJob* worker,const L4RecoveryHelper* helper,DWORD overhead){if(!system_user())return false;return l4_worker_transfer_checked(j,worker,helper,overhead,audit,NULL);}
bool l4_worker_accept(const wchar_t* source_version,const wchar_t* operation,DWORD timeout,L4Journal** out){if(out)*out=NULL;if(!system_user())return false;L4Layout roots;if(!l4_layout_resolve(&roots,source_version))return false;return l4_worker_accept_checked(&roots,operation,timeout,out,audit,NULL);}
bool l4_worker_recheck_checked(L4Journal* j,L4WorkerAdmission* out,L4HandoffAudit check,void* context){if(!out)return fail(ERROR_INVALID_PARAMETER);memset(out,0,sizeof(*out));if(!j || !check)return fail(ERROR_INVALID_PARAMETER);bool ok=admit(j,check,context,out,NULL);if(!ok)memset(out,0,sizeof(*out));return ok;}
bool l4_worker_recheck(L4Journal* j,L4WorkerAdmission* out){if(out)memset(out,0,sizeof(*out));if(!system_user())return false;return l4_worker_recheck_checked(j,out,audit,NULL);}
bool l4_worker_recheck_active_checked(L4Journal* j,const L4UpdateState* state,L4WorkerAdmission* out,L4HandoffAudit check,void* context){
    if(!out)return fail(ERROR_INVALID_PARAMETER);memset(out,0,sizeof(*out));if(!j || !state || !check)return fail(ERROR_INVALID_PARAMETER);
    bool ok=admit(j,check,context,out,state);if(!ok)memset(out,0,sizeof(*out));return ok;
}
bool l4_worker_recheck_active(L4Journal* j,const L4UpdateState* state,L4WorkerAdmission* out){
    if(out)memset(out,0,sizeof(*out));HANDLE thread=NULL;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread)){CloseHandle(thread);return fail(ERROR_ACCESS_DENIED);}if(GetLastError()!=ERROR_NO_TOKEN)return false;
    if(!system_user())return false;HANDLE token=NULL;DWORD bytes=0,session=~0u;TOKEN_TYPE type=TokenImpersonation;
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenType,&type,sizeof(type),&bytes) && type==TokenPrimary &&
        GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&bytes) && session==0;DWORD error=GetLastError();if(token)CloseHandle(token);
    if(!ok)return fail(error?error:ERROR_ACCESS_DENIED);return l4_worker_recheck_active_checked(j,state,out,audit,NULL);
}
