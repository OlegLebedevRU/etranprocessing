/* Isolated transport fixtures exercise concurrent budgets without external DNS/TLS. */
#include "upstream_probe.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
static int failed_channel=-1;
static volatile LONG active_workers, max_workers;
int endpoints_candidates_timed(const ProxyConfig* c,int channel,bool recovery,Leo4Endpoint* out,DWORD timeout) {
    (void)c; assert(timeout==UPSTREAM_PROBE_BUDGET_MS); assert(recovery==(channel==1));
    memset(out,0,sizeof(*out)); sprintf_s(out->host,MAX_HOST_LEN,"channel%d",channel);
    out->port=443; strcpy_s(out->source,24,"default"); return 1;
}
const char* endpoint_logical_name(const ProxyConfig* c,int channel) {(void)c;(void)channel;return "logical.example.com";}
int endpoint_attempt_timeout(const Leo4Endpoint* c,int count,int n,int remaining) {(void)c;int share=remaining/(count-n);return share>2500?2500:share;}
bool schannel_connect_endpoint(SChannelSession* s,CredHandle* creds,const char* host,const char* logical,int port,int timeout,bool media) {
    (void)creds; assert(!media && port==443 && !strcmp(logical,"logical.example.com"));
    memset(s,0,sizeof(*s)); int channel=host[7]-'0';
    LONG current=InterlockedIncrement(&active_workers),old;
    do {old=max_workers;if(current<=old)break;}while(InterlockedCompareExchange(&max_workers,current,old)!=old);
    Sleep(channel==failed_channel?(DWORD)timeout:60);
    InterlockedDecrement(&active_workers); return channel!=failed_channel;
}
void schannel_close(SChannelSession* s) {(void)s;}
int main(void) {
    int cases=0;
    for (int admission=0;admission<2;admission++) for(int enabled=0;enabled<4;enabled++)
    for (int acquired=0;acquired<2;acquired++) {
        UpstreamProbe probes[4]={0};ProxyConfig c={0};CredHandle creds={0};
        for(int n=0;n<4;n++) {probes[n].config=&c;probes[n].creds=&creds;probes[n].channel=n;probes[n].enabled=n<2 || (enabled&(1<<(n-2)));probes[n].admission=admission!=0;probes[n].acquired=acquired!=0;}
        upstream_probe_all(probes);
        for(int n=0;n<4;n++) {
            const char* expect=!probes[n].enabled?"skipped":n!=1 && !admission?"policy_blocked":acquired?"valid":"probe_failed";
            assert(!strcmp(probes[n].verdict,expect));
        }
        ++cases;
    }
    for (int failed=0;failed<4;failed++) {
        UpstreamProbe probes[4]={0};ProxyConfig c={0};CredHandle creds={0}; failed_channel=failed;
        for(int n=0;n<4;n++) {probes[n].config=&c;probes[n].creds=&creds;probes[n].channel=n;probes[n].enabled=true;probes[n].admission=true;probes[n].acquired=true;}
        ULONGLONG start=GetTickCount64();upstream_probe_all(probes);
        assert(GetTickCount64()-start<3500);assert(max_workers>=2);
        for(int n=0;n<4;n++) {assert(!strcmp(probes[n].verdict,n==failed?"probe_failed":"valid"));if(n!=failed)assert(probes[n].elapsed_ms<1000);}
        ++cases;
    }
    printf("Independent upstream diagnostics: %d combinations PASS\n",cases);return 0;
}
