#include "../src/config.h"
#pragma warning(push)
#pragma warning(disable:4201)
#include <windns.h>
#pragma warning(pop)
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static volatile LONG queries;
static DNS_RECORDA answer[2];
static void fake_free(void* value,DNS_FREE_TYPE type) { (void)value; (void)type; }
static DNS_STATUS WINAPI fake_query(PCSTR name,WORD type,DWORD flags,PVOID extra,PDNS_RECORD* out,PVOID* reserved) {
    (void)type; (void)flags; (void)extra; (void)reserved;
    InterlockedIncrement(&queries);
    if (!_stricmp(name,"_disabled._tls.example.com")) {
        memset(answer,0,sizeof(answer)); answer[0].wType=DNS_TYPE_SRV; answer[0].dwTtl=60;
        answer[0].Data.SRV.pNameTarget="."; *out=(PDNS_RECORD)answer; return ERROR_SUCCESS;
    }
    Sleep(50); return DNS_ERROR_RCODE_NAME_ERROR;
}
#define DnsQuery_A fake_query
#undef DnsRecordListFree
#define DnsRecordListFree fake_free
#include "../src/endpoints.c"
int main(void) {
    assert(endpoint_ipv4("8.8.8.8")); assert(!endpoint_ipv4("127.0.0.1"));
    assert(!endpoint_ipv4("10.0.0.1")); assert(!endpoint_ipv4("224.0.0.1"));
    assert(!endpoint_ipv4("100.64.0.1")); assert(!endpoint_ipv4("8.8.8.8:443"));
    assert(endpoint_host_valid("service.example.com")); assert(!endpoint_host_valid("https://example.com"));
    assert(!endpoint_host_valid("bad..name")); assert(!endpoint_host_valid("-bad.example"));
    ProxyConfig config={0}; strcpy_s(config.http_remote_host,MAX_HOST_LEN,"logical.example.com"); config.http_remote_port=443;
    const char* response="{\"v\":1,\"sn\":\"test\",\"endpoints_ttl_seconds\":300,\"endpoints\":{\"https\":{\"host\":\"new.example.com\",\"port\":9443,\"fallback_ips\":[\"8.8.8.8\",\"127.0.0.1\"]}}}";
    unsigned long long now=(unsigned long long)time(NULL);
    assert(!parse_policy(response,strlen(response),"wrong",now));
    assert(parse_policy(response,strlen(response),"test",now));
    assert(ip_counts[1]==1 && policy_hosts[1].port==9443);
    Leo4Endpoint candidates[ENDPOINT_MAX]; int count=endpoints_candidates(&config,1,false,candidates);
    assert(count==3 && !strcmp(candidates[0].source,"policy") && !strcmp(candidates[2].source,"policy_ip"));
    assert(!strcmp(endpoint_logical_name(&config,1),"logical.example.com"));
    config.remote_explicit[1]=1;
    assert(endpoints_candidates(&config,1,false,candidates)==1 && !strcmp(candidates[0].source,"cli"));
    config.remote_explicit[1]=0; expires_at=now-1; expires_tick=0;
    assert(endpoints_candidates(&config,1,false,candidates)==1);
    assert(endpoints_candidates(&config,1,true,candidates)==2); /* stale IP recovery only */
    const char* removed="{\"v\":1,\"sn\":\"test\"}";
    assert(parse_policy(removed,strlen(removed),"test",now) && !ip_counts[1]);
    const char* badttl="{\"sn\":\"test\",\"endpoints_ttl_seconds\":1}";
    assert(!parse_policy(badttl,strlen(badttl),"test",now));
    assert(dns_query("absent.example.com",DNS_TYPE_SRV,candidates,1000)==0);
    assert(dns_query("absent.example.com",DNS_TYPE_SRV,candidates,1000)==0 && queries==1);
    char ip[16]; assert(endpoint_resolve_ipv4("8.8.8.8",ip,1) && queries==1);
    config.srv_enabled=1; strcpy_s(config.srv_names[1],MAX_HOST_LEN,"_disabled._tls.example.com");
    assert(endpoints_candidates(&config,1,true,candidates)==0); /* SRV '.' is explicit unavailability */
    config.srv_enabled=0; config.policy_bootstrap_port=443; strcpy_s(config.policy_bootstrap_ip,16,"87.242.100.34");
    count=endpoints_candidates(&config,1,true,candidates);
    assert(count==2 && !strcmp(candidates[count-1].source,"bootstrap_ip"));
    assert(endpoints_candidates(&config,1,false,candidates)==1);
    assert(!parse_policy(response,strlen(response),"test",now+1000));
    puts("Endpoint tests: PASS (IP validation, source priority, expiry, identity, negative cache, DNS-free IP)");
    endpoints_shutdown(); return 0;
}
