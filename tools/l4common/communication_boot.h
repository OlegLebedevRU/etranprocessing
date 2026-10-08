#pragma once
#include "communication_plan.h"
typedef struct L4CommunicationBoot L4CommunicationBoot;
/* Native restart admission, never RPC/CLI boot=true. Exact active window1,
 * original supervisor gone, current SCM SYSTEM old supervisor/hash, matching
 * protected supervisor/communication plans. Keeps old image/plan/process pins
 * and existing supervisor runner exclusion, never its short decision mutex.
 * No worker exit, SCM mutation, deadline renewal or marker clearing. Caller must
 * retain the permit until its synchronous native execute call actually exits. */
bool l4_communication_boot_open(const L4Layout* roots,const L4UpdateState* expected,
    L4CommunicationBoot** permit);
bool l4_communication_boot_check(L4CommunicationBoot* permit,const L4Layout* roots,
    const L4UpdateState* expected);
/* One native invocation per restarted supervisor process, even on timeout or
 * missing result publication. No production reset API. Admission anchors total/claim
 * budgets; construction and claim SHARE the existing initial verify reserve. */
bool l4_communication_boot_take(L4CommunicationBoot* permit,ULONGLONG* started);
void l4_communication_boot_close(L4CommunicationBoot* permit);

/* Remaining original admission reserve; zero never supplies a new budget. */
DWORD l4_communication_boot_remaining(const L4CommunicationBoot* permit);
