#include "../src/remote_policy.h"
#include <stdio.h>
static unsigned passed,failed;
#define CHECK(x) do{if(x)passed++;else{failed++;printf("FAIL %u %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
int main(void){
    SetupRemoteWorkerPolicy p;const ULONGLONG start=134000000000000000ull;
    CHECK(!setup_remote_policy_at(0,&p));CHECK(!setup_remote_policy_at(~0ull,&p));
    CHECK(!setup_remote_policy_at(start,NULL));CHECK(setup_remote_policy_at(start,&p));
    CHECK(p.communication.mosquitto_ms==300000);
    ULONGLONG independent=4ull*p.communication.verify_ms+p.communication.worker_ms+p.communication.lock_ms+
        p.communication.proxy_ms+p.communication.broker_prepare_ms+p.communication.mosquitto_ms+2ull*p.communication.channel_ms;
    CHECK(independent<=p.communication.total_ms);
    CHECK(p.communication_deadline_utc+(ULONGLONG)p.communication.total_ms*10000<p.supervisor_deadline_utc);
    CHECK(p.communication.total_ms<SETUP_REMOTE_RESTORE_RESERVE_MS);
    /* Four STOP/SWITCH/START ceilings, shared broker START/probe, old proxy
     * validation and final four-health + fresh transport barrier. */
    ULONGLONG restore=11ull*60000+300000+60000+540000;
    CHECK(restore+240000<=SETUP_REMOTE_RESTORE_RESERVE_MS);
    ULONGLONG forward=p.communication.proxy_ms+(ULONGLONG)p.communication.broker_prepare_ms+p.communication.mosquitto_ms+
        p.communication.channel_ms+4ull*p.communication.verify_ms;
    CHECK(forward+SETUP_REMOTE_LAUNCH_MS+SETUP_REMOTE_RESTORE_RESERVE_MS<=SETUP_REMOTE_COMMUNICATION_WINDOW_MS);
    ULONGLONG full_communication=SETUP_REMOTE_LAUNCH_MS+180000ull+300000+60000+900000+
          SETUP_REMOTE_RESTORE_RESERVE_MS+180000;
    CHECK(full_communication*10000<=p.communication_deadline_utc-start);
    CHECK(SETUP_REMOTE_CONTROLLER_WAIT_MS>SETUP_REMOTE_SUPERVISOR_WINDOW_MS+p.cleanup_ms);
    CHECK(p.timeout_ms==SETUP_REMOTE_LAUNCH_MS && p.cleanup_ms && p.overhead_ms>=L4_RECOVERY_TASK_MIN_OVERHEAD_MS);
    const ULONGLONG upper=~0ull-(ULONGLONG)SETUP_REMOTE_SUPERVISOR_WINDOW_MS*10000;
    CHECK(setup_remote_policy_at(upper,&p));CHECK(p.supervisor_deadline_utc==~0ull);
    CHECK(!setup_remote_policy_at(upper+1,&p));
    printf("Fixed remote policy: %u passed, %u failed (ceiling/reserve arithmetic only)\n",passed,failed);return failed?1:0;
}
