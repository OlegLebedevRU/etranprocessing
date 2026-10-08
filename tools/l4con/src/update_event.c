#include "update_event.h"
#include "rpc_contract.h"
#include <stdio.h>
#include <string.h>
bool update_event_preparation_json(const L4RemoteResult* result,unsigned delivery_id,const char* correlation,char* output,size_t capacity){
    if(output && capacity)output[0]=0;BYTE bytes[L4_REMOTE_RESULT_BYTES];
    if(!l4_remote_result_encode(result,bytes))return false;
    L4UpdateOutcome value={result->operation_id,"suite",result->result==L4_REMOTE_RESULT_CANCELLED?"cancelled":"failed",
        result->requested_version,result->resolved_version[0]?result->resolved_version:NULL,result->previous_version,
        result->error,result->started_at,result->finished_at};
    return update_event_json(&value,delivery_id,correlation,output,capacity);
}
static bool utc(ULONGLONG ticks,char output[24]){
    FILETIME file={(DWORD)ticks,(DWORD)(ticks>>32)};SYSTEMTIME time;
    if(!ticks || !FileTimeToSystemTime(&file,&time) || time.wYear<2000 || time.wYear>9999)return false;
    return snprintf(output,24,"%04u-%02u-%02uT%02u:%02u:%02uZ",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond)==20;
}
static bool exact_version(const char* version){return version && strcmp(version,"latest") && rpc_update_version(version);}
static bool encode_event(const L4UpdateOutcome* value,unsigned delivery_id,const char* correlation,const char* extra,char* output,size_t capacity){
    if(output && capacity)output[0]=0;
    if(!value || !output || !capacity || !delivery_id || delivery_id>0x7fffffffU ||
       !rpc_uuid(correlation) || !rpc_uuid(value->operation_id) || !_stricmp(correlation,value->operation_id) ||
       !strcmp(correlation,"00000000-0000-0000-0000-000000000000") || !strcmp(value->operation_id,"00000000-0000-0000-0000-000000000000") ||
       !value->target || (strcmp(value->target,"suite") && strcmp(value->target,"updater")) ||
       !rpc_update_version(value->requested_version) || !exact_version(value->previous_version) ||
       (value->resolved_version && !exact_version(value->resolved_version)) ||
       !value->result || value->finished_at<value->started_at)return false;
    const char* results[]={"succeeded","restored","failed","cancelled","partially_applied","connectivity_unconfirmed","recovery_required"};bool known=false;
    for(unsigned i=0;i<sizeof(results)/sizeof(results[0]);++i)if(!strcmp(value->result,results[i]))known=true;
    if(!known || (!strcmp(value->result,"succeeded") && (value->error || !value->resolved_version)) ||
       (!strcmp(value->result,"restored") && (!value->error || !value->resolved_version)))return false;
    char started[24],finished[24],resolved[40];
    if(!utc(value->started_at,started) || !utc(value->finished_at,finished))return false;
    if(value->resolved_version)snprintf(resolved,sizeof(resolved),"\"%s\"",value->resolved_version);else strcpy_s(resolved,sizeof(resolved),"null");
    int length=snprintf(output,capacity,"{\"101\":%u,\"102\":\"%s\",\"200\":76,\"300\":[{\"449\":{\"v\":1,\"operation_id\":\"%s\",\"target\":\"%s\",\"phase\":\"finished\",\"result\":\"%s\",\"requested_version\":\"%s\",\"resolved_version\":%s,\"previous_version\":\"%s\",\"error\":%lu,\"started_at\":\"%s\",\"finished_at\":\"%s\"%s}}],\"correlationData\":\"%s\"}",
        delivery_id,finished,value->operation_id,value->target,value->result,value->requested_version,resolved,value->previous_version,value->error,started,finished,extra,correlation);
    if(length<1 || (size_t)length>=capacity){output[0]=0;return false;}return true;
}
bool update_event_json(const L4UpdateOutcome* value,unsigned delivery_id,const char* correlation,char* output,size_t capacity){
    return encode_event(value,delivery_id,correlation,"",output,capacity);
}
bool update_event_launch_failure_json(const L4RemoteLaunchFailure* failure,unsigned delivery_id,const char* correlation,char* output,size_t capacity){
    if(output && capacity)output[0]=0;BYTE bytes[L4_REMOTE_LAUNCH_FAILURE_BYTES];
    if(!l4_remote_launch_failure_encode(failure,bytes))return false;
    const L4RemoteResult* r=&failure->result;
    L4UpdateOutcome value={r->operation_id,"suite","recovery_required",r->requested_version,r->resolved_version[0]?r->resolved_version:NULL,r->previous_version,r->error,r->started_at,r->finished_at};
    char extra[256];snprintf(extra,sizeof(extra),",\"underlying_result\":\"%s\",\"cleanup_error\":%lu,\"start_stage\":%lu,\"plan_sequence\":%llu",
        r->result==L4_REMOTE_RESULT_CANCELLED?"cancelled":"failed",failure->cleanup_error,failure->stage,failure->plan_sequence);
    return encode_event(&value,delivery_id,correlation,extra,output,capacity);
}
bool update_event_outcome_json(const L4RemoteOutcome* outcome,unsigned delivery_id,const char* correlation,char* output,size_t capacity){
    if(output && capacity)output[0]=0;BYTE bytes[L4_REMOTE_OUTCOME_BYTES];
    if(!l4_remote_outcome_encode(outcome,bytes))return false;
    const L4RemoteResult* r=&outcome->result;
    const char* result=r->result==L4_REMOTE_OUTCOME_SUCCESS?"succeeded":r->result==L4_REMOTE_OUTCOME_RESTORED?"restored":"recovery_required";
    L4UpdateOutcome value={r->operation_id,"suite",result,r->requested_version,r->resolved_version,r->previous_version,r->error,r->started_at,r->finished_at};
    char installed[40]="null",extra[256];
    if(r->result!=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED)snprintf(installed,sizeof(installed),"\"%s\"",r->result==L4_REMOTE_OUTCOME_RESTORED?r->previous_version:r->resolved_version);
    snprintf(extra,sizeof(extra),",\"installed_version\":%s,\"plan_sequence\":%llu",installed,outcome->plan_sequence);
    return encode_event(&value,delivery_id,correlation,extra,output,capacity);
}
