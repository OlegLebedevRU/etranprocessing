#include "rpc_contract.h"
#include "mqtt_protocol.h"
#include "../../leo4proxy/src/policy_json.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

bool rpc_uuid(const char* s) {
    if (!s || strlen(s)!=36) return false;
    for (int n=0;n<36;n++) {
        if (n==8 || n==13 || n==18 || n==23) { if (s[n]!='-') return false; }
        else if (!isxdigit((unsigned char)s[n])) return false;
    }
    return true;
}
static bool text(const PolicyJson* j,int object,const char* key,char* out,size_t size) {
    int token=policy_json_field(j,object,key);
    if (token<0) return true;
    if (j->tokens[token].type=='n') return true; /* Nullable optional fields in producer DTO. */
    if (j->tokens[token].type!='"') return false;
    size_t length=(size_t)(j->tokens[token].end-j->tokens[token].start);
    char* wrapped=(char*)malloc(length+8);
    if (!wrapped) return false;
    memcpy(wrapped,"{\"v\":",5);
    memcpy(wrapped+5,j->text+j->tokens[token].start,length);
    wrapped[5+length]='}'; wrapped[6+length]=0;
    bool ok=json_extract_string_strict(wrapped,"v",out,size)==1;
    free(wrapped); return ok;
}
static bool number(const PolicyJson* j,int object,const char* key,int* out,int low,int high) {
    int token=policy_json_field(j,object,key);
    if (token<0) return true;
    unsigned long long value;
    if (!policy_json_uint(j,token,&value) || value<(unsigned)low || value>(unsigned)high) return false;
    *out=(int)value; return true;
}
bool rpc_contract_parse(const char* body,size_t length,bool announcement,
                        const char* wire_method,const char* wire_correlation,
                        const char* wire_payload_required,RpcCommand* out) {
    PolicyJson j; memset(out,0,sizeof(*out)); out->payload_required=true;
    out->ttl_sec=0; out->max_output_bytes=1048576;
    if (!policy_json_parse(&j,body,length)) return false;
    int header=policy_json_field(&j,0,"header");
    if (header>=0 && j.tokens[header].type!='{') return false;
    int source=header>=0?header:0;
    if (!number(&j,source,"method_code",&out->method,0,65534) ||
        !text(&j,0,"id",out->task_id,sizeof(out->task_id))) return false;
    if (wire_method && *wire_method) {
        char* end; unsigned long value=strtoul(wire_method,&end,10);
        if (*end || value>65534 || (out->method && out->method!=(int)value)) return false;
        out->method=(int)value;
    }
    if (wire_correlation && *wire_correlation) {
        if (!rpc_uuid(wire_correlation) || (out->task_id[0] && _stricmp(out->task_id,wire_correlation))) return false;
        strcpy_s(out->task_id,sizeof(out->task_id),wire_correlation);
    }
    if (!rpc_uuid(out->task_id)) return false;
    int flag=policy_json_field(&j,0,"payload_required");
    if (flag>=0 && !policy_json_bool(&j,flag,&out->payload_required)) return false;
    if (wire_payload_required && *wire_payload_required) {
        if (strcmp(wire_payload_required,"0") && strcmp(wire_payload_required,"1")) return false;
        bool required=wire_payload_required[0]=='1';
        if (flag>=0 && required!=out->payload_required) return false;
        out->payload_required=required;
    }
    if (announcement) return true;
    int payload=policy_json_field(&j,0,"payload"), dt=policy_json_field(&j,payload,"dt");
    if (payload<0 || j.tokens[payload].type!='{' || dt!=payload+2 || j.tokens[dt].type!='[' ||
        j.tokens[payload].next!=j.tokens[dt].next) return false;
    out->empty=j.tokens[dt].next==dt+1;
    if (out->empty) return out->method==7002 || out->method==7003 || out->method==7004 || out->method==7005;
    int item=dt+1;
    if (j.tokens[item].type!='{' || j.tokens[item].next!=j.tokens[dt].next) return false;
    if (out->method==7011) {
        for (int key=item+1;key<j.tokens[item].next;key=j.tokens[key+1].next) {
            char name[64];
            if (!policy_json_string(&j,key,name,sizeof(name)) ||
                (strcmp(name,"pin") && strcmp(name,"pin_expires_at") && strcmp(name,"ttl_sec"))) return false;
        }
        int expires=policy_json_field(&j,item,"pin_expires_at");
        out->ttl_sec=120;
        if (!text(&j,item,"pin",out->pin,sizeof(out->pin)) || strlen(out->pin)!=6 ||
            expires<0 || !policy_json_uint(&j,expires,&out->pin_expires_at) ||
            out->pin_expires_at>32503680000ULL ||
            !number(&j,item,"ttl_sec",&out->ttl_sec,120,120)) return false;
        for (int n=0;n<6;n++) if (out->pin[n]<'0' || out->pin[n]>'9') return false;
        strcpy_s(out->session_id,sizeof(out->session_id),out->task_id);
        strcpy_s(out->command_line,sizeof(out->command_line),"l4pin --renew-authenticated --pin-stdin");
        return true;
    }
    if (!text(&j,item,"session_id",out->session_id,sizeof(out->session_id)) || !rpc_uuid(out->session_id)) return false;
    for (int key=item+1;key<j.tokens[item].next;key=j.tokens[key+1].next) {
        char name[128];
        if (!policy_json_string(&j,key,name,sizeof(name))) return false;
        bool allowed=!strcmp(name,"session_id");
        if (out->method==7002) allowed=allowed || !strcmp(name,"reason");
        else if (out->method==7001) allowed=allowed || !strcmp(name,"command_line") ||
            !strcmp(name,"command_id") || !strcmp(name,"shell") || !strcmp(name,"args") ||
            !strcmp(name,"ttl_sec") || !strcmp(name,"max_output_bytes") || !strcmp(name,"topic");
        if (!allowed) return false;
    }
    if (out->method==7002) return true;
    if (out->method!=7001) return false;
    if (!text(&j,item,"command_line",out->command_line,sizeof(out->command_line)) ||
        !text(&j,item,"command_id",out->command_id,sizeof(out->command_id)) ||
        !text(&j,item,"shell",out->shell,sizeof(out->shell)) ||
        !number(&j,item,"ttl_sec",&out->ttl_sec,1,3600) ||
        !number(&j,item,"max_output_bytes",&out->max_output_bytes,1,INT_MAX)) return false;
    return out->command_line[0] || out->command_id[0];
}
