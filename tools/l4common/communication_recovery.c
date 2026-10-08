#include "communication_recovery.h"
#include <string.h>
static bool fail(DWORD error){SetLastError(error);return false;}
static bool small(DWORD value){return value>=100 && value<=300000;}
bool l4_communication_budget_valid(const L4CommunicationBudget* b){
    if(!b || !small(b->verify_ms) || !small(b->worker_ms) || !small(b->lock_ms) ||
       !small(b->proxy_ms) || !small(b->broker_prepare_ms) || b->mosquitto_ms!=300000 || !small(b->channel_ms))return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG sum=2ull*b->verify_ms+b->worker_ms+b->lock_ms+b->proxy_ms+b->broker_prepare_ms+b->mosquitto_ms+2ull*b->channel_ms;
    return b->total_ms>=sum && b->total_ms<=3600000?true:fail(ERROR_INVALID_PARAMETER);
}
bool l4_communication_native_budget_valid(const L4CommunicationBudget* b){
    if(!l4_communication_budget_valid(b))return false;
    ULONGLONG sum=4ull*b->verify_ms+b->worker_ms+b->lock_ms+b->proxy_ms+b->broker_prepare_ms+b->mosquitto_ms+2ull*b->channel_ms;
    return b->total_ms>=sum?true:fail(ERROR_INVALID_PARAMETER);
}
static bool valid(const L4UpdateState* s){
    if(!s || s->window!=L4_UPDATE_COMMUNICATION || !s->generation || !s->plan_sequence || !s->deadline_utc ||
       s->owner[36] || s->owner[37] || s->owner[38] || s->owner[39])return false;
    bool nonzero=false;for(unsigned i=0;i<36;i++){char c=s->owner[i];
        if(i==8 || i==13 || i==18 || i==23){if(c!='-')return false;}
        else if((c>='0' && c<='9') || (c>='a' && c<='f')){if(c!='0')nonzero=true;}else return false;
    }return nonzero;
}
static DWORD left(const L4CommunicationRecoveryOps* ops,ULONGLONG deadline){
    ULONGLONG now=ops->monotonic(ops->context);return now<deadline?(DWORD)(deadline-now):0;
}
static ULONGLONG phase(const L4CommunicationRecoveryOps* ops,ULONGLONG total,DWORD ms){
    ULONGLONG now=ops->monotonic(ops->context),end=now+ms;return end<now || end>total?total:end;
}
static bool checked(bool ok,const L4CommunicationRecoveryOps* ops,ULONGLONG deadline,DWORD* error){
    DWORD code=GetLastError();if(!left(ops,deadline)){*error=ERROR_TIMEOUT;return false;}
    if(!ok){*error=code?code:ERROR_NOT_READY;return false;}return true;
}
bool l4_communication_recover(L4CommunicationAttempt* attempt,const L4UpdateState* input,
    const L4CommunicationBudget* input_budget,const L4CommunicationRecoveryOps* ops,
    bool boot_reconciled,L4CommunicationResult* result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);memset(result,0,sizeof(*result));result->error=ERROR_INVALID_PARAMETER;
    if(!attempt || !valid(input) || !l4_communication_budget_valid(input_budget) || !ops || !ops->monotonic || !ops->utc ||
       !ops->verify || !ops->worker_exit || !ops->lock || !ops->unlock || !ops->step)return fail(ERROR_INVALID_PARAMETER);
    /* Copy immutable input before callbacks. Callback context belongs to adapter. */
    L4UpdateState expected=*input;L4CommunicationBudget budget=*input_budget;
    if(!boot_reconciled && ops->utc(ops->context)<expected.deadline_utc){result->error=ERROR_NOT_READY;return fail(result->error);}
    if(InterlockedCompareExchange(&attempt->spent,1,0)){result->error=ERROR_ALREADY_EXISTS;return fail(result->error);}
    ULONGLONG now=ops->monotonic(ops->context),total=now+budget.total_ms,end;
    DWORD remaining=0,error=ERROR_SUCCESS;bool ok=total>=now,locked=false;if(!ok)error=ERROR_ARITHMETIC_OVERFLOW;
    end=phase(ops,total,budget.verify_ms);SetLastError(ERROR_SUCCESS);
    if(ok){remaining=left(ops,end);ok=checked(remaining && ops->verify(&expected,remaining,ops->context),ops,end,&error);}
    end=phase(ops,total,budget.worker_ms);SetLastError(ERROR_SUCCESS);
    if(ok){remaining=left(ops,end);ok=checked(remaining && ops->worker_exit(remaining,ops->context),ops,end,&error);}
    end=phase(ops,total,budget.lock_ms);SetLastError(ERROR_SUCCESS);
    if(ok){remaining=left(ops,end);locked=remaining && ops->lock(remaining,ops->context);ok=checked(locked,ops,end,&error);}
    end=phase(ops,total,budget.verify_ms);SetLastError(ERROR_SUCCESS);
    if(ok){remaining=left(ops,end);ok=checked(remaining && ops->verify(&expected,remaining,ops->context),ops,end,&error);}
    end=phase(ops,total,budget.proxy_ms);
    for(unsigned i=0;ok && i<=L4_COMM_FINAL_BARRIER;i++){
        if(i==L4_COMM_PROXY_CHANNELS || i==L4_COMM_FINAL_BARRIER)end=phase(ops,total,budget.channel_ms);
        if(i==L4_COMM_STOP_MOSQUITTO)end=phase(ops,total,budget.broker_prepare_ms);
        if(i==L4_COMM_START_MOSQUITTO)end=phase(ops,total,budget.mosquitto_ms);
        remaining=left(ops,end);if(!remaining){ok=false;error=ERROR_TIMEOUT;
            if(i==L4_COMM_FINAL_BARRIER)result->outcome=L4_COMM_CONNECTIVITY_UNCONFIRMED;break;}
        SetLastError(ERROR_SUCCESS);ok=checked(ops->step((L4CommunicationStep)i,remaining,ops->context),ops,end,&error);
        if(ok)result->completed++;
        else if(i==L4_COMM_FINAL_BARRIER)result->outcome=L4_COMM_CONNECTIVITY_UNCONFIRMED;
    }
    if(locked)ops->unlock(ops->context);
    if(ok)result->outcome=L4_COMM_VERIFIED;result->error=ok?ERROR_SUCCESS:error;
    return ok?true:fail(error);
}
