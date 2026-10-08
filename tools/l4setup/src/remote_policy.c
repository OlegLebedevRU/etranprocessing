#include "remote_policy.h"
#include <string.h>
bool setup_remote_policy_at(ULONGLONG armed,SetupRemoteWorkerPolicy* p){
    if(!p){SetLastError(ERROR_INVALID_PARAMETER);return false;}memset(p,0,sizeof(*p));
    const ULONGLONG ticks=(ULONGLONG)SETUP_REMOTE_SUPERVISOR_WINDOW_MS*10000;
    if(!armed || armed>~0ull-ticks){SetLastError(ERROR_ARITHMETIC_OVERFLOW);return false;}
    L4CommunicationBudget budget={60000,60000,30000,180000,120000,300000,60000,1050000};
    if(!l4_communication_native_budget_valid(&budget))return false;
    /* Independent communication rollback must fit before the helper deadline;
     * whole-suite restoration has a separate reserve before its active window. */
    if(budget.total_ms>=SETUP_REMOTE_RESTORE_RESERVE_MS ||
       SETUP_REMOTE_COMMUNICATION_WINDOW_MS+budget.total_ms>=SETUP_REMOTE_SUPERVISOR_WINDOW_MS ||
       SETUP_REMOTE_LAUNCH_MS+SETUP_REMOTE_RESTORE_RESERVE_MS>=SETUP_REMOTE_COMMUNICATION_WINDOW_MS){
        SetLastError(ERROR_INVALID_DATA);return false;
    }
    p->armed_utc=armed;p->communication_deadline_utc=armed+(ULONGLONG)SETUP_REMOTE_COMMUNICATION_WINDOW_MS*10000;
    p->supervisor_deadline_utc=armed+ticks;p->communication=budget;
    p->recovery_ms=300000;p->overhead_ms=30000;p->cleanup_ms=60000;p->timeout_ms=SETUP_REMOTE_LAUNCH_MS;
    return true;
}
