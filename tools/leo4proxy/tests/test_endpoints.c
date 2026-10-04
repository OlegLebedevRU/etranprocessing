#include "../src/config.h"
#include <ws2tcpip.h>
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
    if (!_stricmp(name,"multi.example.com")) {
        memset(answer,0,sizeof(answer));answer[0].wType=answer[1].wType=DNS_TYPE_A;
        answer[0].dwTtl=answer[1].dwTtl=60;answer[0].pNext=&answer[1];
        InetPtonA(AF_INET,"8.8.8.8",&answer[0].Data.A.IpAddress);
        InetPtonA(AF_INET,"9.9.9.9",&answer[1].Data.A.IpAddress);
        *out=(PDNS_RECORD)answer;return ERROR_SUCCESS;
    }
    if (!_stricmp(name,"_disabled._tls.example.com")) {
        memset(answer,0,sizeof(answer)); answer[0].wType=DNS_TYPE_SRV; answer[0].dwTtl=60;
        answer[0].Data.SRV.pNameTarget="."; *out=(PDNS_RECORD)answer; return ERROR_SUCCESS;
    }
    if (!_stricmp(name,"_valid._tls.example.com")) {
        memset(answer,0,sizeof(answer)); answer[0].wType=DNS_TYPE_SRV; answer[0].dwTtl=60;
        answer[0].Data.SRV.pNameTarget="srv.example.com"; answer[0].Data.SRV.wPort=9444;
        *out=(PDNS_RECORD)answer; return ERROR_SUCCESS;
    }
    Sleep(!strncmp(name,"pending",7)?400:50); return DNS_ERROR_RCODE_NAME_ERROR;
}
#define DnsQuery_A fake_query
#undef DnsRecordListFree
#define DnsRecordListFree fake_free
#include "../src/endpoints.c"
static void expected_add(Leo4Endpoint* expected,int* count,const char* host,int port,const char* source) {
    for (int n=0;n<*count;n++) if (expected[n].port==port && !_stricmp(expected[n].host,host)) return;
    strcpy_s(expected[*count].host,MAX_HOST_LEN,host); expected[*count].port=port;
    strcpy_s(expected[(*count)++].source,24,source);
}
static void routing_matrix(void) {
    int cases=0;
    for (int srv=0;srv<2;srv++) for (int manual=0;manual<2;manual++)
    for (int dns=0;dns<3;dns++) for (int cache=0;cache<3;cache++)
    for (int recovery=0;recovery<2;recovery++) for (int bootstrap=0;bootstrap<2;bootstrap++)
    for (int lkg=0;lkg<2;lkg++) for (int aliases=0;aliases<2;aliases++)
    for (int channel=0;channel<4;channel++) {
        ProxyConfig c={0}; Leo4Endpoint actual[ENDPOINT_MAX], expected[ENDPOINT_MAX]={0}; int want=0;
        const char* owners[]={"_absent._tls.example.com","_valid._tls.example.com","_disabled._tls.example.com"};
        strcpy_s(c.mqtt_remote_host,MAX_HOST_LEN,"logical.example.com"); c.mqtt_remote_port=443;
        strcpy_s(c.http_remote_host,MAX_HOST_LEN,"logical.example.com"); c.http_remote_port=443;
        strcpy_s(c.stream_remote_host,MAX_HOST_LEN,"logical.example.com"); c.stream_remote_port=443;
        strcpy_s(c.rtp_tunnel_remote_host,MAX_HOST_LEN,"logical.example.com"); c.rtp_tunnel_remote_port=443;
        c.srv_enabled=srv; c.remote_explicit[channel]=manual;
        strcpy_s(c.srv_names[channel],MAX_HOST_LEN,owners[dns]);
        clear_policy(); memset(active,0,sizeof(active)); memset(active_until,0,sizeof(active_until));
        expires_at=(unsigned long long)time(NULL)+(cache==1?300:0); expires_tick=cache==1?GetTickCount64()+300000:0;
        if (cache) {
            strcpy_s(policy_hosts[channel].host,MAX_HOST_LEN,aliases?"logical.example.com":"policy.example.com");
            strcpy_s(policy_hosts[channel].source,24,"policy"); policy_hosts[channel].port=443;
            strcpy_s(policy_ips[channel][0].host,MAX_HOST_LEN,"8.8.8.8"); policy_ips[channel][0].port=443;
            strcpy_s(policy_ips[channel][0].source,24,"policy_ip"); ip_counts[channel]=1;
        }
        if (lkg) {strcpy_s(active[channel].host,MAX_HOST_LEN,"old-srv.example.com");active[channel].port=443;strcpy_s(active[channel].source,24,"srv");active_until[channel]=GetTickCount64()+60000;}
        if (bootstrap) {strcpy_s(c.policy_bootstrap_ip,16,aliases?"8.8.8.8":"87.242.100.34");c.policy_bootstrap_port=443;}
        bool disabled=srv && !manual && dns==2;
        if (!disabled) {
            if (srv && !manual && dns==1) expected_add(expected,&want,"srv.example.com",9444,"srv");
            if (srv && !manual && lkg) expected_add(expected,&want,"old-srv.example.com",443,"srv_lkg");
            if (!manual && cache==1) expected_add(expected,&want,aliases?"logical.example.com":"policy.example.com",443,"policy");
            expected_add(expected,&want,"logical.example.com",443,manual?"cli":"default");
            if (!manual && cache && (cache==1 || recovery)) expected_add(expected,&want,"8.8.8.8",443,"policy_ip");
            if (bootstrap && recovery) expected_add(expected,&want,c.policy_bootstrap_ip,443,"bootstrap_ip");
        }
        int got=endpoints_candidates(&c,channel,recovery,actual); assert(got==want);
        for (int n=0;n<want;n++) {assert(!strcmp(actual[n].host,expected[n].host));assert(actual[n].port==expected[n].port);assert(!strcmp(actual[n].source,expected[n].source));}
        ++cases;
    }
    printf("Routing matrix: %d combinations PASS\n",cases);
}
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
    char addresses[8][16];assert(endpoint_resolve_ipv4_all("multi.example.com",addresses,1000)==2);
    assert(!strcmp(addresses[0],"8.8.8.8") && !strcmp(addresses[1],"9.9.9.9"));
    routing_matrix();
    for(int ips=1;ips<=5;ips++) {
        Leo4Endpoint targets[ENDPOINT_MAX]={0};int remaining=6000;
        for(int n=0;n<ENDPOINT_MAX;n++)strcpy_s(targets[n].source,24,n<ENDPOINT_MAX-ips?"srv":"policy_ip");
        for(int n=0;n<ENDPOINT_MAX-ips;n++)remaining-=endpoint_attempt_timeout(targets,ENDPOINT_MAX,n,remaining);
        assert(remaining>=(ips*1500<3000?ips*1500:3000));
        for(int n=ENDPOINT_MAX-ips;n<ENDPOINT_MAX;n++) {
            int budget=endpoint_attempt_timeout(targets,ENDPOINT_MAX,n,remaining);
            assert(budget>=(ips*1500<3000?ips*1500:3000)/ips);remaining-=budget;
        }
    }
    puts("Worst-case fallback reservation: 5 saturated candidate lists PASS");
    for(int n=0;n<30;n++) {char host[64];sprintf_s(host,sizeof(host),"cache%d.example.com",n);assert(!dns_query(host,DNS_TYPE_A,candidates,1000));}
    LONG before=queries;assert(!dns_query("new-after-full.example.com",DNS_TYPE_A,candidates,1000));assert(queries==before+1);
    ULONGLONG start=GetTickCount64();
    for(int n=0;n<DNS_SLOTS;n++) {char host[64];sprintf_s(host,sizeof(host),"pending%d.example.com",n);assert(!dns_query(host,DNS_TYPE_A,candidates,0));}
    assert(!dns_query("while-all-pending.example.com",DNS_TYPE_A,candidates,2000));
    assert(GetTickCount64()-start<300);
    assert(endpoint_resolve_ipv4_all("87.242.100.34",addresses,1)==1);
    Sleep(450);puts("DNS cache eviction, 24-worker saturation and numeric bypass: PASS");
    endpoints_shutdown(); return 0;
}
