#pragma once
#include <windows.h>
#include <stdbool.h>
#include "mqtt_protocol.h"
#include "../../l4common/link_probe_evidence.h"
enum {LINK_IDLE,LINK_QUEUED,LINK_WAIT_RSP,LINK_NEED_EVENT,LINK_WAIT_EVA,LINK_DONE,LINK_FAILED};
typedef struct {unsigned generation,stage,event_id;ULONGLONG deadline;DWORD error;char correlation[40],request_nonce[40];L4LinkProbeEvidence evidence;} LinkProbe;
bool link_probe_evidence(const LinkProbe* probe,L4LinkProbeEvidence* evidence);
bool link_probe_begin(LinkProbe* probe,ULONGLONG now,DWORD timeout);
void link_probe_rsp(LinkProbe* probe,ULONGLONG now,const MqttRpcMetadata* metadata,const char* body,size_t size);
void link_probe_eva(LinkProbe* probe,ULONGLONG now,const MqttRpcMetadata* metadata,const char* body,size_t size);
