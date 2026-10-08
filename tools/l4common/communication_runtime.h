#pragma once
#include "communication_plan.h"
#include "communication_boot.h"
/* Native SYSTEM owner, not an RPC/CLI interface. Caller authenticates its fixed
 * local readiness adapter. No replacement MQTT client, marker clearing or retry.
 * Probe receives observed pinned service process; channels must use local proxy
 * endpoints without depending on a working broker; final barrier must use the
 * existing console transport with fresh REQ/RSP and EVT/EVA. Missing callbacks
 * refuse before durable STARTED. Production registration remains separate. */
typedef struct {
    bool (*probe)(unsigned service,DWORD pid,DWORD remaining_ms,void* context);
    bool (*channels)(DWORD proxy_pid,DWORD remaining_ms,void* context);
    bool (*barrier)(DWORD remaining_ms,void* context);
    void* context;
} L4CommunicationSignals;
bool l4_communication_execute(const L4Layout* roots,const L4UpdateState* expected,
    const L4CommunicationSignals* signals,L4CommunicationResult* result);
/* Verified native restart permit only, never a caller boot flag. Same fixed
 * recovery path/budget; may resume STARTED only after prior runner has exited.
 * Signal acquisition and startup registration remain separate mandatory gates. */
bool l4_communication_execute_boot(const L4Layout* roots,const L4UpdateState* expected,
    const L4CommunicationSignals* signals,L4CommunicationBoot* permit,L4CommunicationResult* result);
/* Exact UUID Job only. Does not kill a bare PID or a reused worker PID. */
bool l4_communication_worker_exit(const L4CommunicationPlan* plan,DWORD timeout);
/* Read-only independent arm/query proof: exact live worker epoch, original
 * private UUID Job membership/profile, supervisor outside Job. No termination. */
bool l4_communication_worker_live(const L4CommunicationPlan* plan);
