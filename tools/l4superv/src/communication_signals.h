#pragma once
#include "../../l4common/communication_runtime.h"
typedef struct L4SignalProfile L4SignalProfile;
/* Fixed original signed source from held immutable pin. Observe existing source
 * pair and source-version L4Con, pin its process/epoch. No SCM mutation or MQTT
 * CONNECT. Ports from source command/saved broker snapshot, not mutable config,
 * DNS, initiator input or defaults learned from an HTTP response. Caller retains
 * profile until all monitor callbacks finish. Opening is a bounded local probe,
 * not an independent boot/watchdog readiness proof. */
bool supervisor_signals_open(const L4Layout* roots,const L4CommunicationPin* plan,
    DWORD timeout,L4SignalProfile** profile,L4CommunicationSignals* signals);
void supervisor_signals_close(L4SignalProfile* profile);

/* Interrupted-link acquisition, guarded native restart only. Fixed protected
 * old Con/hash/SCM and selected certificate; no proxy/broker/IPC preflight and
 * no MQTT bytes. A new Con epoch requires original gone and same pinned old EXE.
 * Waits for exact automatic old Con in STOPPED/START_PENDING within the same
 * initial permit reserve; missing/failed/stopping services refuse. No service
 * start, extended timeout or durable readiness ACK. */
bool supervisor_signals_open_boot(const L4Layout* roots,const L4CommunicationPin* plan,
    L4CommunicationBoot* boot,const L4UpdateState* expected,L4SignalProfile** profile,L4CommunicationSignals* signals);
