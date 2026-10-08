/* Scripted UTC/Tick grids; modeled state/guard/SYSTEM/SCM, no live services. */
#include "../communication_monitor.h"
#include <stdio.h>
#include <string.h>
static unsigned checks,failures,executions,waits;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL line%u: %s\n",__LINE__,#x);}}while(0)
typedef struct {ULONGLONG first,refresh,tick;DWORD maximum_wait;} Sample;
static Sample samples[8];static unsigned sample_count,sample_index,utc_calls;
static L4CommunicationPlan plan;static L4UpdateState marker;
struct L4CommunicationPin {unsigned unused;};static L4CommunicationPin fixture_pin;
struct L4CommunicationDecision {unsigned unused;};static L4CommunicationDecision fixture_decision;
static void WINAPI scripted_utc(LPFILETIME out){
    CHECK(sample_index<sample_count);ULONGLONG n=utc_calls++?samples[sample_index].refresh:samples[sample_index].first;
    out->dwLowDateTime=(DWORD)n;out->dwHighDateTime=(DWORD)(n>>32);
}
static ULONGLONG WINAPI scripted_tick(void){CHECK(sample_index<sample_count);return samples[sample_index].tick;}
static DWORD WINAPI scripted_wait(HANDLE handle,DWORD timeout){(void)handle;
    if(!timeout)return WAIT_TIMEOUT;
    waits++;CHECK(timeout>0 && timeout<=samples[sample_index].maximum_wait);
    sample_index++;utc_calls=0;CHECK(sample_index<sample_count);return WAIT_TIMEOUT;
}
static BOOL WINAPI modeled_system_sid(PSID sid,WELL_KNOWN_SID_TYPE type){(void)sid;(void)type;return TRUE;}
#define GetSystemTimeAsFileTime scripted_utc
#define GetTickCount64 scripted_tick
#define WaitForSingleObject scripted_wait
#define IsWellKnownSid modeled_system_sid
#include "../communication_monitor.c"
#undef IsWellKnownSid
#undef WaitForSingleObject
#undef GetTickCount64
#undef GetSystemTimeAsFileTime
bool l4_update_state_read(const L4Layout* roots,L4UpdateState* out){(void)roots;*out=marker;return true;}
bool l4_communication_plan_open(const L4Layout* roots,const wchar_t* operation,L4CommunicationPin** out){(void)roots;(void)operation;*out=&fixture_pin;return true;}
const L4CommunicationPlan* l4_communication_pinned_plan(const L4CommunicationPin* value){return value==&fixture_pin?&plan:NULL;}
bool l4_communication_plan_matches(const L4CommunicationPin* value,const L4UpdateState* state){return value==&fixture_pin && state->generation==plan.generation && state->window==1;}
void l4_communication_plan_close(L4CommunicationPin* value){(void)value;}
bool l4_communication_native_budget_valid(const L4CommunicationBudget* budget){(void)budget;return true;}
bool l4_communication_decision_open(const L4Layout* roots,const wchar_t* operation,DWORD timeout,L4CommunicationDecision** out){(void)roots;(void)operation;(void)timeout;*out=&fixture_decision;return true;}
void l4_communication_decision_close(L4CommunicationDecision* value){(void)value;}
bool l4_communication_decision_read(L4CommunicationDecision* value,L4CommunicationPhase* phase,DWORD* error){(void)value;*phase=L4_COMM_DEC_WAIT;*error=0;return true;}
bool l4_communication_worker_live(const L4CommunicationPlan* value){(void)value;return true;}
bool l4_communication_execute(const L4Layout* roots,const L4UpdateState* expected,const L4CommunicationSignals* signals,L4CommunicationResult* result){
    (void)roots;(void)expected;(void)signals;CHECK(samples[sample_index].refresh>=plan.deadline_utc);executions++;
    result->outcome=L4_COMM_VERIFIED;result->error=0;result->completed=10;return true;
}
static bool service(unsigned i,DWORD pid,DWORD ms,void* context){(void)i;(void)pid;(void)ms;(void)context;return true;}
static bool channels(DWORD pid,DWORD ms,void* context){(void)pid;(void)ms;(void)context;return true;}
static bool barrier(DWORD ms,void* context){(void)ms;(void)context;return true;}
static void reset(void){memset(&plan,0,sizeof(plan));plan.armed_utc=1000000000;plan.deadline_utc=plan.armed_utc+800000;plan.generation=1;
    memset(&marker,0,sizeof(marker));marker.window=1;marker.generation=1;executions=waits=sample_index=utc_calls=0;sample_count=0;}
static void observe(DWORD expected,unsigned count){
    L4CommunicationMonitor monitor={0};monitor.pin=&fixture_pin;monitor.monotonic_end=1080;monitor.monotonic_grace_end=1112;
    monitor.stop=(HANDLE)(ULONG_PTR)1;monitor.ready=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(monitor.ready!=NULL);InitializeSRWLock(&monitor.result_lock);
    CHECK(run(&monitor)==0);CHECK(monitor.finished);CHECK(monitor.result.error==expected);CHECK(executions==count);CloseHandle(monitor.ready);
}
int main(void){
    reset();samples[0]=(Sample){plan.deadline_utc-1,plan.deadline_utc,1080,32};sample_count=1;
    observe(0,1);CHECK(!waits); /* Sequential read crosses deadline. */
    reset();samples[0]=(Sample){plan.deadline_utc-160000,plan.deadline_utc-160000,1080,16};
    samples[1]=(Sample){plan.deadline_utc,plan.deadline_utc,1096,32};sample_count=2;
    observe(0,1);CHECK(waits==1); /* Coarse gap never executes early. */
    reset();samples[0]=(Sample){plan.deadline_utc-1,plan.deadline_utc-1,1080,1};
    samples[1]=(Sample){plan.deadline_utc-1,plan.deadline_utc-1,1112,1};sample_count=2;
    observe(ERROR_TIME_SKEW,0);CHECK(waits==1); /* Rollback beyond fixed grace. */
    reset();samples[0]=(Sample){plan.armed_utc+10000,plan.armed_utc+10000,1080,32};
    samples[1]=(Sample){plan.armed_utc+10000,plan.armed_utc+10000,1112,32};sample_count=2;
    observe(ERROR_TIME_SKEW,0);CHECK(waits==1); /* Large delta still polls only fixed grace. */
    reset();samples[0]=(Sample){plan.armed_utc-1,plan.armed_utc-1,1000,32};sample_count=1;
    observe(ERROR_TIME_SKEW,0);CHECK(!waits);
    reset();samples[0]=(Sample){plan.deadline_utc-10000,plan.armed_utc-1,1080,32};sample_count=1;
    observe(ERROR_TIME_SKEW,0); /* Fresh sample backward below arm. */
    reset();samples[0]=(Sample){plan.deadline_utc,plan.deadline_utc,1112,32};sample_count=1;
    observe(0,1); /* Actual UTC deadline wins over grid cap. */
    reset();FILETIME exit,kernel,user;CHECK(GetProcessTimes(GetCurrentProcess(),&plan.supervisor_created,&exit,&kernel,&user));plan.supervisor_pid=GetCurrentProcessId();
    samples[0]=(Sample){plan.armed_utc+1,plan.armed_utc+1,~0ull-16,32};sample_count=1;
    L4CommunicationMonitor* monitor=NULL;L4Layout roots={0};L4CommunicationSignals signals={service,channels,barrier,NULL};
    CHECK(!l4_communication_monitor_start(&roots,L"c1752304-b933-4153-b7a5-72cb077033fe",&signals,&monitor));
    CHECK(GetLastError()==ERROR_ARITHMETIC_OVERFLOW && !monitor && !executions);
    printf("Communication monitor clock: %u checks, %u failures; scripted clocks, modeled state/guard/SYSTEM/SCM; no live services\n",checks,failures);
    return failures?1:0;
}
