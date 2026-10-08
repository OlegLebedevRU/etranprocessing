#include <windows.h>
#include <assert.h>
#include <stdio.h>
static ULONGLONG clock_utc=200;
static void evidence_time(FILETIME* t){t->dwLowDateTime=(DWORD)clock_utc;t->dwHighDateTime=(DWORD)(clock_utc>>32);}
#define GetSystemTimeAsFileTime evidence_time
#include "../src/link_probe.c"
#undef GetSystemTimeAsFileTime
int main(void){
    const char* req="11111111-1111-4111-8111-111111111111",*evt="22222222-2222-4222-8222-222222222222";
    LinkProbe p={0};L4LinkProbeEvidence e;assert(link_probe_begin(&p,1,1000));assert(!link_probe_evidence(&p,&e));
    strcpy_s(p.request_nonce,40,req);strcpy_s(p.evidence.request_nonce,40,req);p.evidence.req_sent_utc=100;p.stage=LINK_WAIT_RSP;
    MqttRpcMetadata m={0};strcpy_s(m.iot_probe,sizeof(m.iot_probe),"1");strcpy_s(m.method_code,sizeof(m.method_code),"0");strcpy_s(m.correlation,40,req);
    const char* rsp="{\"v\":1,\"type\":\"channel_probe\",\"status\":\"success\"}";
    m.correlation[0]='9';link_probe_rsp(&p,2,&m,rsp,strlen(rsp));assert(!p.evidence.rsp_received_utc);m.correlation[0]='1';
    link_probe_rsp(&p,2,&m,rsp,strlen(rsp));assert(p.stage==LINK_NEED_EVENT && p.evidence.rsp_received_utc==200);
    p.stage=LINK_WAIT_EVA;p.event_id=p.evidence.event_id=123;p.evidence.evt_sent_utc=300;
    strcpy_s(p.correlation,40,evt);strcpy_s(p.evidence.event_nonce,40,evt);strcpy_s(m.correlation,40,evt);
    strcpy_s(m.event_type_code,sizeof(m.event_type_code),"0");strcpy_s(m.dev_event_id,sizeof(m.dev_event_id),"123");clock_utc=400;
    const char* eva="{\"v\":1,\"type\":\"channel_probe\",\"status\":\"success\",\"request_nonce\":\"11111111-1111-4111-8111-111111111111\"}";
    m.dev_event_id[0]='9';link_probe_eva(&p,3,&m,eva,strlen(eva));assert(!p.evidence.eva_received_utc);m.dev_event_id[0]='1';
    link_probe_eva(&p,3,&m,eva,strlen(eva));assert(link_probe_evidence(&p,&e));assert(e.echo_event_id==123 && !strcmp(e.eva_request_nonce,req));
    BYTE raw[L4_LINK_EVIDENCE_BYTES];L4LinkProbeEvidence decoded;assert(l4_link_evidence_encode(&e,raw));assert(l4_link_evidence_decode(raw,&decoded));assert(!memcmp(&e,&decoded,sizeof(e)));
    for(unsigned i=0;i<L4_LINK_EVIDENCE_BYTES;i++){if(i>=204 && i<236)continue;BYTE saved=raw[i];raw[i]^=0x80;assert(!l4_link_evidence_decode(raw,&decoded));raw[i]=saved;}
    L4LinkProbeEvidence bad=e;bad.req_sent_utc=bad.rsp_received_utc;assert(!l4_link_evidence_valid(&bad));bad=e;bad.eva_received_utc=bad.req_sent_utc+3000000001ull;assert(!l4_link_evidence_valid(&bad));
    bad=e;bad.echo_event_id++;assert(!l4_link_evidence_valid(&bad));p.stage=LINK_FAILED;assert(!link_probe_evidence(&p,&decoded));
    p.stage=LINK_DONE;p.evidence.req_sent_utc=0;assert(!link_probe_evidence(&p,&decoded));assert(!decoded.req_sent_utc);
    puts("Fresh link evidence: actual strict matched parser, chronology, completeness, nonce/ID echo and codec refusal PASS");return 0;
}
