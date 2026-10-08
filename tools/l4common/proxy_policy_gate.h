#pragma once
#include <windows.h>
#include "../leo4proxy/src/policy_json.h"
/* Production policy only. Candidate isolated mode must never use this gate.
 * A cached known server grant remains valid for the protocol's 72-hour window;
 * initial unknown grace is deliberately insufficient for a remote update. */
static inline bool l4_proxy_policy_gate(const PolicyJson* json,unsigned long long now){
    int p=policy_json_field(json,0,"policy");bool mqtt=false,https=false,pending=true;
    unsigned long long success=0,until=0,generation=0;char facts[256],error[256];
    return policy_json_bool(json,policy_json_field(json,p,"mqtt_rtp_allowed"),&mqtt) && mqtt &&
        policy_json_bool(json,policy_json_field(json,p,"outgoing_https_allowed"),&https) && https &&
        policy_json_bool(json,policy_json_field(json,p,"storage_pending"),&pending) && !pending &&
        policy_json_uint(json,policy_json_field(json,p,"last_success_at"),&success) && success && success<=now &&
        policy_json_uint(json,policy_json_field(json,p,"offline_allowed_until"),&until) && until<=32503680000ULL &&
        success<=32503680000ULL-259200ULL && until==success+259200ULL && now<until &&
        policy_json_uint(json,policy_json_field(json,p,"generation"),&generation) && generation &&
        policy_json_string(json,policy_json_field(json,p,"stop_facts"),facts,sizeof(facts)) && !*facts &&
        policy_json_string(json,policy_json_field(json,p,"last_error"),error,sizeof(error));
}
static inline unsigned long long l4_proxy_policy_utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);
    return ((((unsigned long long)t.dwHighDateTime<<32)|t.dwLowDateTime)-116444736000000000ULL)/10000000ULL;}
