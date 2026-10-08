#include "../update_guard.h"
#include <stdio.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL update-guard %u %s\n",__LINE__,#x);}}while(0)
static const char* owner="ea9c98c0-bcd2-4e87-9ab5-2df74eab41e7";
static const char* foreign="f4106499-d645-4e31-8913-c2b73f41937a";
typedef struct { L4UpdateGuard* guard;HANDLE admitted,release;bool entered,left; } Work;
static DWORD WINAPI accepted_work(void* context){
    Work* work=(Work*)context;L4UpdateTicket ticket={0};
    work->entered=l4_update_guard_enter(work->guard,L4_WORK_OTHER_TOOLS,GetTickCount64(),&ticket);
    SetEvent(work->admitted);
    if(WaitForSingleObject(work->release,5000)!=WAIT_OBJECT_0)return 1;
    work->left=work->entered && l4_update_guard_leave(&ticket);return work->left?0:1;
}
int main(void){
    L4UpdateGuard guard={0};L4UpdateTicket a={0},b={0},denied={0};
    CHECK(l4_update_guard_enter(&guard,L4_WORK_COMMUNICATION,100,&a));
    CHECK(l4_update_guard_enter(&guard,L4_WORK_OTHER_TOOLS,100,&b));
    CHECK(!l4_update_guard_enter(&guard,L4_WORK_OTHER_TOOLS,100,&b));
    CHECK(!l4_update_guard_begin(&guard,NULL,L4_UPDATE_COMMUNICATION,100,200));
    CHECK(!l4_update_guard_begin(&guard,"00000000-0000-0000-0000-000000000000",L4_UPDATE_COMMUNICATION,100,200));
    CHECK(!l4_update_guard_begin(&guard,"ea9c98c0-bcd2-4e87-9ab5-2df74eab41e7x",L4_UPDATE_COMMUNICATION,100,200));
    CHECK(!l4_update_guard_begin(&guard,owner,0,100,200));
    CHECK(!l4_update_guard_begin(&guard,owner,L4_UPDATE_COMMUNICATION,100,100));
    CHECK(l4_update_guard_begin(&guard,owner,L4_UPDATE_COMMUNICATION,100,200));
    CHECK(l4_update_guard_begin(&guard,"EA9C98C0-BCD2-4E87-9AB5-2DF74EAB41E7",L4_UPDATE_COMMUNICATION,101,200));
    CHECK(!l4_update_guard_begin(&guard,owner,L4_UPDATE_COMMUNICATION,101,300));
    CHECK(!l4_update_guard_begin(&guard,foreign,L4_UPDATE_COMMUNICATION,999,1000));
    CHECK(!l4_update_guard_begin(&guard,owner,L4_UPDATE_OTHER_TOOLS,101,200));
    CHECK(!l4_update_guard_ready(&guard,foreign,999));
    CHECK(!l4_update_guard_ready(&guard,owner,102));
    CHECK(!l4_update_guard_enter(&guard,L4_WORK_OTHER_TOOLS,102,&denied));
    CHECK(!l4_update_guard_enter(&guard,L4_WORK_COMMUNICATION,102,&denied));
    CHECK(!l4_update_guard_finish(&guard,owner));
    CHECK(l4_update_guard_leave(&b));CHECK(!l4_update_guard_leave(&b));
    CHECK(!l4_update_guard_ready(&guard,owner,103));
    CHECK(l4_update_guard_leave(&a));CHECK(l4_update_guard_ready(&guard,owner,104));
    CHECK(!l4_update_guard_finish(&guard,foreign));
    CHECK(l4_update_guard_ready(&guard,owner,199));
    CHECK(!l4_update_guard_ready(&guard,owner,200));
    CHECK(!l4_update_guard_ready(&guard,owner,100));
    CHECK(!l4_update_guard_enter(&guard,L4_WORK_COMMUNICATION,300,&denied));
    CHECK(!l4_update_guard_begin(&guard,owner,L4_UPDATE_COMMUNICATION,300,500));
    CHECK(l4_update_guard_finish(&guard,owner));CHECK(!l4_update_guard_finish(&guard,owner));
    CHECK(l4_update_guard_begin(&guard,owner,L4_UPDATE_OTHER_TOOLS,400,500));
    CHECK(l4_update_guard_enter(&guard,L4_WORK_COMMUNICATION,401,&a));
    CHECK(!l4_update_guard_enter(&guard,L4_WORK_OTHER_TOOLS,401,&denied));
    CHECK(l4_update_guard_ready(&guard,owner,402));
    CHECK(!l4_update_guard_finish(&guard,owner));CHECK(l4_update_guard_leave(&a));
    /* Expiry must be enforced at admission even without a readiness poll. */
    CHECK(!l4_update_guard_enter(&guard,L4_WORK_COMMUNICATION,500,&denied));
    CHECK(!l4_update_guard_ready(&guard,owner,501));
    CHECK(l4_update_guard_finish(&guard,owner));
    CHECK(l4_update_guard_begin(&guard,owner,L4_UPDATE_OTHER_TOOLS,600,700));
    /* Samples from concurrent callers may arrive out of order under the lock. */
    CHECK(l4_update_guard_enter(&guard,L4_WORK_COMMUNICATION,599,&denied));
    CHECK(l4_update_guard_leave(&denied));
    CHECK(l4_update_guard_ready(&guard,owner,601));
    CHECK(l4_update_guard_finish(&guard,owner));
    CHECK(!l4_update_guard_enter(&guard,(L4UpdateWork)2,700,&denied));
    CHECK(!l4_update_guard_ready(&guard,owner,700));
    /* Real cross-thread ownership: begin closes admission before drain completes;
     * it neither waits under a lock nor cancels the accepted asynchronous work. */
    for(unsigned i=0;i<32;i++){
        Work work={0};work.guard=&guard;
        work.admitted=CreateEventW(NULL,TRUE,FALSE,NULL);work.release=CreateEventW(NULL,TRUE,FALSE,NULL);
        CHECK(work.admitted && work.release);
        if(!work.admitted || !work.release){if(work.admitted)CloseHandle(work.admitted);if(work.release)CloseHandle(work.release);break;}
        HANDLE thread=CreateThread(NULL,0,accepted_work,&work,0,NULL);CHECK(thread!=NULL);
        if(!thread){CloseHandle(work.admitted);CloseHandle(work.release);break;}
        CHECK(WaitForSingleObject(work.admitted,5000)==WAIT_OBJECT_0);
        ULONGLONG now=GetTickCount64();CHECK(work.entered);
        CHECK(l4_update_guard_begin(&guard,owner,L4_UPDATE_COMMUNICATION,now,now+5000));
        CHECK(!l4_update_guard_ready(&guard,owner,GetTickCount64()));
        CHECK(!l4_update_guard_enter(&guard,L4_WORK_OTHER_TOOLS,GetTickCount64(),&denied));
        CHECK(!l4_update_guard_finish(&guard,owner));SetEvent(work.release);
        DWORD wait=WaitForSingleObject(thread,5000);CHECK(wait==WAIT_OBJECT_0);
        if(wait!=WAIT_OBJECT_0)return 1; /* Never free stack context of a live thread. */
        DWORD code=1;CHECK(GetExitCodeThread(thread,&code) && code==0 && work.left);
        CHECK(l4_update_guard_ready(&guard,owner,GetTickCount64()));
        CHECK(l4_update_guard_finish(&guard,owner));
        CloseHandle(thread);CloseHandle(work.admitted);CloseHandle(work.release);
    }
    printf("Update guard: %u passed, %u failed, %u total\n",checks-failures,failures,checks);
    return failures?1:0;
}
