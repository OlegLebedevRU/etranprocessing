#pragma once
#include "communication_runtime.h"
typedef struct L4CommunicationMonitor L4CommunicationMonitor;
/* Original SYSTEM supervisor only, one immutable operation/deadline per monitor.
 * Independent Win32 thread, not an orchestration tick or heartbeat. Before
 * marker publication accepts only explicit clear at generation-1; afterwards
 * requires exact window1. Foreign/missing/corrupt state closes this monitor.
 * This API is NOT mode3 readiness: production boot recovery/adapter registration
 * remains mandatory before declaring protection. No boot boolean/RPC override. */
bool l4_communication_monitor_start(const L4Layout* roots,const wchar_t* operation,
    const L4CommunicationSignals* signals,L4CommunicationMonitor** monitor);
/* Read-only observation. Error from executor/storage is retained. */
bool l4_communication_monitor_poll(L4CommunicationMonitor* monitor,bool* finished,L4CommunicationResult* result);
/* Read-only native owner proof, NOT production mode3 admission. The independent
 * thread must have completed an actual valid state/deadline observation, retain
 * the same immutable plan, remain live/not cancelled/finished, and repeat worker
 * Job + WAIT decision + current clear/active marker checks. No arm/scan/renewal.
 * Caller serializes lifecycle against close and retains its original plan pin. */
bool l4_communication_monitor_proof(L4CommunicationMonitor* monitor,const L4CommunicationPin* pin,
    const L4UpdateState* expected,DWORD timeout,HANDLE cancel);
/* Cancel before claim; after claim wait for bounded executor, never detach/free
 * live thread or abandon its runner. On timeout caller retains monitor ownership.
 * Cancellation cannot clear marker or prove a successful recovery. */
bool l4_communication_monitor_close(L4CommunicationMonitor** monitor,DWORD timeout);
