#pragma once
#include "../../l4common/layout.h"
#include "../../l4common/broker_profile.h"
typedef L4BrokerProfile SetupBrokerConfig;
/* Pure active-profile renderer. SN is captured from authenticated terminal
 * discovery by the local installer, never an RPC-selected bridge identity.
 * No service/client/file writes, template fallback or duplicate MQTT connection. */
bool setup_broker_render(const L4Layout* layout,const char* sn,SetupBrokerConfig* output);

/* No terminal identity or upstream client: local deployment before enrollment. */
bool setup_broker_render_standby(const L4Layout* layout,SetupBrokerConfig* output);
