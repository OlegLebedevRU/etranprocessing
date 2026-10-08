#include "rollback.h"
#include <stdio.h>
static ULONGLONG utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
bool rollback_execute(const L4Layout* roots,const wchar_t* operation,bool boot){
    L4RecoveryGuard* guard=NULL;HANDLE runner=INVALID_HANDLE_VALUE,lock=INVALID_HANDLE_VALUE;
    if(!l4_recovery_open(roots,operation,1000,&guard))return false;
    const L4RecoveryPlan* plan=l4_recovery_plan(guard);ULONGLONG deadline=GetTickCount64()+plan->recovery_ms;
    L4RecoveryAction action;bool ok=l4_recovery_action(guard,utc(),boot,&action),begun=false;
    if(ok && (action==L4_RECOVERY_DONE || action==L4_RECOVERY_WAIT)){l4_recovery_close(guard);return true;}
    if(ok && action!=L4_RECOVERY_REQUIRED){ok=false;SetLastError(ERROR_INVALID_STATE);}
    l4_recovery_release(guard);
    /* Never wait for an executor while holding the decision lock: it must be
     * available to the previous executor's final result and the worker. */
    if(ok){runner=rollback_runner(roots,operation,deadline);ok=runner!=INVALID_HANDLE_VALUE;}
    if(ok)ok=l4_recovery_relock(guard,rollback_remaining(deadline)) && l4_recovery_action(guard,utc(),boot,&action);
    if(ok && action==L4_RECOVERY_DONE)goto done;
    if(ok){ok=action==L4_RECOVERY_REQUIRED && l4_recovery_begin(guard,utc(),boot);begun=ok;}
    l4_recovery_release(guard);
    if(ok)ok=rollback_worker(plan,deadline);
    /* Worker is observed exited, including its Job children. */
    if(ok){lock=rollback_lock(roots,deadline);ok=lock!=INVALID_HANDLE_VALUE;}
    if(ok)ok=rollback_supervisor(roots,plan,deadline);
    {DWORD error=ok?0:GetLastError();if(!ok && !error)error=ERROR_GEN_FAILURE;
        if(begun){
            /* Failure evidence also has a bounded attempt when the work budget
             * is exhausted. Never turn a late service result into success. */
            if(ok && !rollback_remaining(deadline)){ok=false;error=ERROR_TIMEOUT;}
            DWORD remaining=rollback_remaining(deadline);
            bool saved=l4_recovery_relock(guard,remaining?remaining:1000) &&
                l4_recovery_finish(guard,ok?L4_RECOVERY_RESTORED:L4_RECOVERY_FAILED,error,utc());
            if(!saved){ok=false;error=GetLastError();}
        }
        SetLastError(error);
    }
done: {DWORD error=GetLastError();l4_recovery_close(guard);if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);if(runner!=INVALID_HANDLE_VALUE)CloseHandle(runner);SetLastError(error);return ok;}
}
