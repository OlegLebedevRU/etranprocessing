#pragma once
#include "../../l4common/update_state.h"
typedef struct L4CommunicationWatch L4CommunicationWatch;
/* Installed SYSTEM supervisor: original deadline monitor or one protected startup
 * reconciliation of an active original operation. Independent owner thread discovers
 * published private plans without deployment.lock or a new RPC/arming message.
 * Health/mode3 query never creates a plan/monitor or renews a deadline. */
bool supervisor_communication_watch_start(const L4UpdateConsumer* consumer,L4CommunicationWatch** watch);
bool supervisor_communication_watch_close(L4CommunicationWatch** watch,DWORD timeout);
/* Native read-only retained-owner proof underlying technical mode3 protection.
 * Requires compiled native boot registration and a valid startup snapshot.
 * No discovery/arming/deadline renewal or mutation. Lifecycle must be serialized
 * against watch_close; actual signed restart/channel acceptance is separate. */
DWORD supervisor_communication_watch_proof(L4CommunicationWatch* watch,const L4UpdateState* expected,DWORD timeout,HANDLE cancel);
/* Registered read-only v2 handler: actual retained independent monitor + original
 * worker/WAIT/state/epoch and fresh native SCM crash profile are mandatory. Not
 * channel readiness/update success, not permission to enable a remote executor. */
DWORD supervisor_communication_watch_query(L4CommunicationWatch* watch,const L4UpdateState* expected,DWORD timeout,HANDLE cancel);
