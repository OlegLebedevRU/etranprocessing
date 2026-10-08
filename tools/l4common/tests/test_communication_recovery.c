#include "../communication_recovery.h"
#include <stdio.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL %u: %s\n",__LINE__,#x);}}while(0)
typedef struct {
    ULONGLONG clock,utc;unsigned calls,unlocks;int fault,late;
    unsigned log[16];DWORD budgets[16];bool locked,worker_gone;
    L4UpdateState expected;
} Fixture;
static ULONGLONG monotonic(void* p){return ((Fixture*)p)->clock;}
static ULONGLONG utc(void* p){return ((Fixture*)p)->utc;}
static bool action(Fixture* f,unsigned code,DWORD ms){
    unsigned i=f->calls++;f->log[i]=code;f->budgets[i]=ms;CHECK(ms>0 && ms<=300000);
    f->clock+=(int)i==f->late?ms:1;
    if((int)i==f->fault){SetLastError(ERROR_ACCESS_DENIED);return false;}return true;
}
static bool verify(const L4UpdateState* state,DWORD ms,void* p){
    Fixture* f=p;CHECK(!memcmp(state,&f->expected,sizeof(*state)));
    CHECK(f->calls?f->locked:!f->locked);return action(f,f->calls?3:0,ms);
}
static bool worker(DWORD ms,void* p){Fixture* f=p;CHECK(!f->locked);bool ok=action(f,1,ms);if(ok)f->worker_gone=true;return ok;}
static bool lock(DWORD ms,void* p){Fixture* f=p;CHECK(f->worker_gone && !f->locked);bool ok=action(f,2,ms);if(ok)f->locked=true;return ok;}
static void unlock(void* p){Fixture* f=p;CHECK(f->locked);f->locked=false;f->unlocks++;}
static bool step(L4CommunicationStep op,DWORD ms,void* p){Fixture* f=p;CHECK(f->locked && f->worker_gone);return action(f,4+(unsigned)op,ms);}
static void reset(Fixture* f){memset(f,0,sizeof(*f));f->fault=f->late=-1;f->clock=100;f->utc=1000;
    strcpy_s(f->expected.owner,40,"17730000-0000-4000-8000-000000000006");
    f->expected.window=1;f->expected.generation=7;f->expected.plan_sequence=64;f->expected.deadline_utc=1000;
}
typedef struct {
    HANDLE go;L4CommunicationAttempt* attempt;Fixture* fixture;
    const L4CommunicationBudget* budget;const L4CommunicationRecoveryOps* ops;
    L4CommunicationResult result;bool ok;
} Race;
static DWORD WINAPI recover_thread(void* context){
    Race* race=context;WaitForSingleObject(race->go,5000);
    race->ok=l4_communication_recover(race->attempt,&race->fixture->expected,race->budget,race->ops,false,&race->result);
    return 0;
}
int main(void){
    Fixture f;reset(&f);L4CommunicationBudget budget={100,100,100,100,100,300000,100,300800};
    L4CommunicationRecoveryOps ops={monotonic,utc,verify,worker,lock,unlock,step,&f};
    L4CommunicationAttempt attempt={0};L4CommunicationResult result;
    CHECK(l4_communication_budget_valid(&budget));
    CHECK(!l4_communication_native_budget_valid(&budget));
    L4CommunicationBudget native=budget;native.total_ms+=2*native.verify_ms;CHECK(l4_communication_native_budget_valid(&native));
    native.total_ms--;CHECK(!l4_communication_native_budget_valid(&native));
    for(unsigned i=0;i<8;i++){L4CommunicationBudget bad=budget;
        if(i==0)bad.verify_ms=0;else if(i==1)bad.worker_ms=99;else if(i==2)bad.lock_ms=300001;
        else if(i==3)bad.proxy_ms=0;else if(i==4)bad.broker_prepare_ms=0;else if(i==5)bad.mosquitto_ms=299999;else if(i==6)bad.channel_ms=0;else bad.total_ms--;
        CHECK(!l4_communication_budget_valid(&bad));}
    f.utc=999;CHECK(!l4_communication_recover(&attempt,&f.expected,&budget,&ops,false,&result));CHECK(!attempt.spent && !f.calls);
    f.utc=1000;CHECK(l4_communication_recover(&attempt,&f.expected,&budget,&ops,false,&result));
    CHECK(result.outcome==L4_COMM_VERIFIED && result.completed==10 && !result.error && f.calls==14 && f.unlocks==1);
    for(unsigned i=0;i<14;i++)CHECK(f.log[i]==i);
    CHECK(f.budgets[4]==100 && f.budgets[7]==97 && f.budgets[8]==100 && f.budgets[9]==100 && f.budgets[10]==99 && f.budgets[11]==300000 && f.budgets[12]==299999 && f.budgets[13]==100);
    CHECK(!l4_communication_recover(&attempt,&f.expected,&budget,&ops,false,&result));CHECK(result.error==ERROR_ALREADY_EXISTS && f.calls==14);
    for(int fault=0;fault<14;fault++){
        reset(&f);attempt.spent=0;f.fault=fault;
        CHECK(!l4_communication_recover(&attempt,&f.expected,&budget,&ops,false,&result));
        CHECK(result.error==ERROR_ACCESS_DENIED && f.calls==(unsigned)fault+1);
        CHECK(f.unlocks==(fault>=3?1u:0u) && !f.locked);
        CHECK(result.outcome==(fault==13?L4_COMM_CONNECTIVITY_UNCONFIRMED:L4_COMM_FAILED));
        CHECK(!l4_communication_recover(&attempt,&f.expected,&budget,&ops,true,&result));CHECK(f.calls==(unsigned)fault+1);
    }
    for(int late=0;late<14;late++){
        reset(&f);attempt.spent=0;f.late=late;
        CHECK(!l4_communication_recover(&attempt,&f.expected,&budget,&ops,false,&result));
        CHECK(result.error==ERROR_TIMEOUT && f.calls==(unsigned)late+1);
        CHECK(f.unlocks==(late>=2?1u:0u) && !f.locked);
        CHECK(result.outcome==(late==13?L4_COMM_CONNECTIVITY_UNCONFIRMED:L4_COMM_FAILED));
    }
    reset(&f);attempt.spent=0;f.utc=1;
    CHECK(l4_communication_recover(&attempt,&f.expected,&budget,&ops,true,&result));CHECK(result.outcome==L4_COMM_VERIFIED);
    reset(&f);attempt.spent=0;f.clock=~0ull-10;
    CHECK(!l4_communication_recover(&attempt,&f.expected,&budget,&ops,false,&result));CHECK(result.error==ERROR_ARITHMETIC_OVERFLOW && !f.calls);
    reset(&f);attempt.spent=0;f.expected.window=2;
    CHECK(!l4_communication_recover(&attempt,&f.expected,&budget,&ops,true,&result));CHECK(!attempt.spent && !f.calls);
    f.expected.window=1;f.expected.owner[36]='x';CHECK(!l4_communication_recover(&attempt,&f.expected,&budget,&ops,true,&result));CHECK(!attempt.spent);
    reset(&f);attempt.spent=0;HANDLE go=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(go);
    Race races[2]={{go,&attempt,&f,&budget,&ops,{0},false},{go,&attempt,&f,&budget,&ops,{0},false}};
    HANDLE threads[2]={CreateThread(NULL,0,recover_thread,&races[0],0,NULL),CreateThread(NULL,0,recover_thread,&races[1],0,NULL)};
    CHECK(threads[0] && threads[1]);SetEvent(go);CHECK(WaitForMultipleObjects(2,threads,TRUE,5000)==WAIT_OBJECT_0);
    CHECK(races[0].ok!=races[1].ok && f.calls==14 && f.unlocks==1);
    CHECK((races[0].ok?races[1].result.error:races[0].result.error)==ERROR_ALREADY_EXISTS);
    CloseHandle(threads[0]);CloseHandle(threads[1]);CloseHandle(go);
    printf("Communication recovery policy: %u checks, %u failures; modeled SCM/Job/barriers, no services\n",checks,failures);
    return failures?1:0;
}
