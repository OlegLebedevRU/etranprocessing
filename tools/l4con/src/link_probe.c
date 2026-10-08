#include "link_probe.h"
#include "../../leo4proxy/src/policy_json.h"
#include <string.h>
#include <stdio.h>
#include <objbase.h>
#include <stdlib.h>
static ULONGLONG stamp(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
bool link_probe_evidence(const LinkProbe* p,L4LinkProbeEvidence* out){
    if(!out)return false;memset(out,0,sizeof(*out));if(!p || p->stage!=LINK_DONE || !l4_link_evidence_valid(&p->evidence))return false;
    *out=p->evidence;return true;
}
bool link_probe_begin(LinkProbe* p,ULONGLONG now,DWORD timeout){
    if(!timeout || timeout>300000 || (p->stage!=LINK_IDLE && p->stage!=LINK_DONE && p->stage!=LINK_FAILED))return false;
    GUID id;if(FAILED(CoCreateGuid(&id)))return false;
    unsigned generation=p->generation+1;memset(p,0,sizeof(*p));p->generation=generation;p->stage=LINK_QUEUED;p->deadline=now+timeout;
    snprintf(p->request_nonce,sizeof(p->request_nonce),"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        id.Data1,id.Data2,id.Data3,id.Data4[0],id.Data4[1],id.Data4[2],id.Data4[3],id.Data4[4],id.Data4[5],id.Data4[6],id.Data4[7]);return true;
}
static bool reply(const char* body,size_t size,const char* nonce,char status[32]){
    PolicyJson j;unsigned long long version=0;char type[32],received[40];unsigned fields=0;
    if(size>512 || !policy_json_parse(&j,body,size) || j.tokens[0].type!='{' ||
       !policy_json_uint(&j,policy_json_field(&j,0,"v"),&version) || version!=1 ||
       !policy_json_string(&j,policy_json_field(&j,0,"type"),type,sizeof(type)) || strcmp(type,"channel_probe") ||
       !policy_json_string(&j,policy_json_field(&j,0,"status"),status,32))return false;
    for(int key=1;key<j.tokens[0].next;key=j.tokens[key+1].next){char name[32];++fields;
        if(!policy_json_string(&j,key,name,sizeof(name)) || (strcmp(name,"v") && strcmp(name,"type") && strcmp(name,"status") && (!nonce || strcmp(name,"request_nonce"))))return false;}
    return fields==(nonce?4u:3u) && (!nonce || (policy_json_string(&j,policy_json_field(&j,0,"request_nonce"),received,sizeof(received)) && !_stricmp(nonce,received)));
}
void link_probe_rsp(LinkProbe* p,ULONGLONG now,const MqttRpcMetadata* m,const char* body,size_t size){
    char status[32];
    if(p->stage!=LINK_WAIT_RSP || now>=p->deadline || strcmp(m->iot_probe,"1") || strcmp(m->method_code,"0") ||
       _stricmp(p->request_nonce,m->correlation) || !reply(body,size,NULL,status))return;
    if(!strcmp(status,"success")){p->stage=LINK_NEED_EVENT;strcpy_s(p->evidence.rsp_correlation,40,m->correlation);p->evidence.rsp_received_utc=stamp();}
    else if(!strcmp(status,"error")){p->stage=LINK_FAILED;p->error=ERROR_BAD_NET_RESP;}
}
void link_probe_eva(LinkProbe* p,ULONGLONG now,const MqttRpcMetadata* m,const char* body,size_t size){
    if(p->stage!=LINK_WAIT_EVA || now>=p->deadline || strcmp(m->iot_probe,"1") || _stricmp(p->correlation,m->correlation) || strcmp(m->event_type_code,"0"))return;
    char expected[16];snprintf(expected,sizeof(expected),"%u",p->event_id);if(strcmp(expected,m->dev_event_id))return;
    char status[32];if(!reply(body,size,p->request_nonce,status))return;
    if(!strcmp(status,"success")){PolicyJson j;if(!policy_json_parse(&j,body,size) || !policy_json_string(&j,policy_json_field(&j,0,"request_nonce"),p->evidence.eva_request_nonce,40))return;
        p->stage=LINK_DONE;strcpy_s(p->evidence.eva_correlation,40,m->correlation);p->evidence.echo_event_id=(DWORD)strtoul(m->dev_event_id,NULL,10);p->evidence.eva_received_utc=stamp();}
    else if(!strcmp(status,"error")){p->stage=LINK_FAILED;p->error=ERROR_BAD_NET_RESP;}
}
