/* Real read-only reporting thread/queue/cursor; identity/pin/snapshot/publisher
 * modeled. No CONNECT, production SCM, installed files or actual MQTT broker. */
#include "../src/update_reporting.h"
#include "../../l4common/journal_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static ULONGLONG tick=10000;static volatile LONG enabled,observations,replies,publishes;static bool accept_event=true;static L4RemoteStatus snapshot;
static char published[2048],replied[2048],last_nonce[37];static unsigned last_id;
static bool ready(void* context){(void)context;return InterlockedCompareExchange(&enabled,0,0)!=0;}
static void reply(void* context,const char* task,int code,const char* json){(void)context;assert(code==200 && strstr(json,task));strcpy_s(replied,sizeof(replied),json);InterlockedIncrement(&replies);}
static bool event(void* context,const char* json,unsigned id,const char* nonce){(void)context;assert(id && strcmp(nonce,snapshot.operation_id));
    strcpy_s(published,sizeof(published),json);strcpy_s(last_nonce,sizeof(last_nonce),nonce);last_id=id;InterlockedIncrement(&publishes);return accept_event;}
bool l4_remote_status_observe(const L4Layout* roots,const wchar_t* operation,L4RemoteStatus* out){(void)roots;(void)operation;*out=snapshot;InterlockedIncrement(&observations);return true;}
static bool pin(const wchar_t* path,const wchar_t* root,bool control,L4FileFence* fence){(void)path;(void)root;(void)control;memset(fence,0,sizeof(*fence));return true;}
static void unpin(L4FileFence* fence){(void)fence;}
static ULONGLONG clock_tick(void){return tick;}
#define l4_store_pin pin
#define l4_store_unpin unpin
#define GetTickCount64 clock_tick
#include "../src/update_reporting.c"
#undef GetTickCount64
static bool until(volatile LONG* value,LONG wanted){ULONGLONG end=GetTickCount64()+5000;while(GetTickCount64()<end){if(InterlockedCompareExchange(value,0,0)>=wanted)return true;Sleep(10);}return false;}
int main(void){
    const char* operation="135a4120-9ba6-4f6c-8cac-4baf5df8f1df",*task="22222222-2222-4222-8222-222222222222";
    FILETIME now;GetSystemTimeAsFileTime(&now);ULONGLONG stamp=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;
    strcpy_s(snapshot.operation_id,37,operation);snapshot.request.target=L4_REMOTE_SUITE;strcpy_s(snapshot.request.version,32,"latest");snapshot.recorded_phase=L4_REMOTE_RECORDED_FINISHED;
    snapshot.has_result=true;strcpy_s(snapshot.result.operation_id,37,operation);strcpy_s(snapshot.result.requested_version,32,"latest");strcpy_s(snapshot.result.previous_version,32,"1.13.6");
    snapshot.result.target=1;snapshot.result.result=L4_REMOTE_RESULT_FAILED;snapshot.result.error=ERROR_TIMEOUT;snapshot.result.started_at=stamp;snapshot.result.finished_at=stamp;
    /* Bounded queue refuses overflow; no RPC execution, job or state mutation. */
    L4UpdateReporting local={0};InitializeCriticalSection(&local.lock);local.stop=CreateEventW(NULL,TRUE,FALSE,NULL);assert(local.stop);
    for(unsigned i=0;i<8;i++)assert(update_reporting_status(&local,task,operation));assert(!update_reporting_status(&local,task,operation));
    assert(!update_reporting_status(&local,task,"00000000-0000-0000-0000-000000000000"));Request request;
    for(unsigned i=0;i<8;i++)assert(take(&local,&request));assert(!take(&local,&request));SetEvent(local.stop);assert(!update_reporting_status(&local,task,operation));CloseHandle(local.stop);DeleteCriticalSection(&local.lock);
    /* Retry same delivery identity once, then stop even after later sweeps. */
    memset(&local,0,sizeof(local));local.ready=ready;local.event=event;InterlockedExchange(&enabled,1);accept_event=false;
    send_result(&local,L"135a4120-9ba6-4f6c-8cac-4baf5df8f1df",&snapshot);assert(publishes==1);char nonce[37];strcpy_s(nonce,37,last_nonce);unsigned id=last_id;
    send_result(&local,L"135a4120-9ba6-4f6c-8cac-4baf5df8f1df",&snapshot);assert(publishes==1);tick+=5000;
    send_result(&local,L"135a4120-9ba6-4f6c-8cac-4baf5df8f1df",&snapshot);assert(publishes==2 && id==last_id && !strcmp(nonce,last_nonce));tick+=5000;
    send_result(&local,L"135a4120-9ba6-4f6c-8cac-4baf5df8f1df",&snapshot);assert(publishes==2);
    /* Durable success is not publishable until actual marker clear, even when
     *65 clear intent is recorded. Pending observation must consume no slot. */
    L4RemoteStatus saved_snapshot=snapshot;memset(&local,0,sizeof(local));local.ready=ready;local.event=event;
    snapshot.plan_sequence=11;snapshot.has_result=false;snapshot.has_outcome=true;snapshot.outcome.result=saved_snapshot.result;
    strcpy_s(snapshot.outcome.result.resolved_version,32,"1.13.7");snapshot.outcome.result.result=L4_REMOTE_OUTCOME_SUCCESS;snapshot.outcome.result.error=0;
    snapshot.outcome.plan_sequence=11;snapshot.outcome.plan_sha256[0]=1;snapshot.outcome.proof_sequence=20;snapshot.outcome.proof_sha256[0]=2;
    snapshot.outcome_clear_recorded=true;accept_event=true;LONG before=publishes;
    send_result(&local,L"135a4120-9ba6-4f6c-8cac-4baf5df8f1df",&snapshot);assert(publishes==before && !local.used);
    snapshot.outcome_cleared=true;send_result(&local,L"135a4120-9ba6-4f6c-8cac-4baf5df8f1df",&snapshot);assert(publishes==before+1 && local.used==1 && strstr(published,"\"succeeded\""));
    send_result(&local,L"135a4120-9ba6-4f6c-8cac-4baf5df8f1df",&snapshot);assert(publishes==before+1);
    memset(&local,0,sizeof(local));local.ready=ready;local.event=event;snapshot.outcome_cleared=false;
    snapshot.outcome.result.result=L4_REMOTE_OUTCOME_RESTORED;snapshot.outcome.result.error=ERROR_TIMEOUT;
    send_result(&local,L"135a4120-9ba6-4f6c-8cac-4baf5df8f1df",&snapshot);assert(publishes==before+1 && !local.used);
    snapshot.outcome_cleared=true;send_result(&local,L"135a4120-9ba6-4f6c-8cac-4baf5df8f1df",&snapshot);assert(publishes==before+2 && strstr(published,"\"restored\"") && strstr(published,"\"installed_version\":\"1.13.6\""));
    memset(&local,0,sizeof(local));local.ready=ready;local.event=event;snapshot.outcome_cleared=false;snapshot.outcome_clear_recorded=false;
    snapshot.outcome.result.result=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED;snapshot.outcome.result.error=ERROR_TIMEOUT;snapshot.outcome.proof_sequence=0;memset(snapshot.outcome.proof_sha256,0,32);
    send_result(&local,L"135a4120-9ba6-4f6c-8cac-4baf5df8f1df",&snapshot);assert(publishes==before+3 && strstr(published,"\"recovery_required\""));
    snapshot=saved_snapshot;
    /* New results retain room after64; evicted old history never restarts delivery. */
    memset(&local,0,sizeof(local));wchar_t history_name[37];
    for(unsigned i=1;i<=64;i++){swprintf_s(history_name,37,L"%08x-2222-4222-8222-222222222222",i);assert(delivery(&local,history_name,stamp+i));}
    assert(local.used==64);assert(delivery(&local,L"00000065-2222-4222-8222-222222222222",stamp+65));
    assert(!delivery(&local,L"00000001-2222-4222-8222-222222222222",stamp+1));assert(local.used==64);
    InterlockedExchange(&enabled,0);accept_event=true;InterlockedExchange(&publishes,0);
    wchar_t temp[MAX_PATH],path[MAX_PATH],child[MAX_PATH];assert(GetTempPathW(MAX_PATH,temp));
    swprintf_s(path,MAX_PATH,L"%lsL4Reporting-%lu",temp,GetCurrentProcessId());assert(CreateDirectoryW(path,NULL));swprintf_s(child,MAX_PATH,L"%ls\\%hs",path,operation);assert(CreateDirectoryW(child,NULL));
    L4Layout roots={0};wcscpy_s(roots.operations,MAX_PATH,path);wcscpy_s(roots.data,MAX_PATH,path);HANDLE stop=CreateEventW(NULL,TRUE,FALSE,NULL);assert(stop);L4UpdateReporting* reporting=NULL;
    assert(update_reporting_start(&roots,stop,ready,reply,event,NULL,&reporting));assert(update_reporting_status(reporting,task,operation));Sleep(50);assert(!replies && !publishes && !observations);
    InterlockedExchange(&enabled,1);assert(until(&replies,1) && strstr(replied,"recorded_history"));assert(until(&publishes,1));
    Sleep(1100);assert(publishes==1);SetEvent(stop);assert(update_reporting_close(&reporting,5000) && !reporting);CloseHandle(stop);
    /* Proactive scan does not depend on an operator sending7032. */
    InterlockedExchange(&publishes,0);stop=CreateEventW(NULL,TRUE,FALSE,NULL);assert(stop);
    assert(update_reporting_start(&roots,stop,ready,reply,event,NULL,&reporting));assert(until(&publishes,1));
    assert(strstr(published,"\"200\":76") && strstr(published,"\"449\""));SetEvent(stop);assert(update_reporting_close(&reporting,5000));CloseHandle(stop);
    assert(RemoveDirectoryW(child) && RemoveDirectoryW(path));
    puts("Update reporting: async7032, scan-only76 without7032, bounded queue/retry, distinct stable delivery, probe priority and clean thread teardown PASS");return 0;
}
