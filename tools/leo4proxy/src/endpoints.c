#include "endpoints.h"
#include "policy_json.h"
#pragma warning(push)
#pragma warning(disable:4201)
#include <windns.h>
#pragma warning(pop)
#include <ws2tcpip.h>
#include <sddl.h>
#include <shlobj.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

/* Bounded single-flight DNS slots. Workers never borrow config/session memory.
 * Caller deadlines do not attempt unsafe cancellation of Win7 DnsQuery_A.
 * A slot is never recycled while its query is running. */
#define DNS_SLOTS 24
typedef struct {
    char name[MAX_HOST_LEN];
    WORD type;
    HANDLE done;
    volatile LONG running;
    ULONGLONG expires;
    int count;
    Leo4Endpoint entries[8];
} DnsSlot;
static SRWLOCK dns_lock=SRWLOCK_INIT, endpoint_lock=SRWLOCK_INIT;
static DnsSlot slots[DNS_SLOTS];
static bool shutting_down;
static const char* channels[]={"mqtt","https","l4stream","l4rtp"};
static Leo4Endpoint policy_hosts[4], policy_ips[4][4], active[4];
static int ip_counts[4];
static ULONGLONG active_until[4];
static char identity[MAX_SN_LEN];
static unsigned long long received_at, expires_at;
static ULONGLONG expires_tick;
static const wchar_t* cache_key=L"SOFTWARE\\Leo4\\Leo4Proxy\\Policy";
static wchar_t cache_file[MAX_PATH];

bool endpoint_ipv4(const char* text) {
    IN_ADDR address;
    if (!text || InetPtonA(AF_INET,text,&address)!=1) return false;
    unsigned long ip=ntohl(address.S_un.S_addr);
    return (ip>>24)!=0 && (ip>>24)!=127 && (ip>>24)<224 &&
        (ip>>24)!=10 && (ip>>16)!=0xa9fe && (ip>>16)!=0xc0a8 &&
        (ip>>20)!=0xac1 && (ip>>22)!=0x191;
}
bool endpoint_host_valid(const char* text) {
    if (!text || !*text || strlen(text)>253) return false;
    size_t label=0;
    for (size_t n=0;text[n];n++) {
        unsigned char c=(unsigned char)text[n];
        if (c=='.') { if (!label || label>63 || text[n-1]=='-') return false; label=0; }
        else {
            if (!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='-') ||
                (!label && c=='-')) return false;
            ++label;
        }
    }
    return label && label<=63 && text[strlen(text)-1]!='-';
}
static unsigned __stdcall dns_worker(void* value) {
    DnsSlot* slot=(DnsSlot*)value;
    PDNS_RECORDA records=NULL;
    DNS_STATUS status=DnsQuery_A(slot->name,slot->type,DNS_QUERY_BYPASS_CACHE,NULL,(PDNS_RECORD*)&records,NULL);
    Leo4Endpoint entries[8]={0}; int count=0; DWORD ttl=300;
    if (!status) for (PDNS_RECORDA r=records;r && count<8;r=r->pNext) {
        if (r->wType!=slot->type) continue;
        Leo4Endpoint* e=&entries[count];
        if (r->dwTtl<ttl) ttl=r->dwTtl;
        if (slot->type==DNS_TYPE_SRV) {
            if (!r->Data.SRV.pNameTarget) continue;
            if (!strcmp(r->Data.SRV.pNameTarget,".")) {
                memset(entries,0,sizeof(entries));
                strcpy_s(entries[0].source,sizeof(entries[0].source),"srv_disabled");
                count=1; break;
            }
            if (!r->Data.SRV.pNameTarget || strlen(r->Data.SRV.pNameTarget)>=sizeof(e->host)) continue;
            strcpy_s(e->host,sizeof(e->host),r->Data.SRV.pNameTarget);
            size_t len=strlen(e->host); if (len && e->host[len-1]=='.') e->host[len-1]=0;
            if (!endpoint_host_valid(e->host) || !r->Data.SRV.wPort) continue;
            e->port=r->Data.SRV.wPort; e->priority=r->Data.SRV.wPriority; e->weight=r->Data.SRV.wWeight;
            strcpy_s(e->source,sizeof(e->source),"srv");
        } else {
            IN_ADDR address; address.S_un.S_addr=r->Data.A.IpAddress;
            if (!InetNtopA(AF_INET,&address,e->host,sizeof(e->host))) continue;
        }
        ++count;
    }
    if (records) DnsRecordListFree(records,DnsFreeRecordList);
    AcquireSRWLockExclusive(&dns_lock);
    memcpy(slot->entries,entries,sizeof(entries)); slot->count=count;
    slot->expires=GetTickCount64()+(count?(ttl<30?30:ttl)*1000ULL:30000ULL);
    InterlockedExchange(&slot->running,0); SetEvent(slot->done);
    ReleaseSRWLockExclusive(&dns_lock);
    return 0;
}
static int dns_query(const char* name, WORD type, Leo4Endpoint* out, DWORD timeout) {
    DnsSlot* slot=NULL; DnsSlot* free_slot=NULL; DnsSlot* completed_slot=NULL;
    AcquireSRWLockExclusive(&dns_lock);
    if (!shutting_down) for (int n=0;n<DNS_SLOTS;n++) {
        DnsSlot* s=&slots[n];
        if (s->type==type && !_stricmp(s->name,name)) { slot=s; break; }
        if (!free_slot && !s->running && (!s->done || GetTickCount64()>=s->expires)) free_slot=s;
        if (!s->running && (!completed_slot || s->expires<completed_slot->expires)) completed_slot=s;
    }
    bool evicted=false;
    if (!shutting_down && !slot) {slot=free_slot?free_slot:completed_slot;evicted=slot && !free_slot;}
    if (!slot) { ReleaseSRWLockExclusive(&dns_lock); return 0; }
    if (!slot->running && (evicted || !slot->done || GetTickCount64()>=slot->expires)) {
        if (!slot->done) slot->done=CreateEventW(NULL,TRUE,FALSE,NULL);
        if (!slot->done) { ReleaseSRWLockExclusive(&dns_lock); return 0; }
        ResetEvent(slot->done); strcpy_s(slot->name,sizeof(slot->name),name); slot->type=type;
        slot->count=0; slot->running=1;
        HANDLE thread=(HANDLE)_beginthreadex(NULL,0,dns_worker,slot,0,NULL);
        if (thread) CloseHandle(thread);
        else { slot->running=0; slot->expires=GetTickCount64()+30000; SetEvent(slot->done); }
    }
    /* A slot can be reused after completion; re-check identity under the lock. */
    HANDLE event=slot->done;
    ReleaseSRWLockExclusive(&dns_lock);
    if (WaitForSingleObject(event,timeout)!=WAIT_OBJECT_0) return 0;
    AcquireSRWLockShared(&dns_lock);
    int count=0;
    if (!slot->running && slot->type==type && !_stricmp(slot->name,name)) {
        count=slot->count; memcpy(out,slot->entries,(size_t)count*sizeof(*out));
    }
    ReleaseSRWLockShared(&dns_lock); return count;
}
int endpoint_resolve_ipv4_all(const char* host,char out[8][16],DWORD timeout) {
    IN_ADDR address;
    if (InetPtonA(AF_INET,host,&address)==1) { strcpy_s(out[0],16,host); return 1; }
    Leo4Endpoint entries[8];
    int count=dns_query(host,DNS_TYPE_A,entries,timeout), unique=0;
    for (int n=0;n<count;n++) {
        bool duplicate=false;
        for (int k=0;k<unique;k++) if (!strcmp(out[k],entries[n].host)) duplicate=true;
        if (!duplicate) strcpy_s(out[unique++],16,entries[n].host);
    }
    return unique;
}
bool endpoint_resolve_ipv4(const char* host,char out[16],DWORD timeout) {
    char addresses[8][16];
    if (!endpoint_resolve_ipv4_all(host,addresses,timeout)) return false;
    strcpy_s(out,16,addresses[0]); return true;
}
const char* endpoint_logical_name(const ProxyConfig* c,int channel) {
    switch(channel) {
        case ENDPOINT_MQTT: return c->mqtt_remote_host;
        case ENDPOINT_STREAM: return c->stream_remote_host;
        case ENDPOINT_RTP: return c->rtp_tunnel_remote_host;
        default: return c->http_remote_host;
    }
}
static int channel_port(const ProxyConfig* c,int channel) {
    switch(channel) {
        case ENDPOINT_MQTT: return c->mqtt_remote_port;
        case ENDPOINT_STREAM: return c->stream_remote_port;
        case ENDPOINT_RTP: return c->rtp_tunnel_remote_port;
        default: return c->http_remote_port;
    }
}
int endpoints_candidates_timed(const ProxyConfig* c,int channel,bool recovery,Leo4Endpoint* out,DWORD timeout) {
    if (channel<0 || channel>=4) return 0;
    int count=0;
    if (c->srv_enabled && !c->remote_explicit[channel]) {
        char owner[MAX_HOST_LEN]; const char* services[]={"_mqtt._tls.","_https._tcp.","_l4stream._tls.","_l4rtp._tls."};
        int len=snprintf(owner,sizeof(owner),"%s%s",services[channel],endpoint_logical_name(c,channel));
        if (c->srv_names[channel][0]) strcpy_s(owner,sizeof(owner),c->srv_names[channel]);
        else if (len<0 || len>=MAX_HOST_LEN) owner[0]=0;
        if (owner[0]) count=dns_query(owner,DNS_TYPE_SRV,out,timeout<2000?timeout:2000);
        if (count && !strcmp(out[0].source,"srv_disabled")) return 0;
        /* RFC 2782 weighted selection without replacement, priority first. */
        for (int n=0;n<count;n++) {
            unsigned short priority=65535; unsigned int total=0,random=0;
            for (int k=n;k<count;k++) if (out[k].priority<priority) priority=out[k].priority;
            for (int k=n;k<count;k++) if (out[k].priority==priority) total+=out[k].weight;
            random=(unsigned int)GetTickCount() ^ (unsigned int)rand(); unsigned int pick=total?random%(total+1):0; int selected=n;
            for (int k=n;k<count;k++) if (out[k].priority==priority) {
                selected=k; if (!total || pick<=out[k].weight) break; pick-=out[k].weight;
            }
            Leo4Endpoint temp=out[n]; out[n]=out[selected]; out[selected]=temp;
        }
    }
    AcquireSRWLockShared(&endpoint_lock);
    if (!c->remote_explicit[channel] && c->srv_enabled &&
        active_until[channel]>GetTickCount64() && !strcmp(active[channel].source,"srv")) {
        bool duplicate=false;
        for (int n=0;n<count;n++) if (out[n].port==active[channel].port && !_stricmp(out[n].host,active[channel].host)) duplicate=true;
        if (!duplicate && count<ENDPOINT_MAX-1) {
            out[count]=active[channel]; strcpy_s(out[count++].source,24,"srv_lkg");
        }
    }
    bool fresh=expires_at>(unsigned long long)time(NULL) && GetTickCount64()<expires_tick;
    if (!c->remote_explicit[channel] && fresh && policy_hosts[channel].host[0]) out[count++]=policy_hosts[channel];
    Leo4Endpoint fallback={0}; strcpy_s(fallback.host,sizeof(fallback.host),endpoint_logical_name(c,channel));
    fallback.port=channel_port(c,channel); strcpy_s(fallback.source,sizeof(fallback.source),c->remote_explicit[channel]?"cli":"default");
    out[count++]=fallback;
    if (!c->remote_explicit[channel] && (fresh || recovery))
        for (int n=0;n<ip_counts[channel] && count<ENDPOINT_MAX-1;n++) out[count++]=policy_ips[channel][n];
    ReleaseSRWLockShared(&endpoint_lock);
    if (recovery && c->policy_bootstrap_ip[0] && count<ENDPOINT_MAX) {
        Leo4Endpoint bootstrap={0}; strcpy_s(bootstrap.host,sizeof(bootstrap.host),c->policy_bootstrap_ip);
        bootstrap.port=c->policy_bootstrap_port; strcpy_s(bootstrap.source,sizeof(bootstrap.source),"bootstrap_ip"); out[count++]=bootstrap;
    }
    /* Keep the earliest source/order; aliases must not consume retry budgets. */
    int unique=0;
    for (int n=0;n<count;n++) {
        bool duplicate=false;
        for (int k=0;k<unique;k++) if (out[k].port==out[n].port && !_stricmp(out[k].host,out[n].host)) duplicate=true;
        if (!duplicate) out[unique++]=out[n];
    }
    return unique;
}
int endpoints_candidates(const ProxyConfig* c,int channel,bool recovery,Leo4Endpoint* out) {
    return endpoints_candidates_timed(c,channel,recovery,out,2000);
}
int endpoint_attempt_timeout(const Leo4Endpoint* candidates,int count,int index,int remaining_ms) {
    if(!candidates || index<0 || index>=count || remaining_ms<=0)return 0;
    int ips=0,primary=0;
    for(int n=index;n<count;n++) {
        if(!strcmp(candidates[n].source,"policy_ip") || !strcmp(candidates[n].source,"bootstrap_ip"))++ips;
        else ++primary;
    }
    bool fallback=!strcmp(candidates[index].source,"policy_ip") || !strcmp(candidates[index].source,"bootstrap_ip");
    int share;
    if(ips && !fallback) {
        int reserved=ips*1500;if(reserved>3000)reserved=3000;
        if(remaining_ms<=reserved)return 0; /* Preserve recovery instead of spending it on DNS. */
        share=(remaining_ms-reserved)/primary;
    } else share=remaining_ms/(count-index);
    return share>2500?2500:share;
}
static void clear_policy(void) { memset(policy_hosts,0,sizeof(policy_hosts)); memset(policy_ips,0,sizeof(policy_ips)); memset(ip_counts,0,sizeof(ip_counts)); }
static bool parse_policy(const char* text,size_t len,const char* sn,unsigned long long received) {
    PolicyJson json; char found[MAX_SN_LEN]; unsigned long long ttl=86400;
    if (!policy_json_parse(&json,text,len) ||
        !policy_json_string(&json,policy_json_field(&json,0,"sn"),found,sizeof(found)) || strcmp(found,sn)) return false;
    int ttl_field=policy_json_field(&json,0,"endpoints_ttl_seconds");
    if (ttl_field>=0 && (!policy_json_uint(&json,ttl_field,&ttl) || ttl<300 || ttl>604800)) return false;
    Leo4Endpoint hosts[4]={0}, ips[4][4]={0}; int counts[4]={0};
    int endpoints=policy_json_field(&json,0,"endpoints");
    for (int c=0;c<4;c++) {
        int entry=policy_json_field(&json,endpoints,channels[c]); unsigned long long port;
        if (!policy_json_string(&json,policy_json_field(&json,entry,"host"),hosts[c].host,sizeof(hosts[c].host)) ||
            !endpoint_host_valid(hosts[c].host) || !policy_json_uint(&json,policy_json_field(&json,entry,"port"),&port) || !port || port>65535) {
            memset(&hosts[c],0,sizeof(hosts[c])); continue;
        }
        hosts[c].port=(int)port; strcpy_s(hosts[c].source,sizeof(hosts[c].source),"policy");
        int list=policy_json_field(&json,entry,"fallback_ips");
        if (list>=0 && json.tokens[list].type=='[') for (int k=list+1;k<json.tokens[list].next && counts[c]<4;k=json.tokens[k].next) {
            Leo4Endpoint ip=hosts[c];
            if (policy_json_string(&json,k,ip.host,sizeof(ip.host)) && endpoint_ipv4(ip.host)) {
                strcpy_s(ip.source,sizeof(ip.source),"policy_ip"); ips[c][counts[c]++]=ip;
            }
        }
    }
    unsigned long long now=(unsigned long long)time(NULL);
    if (received>now || received>ULLONG_MAX-ttl) return false;
    memcpy(policy_hosts,hosts,sizeof(hosts)); memcpy(policy_ips,ips,sizeof(ips)); memcpy(ip_counts,counts,sizeof(counts));
    received_at=received; expires_at=received+ttl; expires_tick=GetTickCount64()+(expires_at>now?(expires_at-now)*1000:0);
    return true;
}
void endpoints_init(const ProxyConfig* config) {
    (void)config;
    wchar_t base[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL,CSIDL_COMMON_APPDATA,NULL,SHGFP_TYPE_CURRENT,base)))
        swprintf_s(cache_file,MAX_PATH,L"%ls\\Leo4Proxy\\endpoints.cache",base);
}
void endpoints_identity(const char* sn) {
    AcquireSRWLockExclusive(&endpoint_lock);
    if (!strcmp(identity,sn)) { ReleaseSRWLockExclusive(&endpoint_lock); return; }
    strcpy_s(identity,sizeof(identity),sn); clear_policy(); memset(active,0,sizeof(active)); memset(active_until,0,sizeof(active_until)); received_at=expires_at=0; expires_tick=0;
    char data[16393]; DWORD length=sizeof(data),type=0; HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,cache_key,0,KEY_QUERY_VALUE|KEY_WOW64_32KEY,&key)==ERROR_SUCCESS) {
        if (RegQueryValueExW(key,L"Endpoints",NULL,&type,(BYTE*)data,&length)==ERROR_SUCCESS && type==REG_BINARY && length>8 && length<=16392) {
            unsigned long long received; memcpy(&received,data,8); parse_policy(data+8,length-8,sn,received);
        }
        RegCloseKey(key);
    }
    if (!received_at && cache_file[0]) {
        FILE* f=NULL;
        if (!_wfopen_s(&f,cache_file,L"rb") && f) {
            unsigned long long received;
            if (fread(&received,8,1,f)==1) { size_t bytes=fread(data,1,16385,f); if (bytes<=16384) parse_policy(data,bytes,sn,received); }
            fclose(f);
        }
    }
    ReleaseSRWLockExclusive(&endpoint_lock);
}
void endpoints_accept(const char* text,size_t length,const char* sn) {
    if (!text || !sn || length>16384) return;
    AcquireSRWLockExclusive(&endpoint_lock);
    unsigned long long received=(unsigned long long)time(NULL);
    if (strcmp(identity,sn) || !parse_policy(text,length,sn,received)) { ReleaseSRWLockExclusive(&endpoint_lock); return; }
    char data[16393]; memcpy(data,&received,8); memcpy(data+8,text,length);
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,cache_key,0,KEY_SET_VALUE|KEY_WOW64_32KEY,&key)==ERROR_SUCCESS) {
        RegSetValueExW(key,L"Endpoints",0,REG_BINARY,(BYTE*)data,(DWORD)length+8); RegFlushKey(key); RegCloseKey(key);
    }
    if (cache_file[0]) {
        wchar_t temp[MAX_PATH]; swprintf_s(temp,MAX_PATH,L"%ls.tmp",cache_file);
        PSECURITY_DESCRIPTOR sd=NULL;
        ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",SDDL_REVISION_1,&sd,NULL);
        SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};
        HANDLE file=sd?CreateFileW(temp,GENERIC_WRITE,0,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL):INVALID_HANDLE_VALUE;
        if (file!=INVALID_HANDLE_VALUE) {
            DWORD wrote=0; bool ok=WriteFile(file,data,(DWORD)length+8,&wrote,NULL) && wrote==length+8 && FlushFileBuffers(file);
            CloseHandle(file);
            if (ok) MoveFileExW(temp,cache_file,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
            else DeleteFileW(temp);
        }
        if (sd) LocalFree(sd);
    }
    ReleaseSRWLockExclusive(&endpoint_lock);
}
void endpoints_connected(int channel,const Leo4Endpoint* endpoint) {
    if (channel<0 || channel>=4 || !endpoint) return;
    AcquireSRWLockExclusive(&endpoint_lock);
    active[channel]=*endpoint;
    active_until[channel]=GetTickCount64()+300000;
    if (!strcmp(active[channel].source,"srv_lkg")) strcpy_s(active[channel].source,24,"srv");
    ReleaseSRWLockExclusive(&endpoint_lock);
}
void endpoints_diagnostics(char* out,size_t size) {
    AcquireSRWLockShared(&endpoint_lock); size_t used=0;
    int wrote=snprintf(out,size,"{\"expires_at\":%llu,\"channels\":{",expires_at);
    if (wrote>0) used=(size_t)wrote;
    for (int c=0;c<4 && used<size;c++) {
        wrote=snprintf(out+used,size-used,"%s\"%s\":{\"host\":\"%s\",\"port\":%d,\"source\":\"%s\"}",c?",":"",channels[c],active[c].host,active[c].port,active[c].source);
        if (wrote>0) used+=(size_t)wrote;
    }
    if (used<size) snprintf(out+used,size-used,"}}");
    ReleaseSRWLockShared(&endpoint_lock);
}
void endpoints_shutdown(void) {
    AcquireSRWLockExclusive(&dns_lock); shutting_down=true; ReleaseSRWLockExclusive(&dns_lock);
    /* Static slots remain valid until process exit; no unbounded wait on Win7 DNS. */
}
