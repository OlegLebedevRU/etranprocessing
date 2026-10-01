#include "proxy_probe.h"
#include "../../l4pin/src/http_client.h"
#include "../../leo4proxy/src/policy_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
bool setup_proxy_probe(int port, bool require_ready, int timeout_ms) {
    if(timeout_ms < 1 || port < 1 || port > 65535) return false;
    char url[128]; sprintf_s(url,sizeof(url),"http://127.0.0.1:%d/_leo4/info",port);
    char* body=NULL; size_t length=0;
    if(!http_get_simple(url,timeout_ms,&body,&length)) return false;
    PolicyJson json; char status[64]; bool found=false;
    bool valid=policy_json_parse(&json,body,length) &&
        policy_json_string(&json,policy_json_field(&json,0,"status"),status,sizeof(status)) &&
        policy_json_bool(&json,policy_json_field(&json,0,"certificate_found"),&found);
    valid=valid && ((!strcmp(status,"ready") && found) ||
        (!require_ready && !strcmp(status,"waiting_for_certificate") && !found));
    free(body); return valid;
}
