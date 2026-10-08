#include "update_status.h"
#include "rpc_contract.h"
#include <stdio.h>
#include <string.h>
static bool outcome_state(const L4RemoteStatus* status){
    if(!status->has_outcome){if(status->outcome_clear_recorded || status->outcome_cleared){SetLastError(ERROR_INVALID_DATA);return false;}return true;}
    BYTE bytes[L4_REMOTE_OUTCOME_BYTES];
    bool ok=!status->has_result && !status->has_launch_failure && l4_remote_outcome_encode(&status->outcome,bytes) &&
        !strcmp(status->outcome.result.operation_id,status->operation_id) && !strcmp(status->outcome.result.requested_version,status->request.version) &&
        status->outcome.plan_sequence==status->plan_sequence && (!status->outcome_cleared || status->outcome_clear_recorded) &&
        (status->outcome.result.result!=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED || (!status->outcome_clear_recorded && !status->outcome_cleared));
    if(!ok)SetLastError(ERROR_INVALID_DATA);return ok;
}
bool update_status_result_json(const L4RemoteStatus* status,unsigned delivery_id,const char* correlation,char* output,size_t capacity){
    if(output && capacity)output[0]=0;
    if(!status || !output || !capacity){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    if(!outcome_state(status))return false;
    if(status->has_outcome){
        if(status->outcome.result.result!=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED && !status->outcome_cleared){SetLastError(ERROR_IO_PENDING);return false;}
        return update_event_outcome_json(&status->outcome,delivery_id,correlation,output,capacity);
    }
    if(!status->has_result){SetLastError(ERROR_IO_PENDING);return false;}
    return status->has_launch_failure?update_event_launch_failure_json(&status->launch_failure,delivery_id,correlation,output,capacity):
        update_event_preparation_json(&status->result,delivery_id,correlation,output,capacity);
}
bool update_status_event_json(const L4Layout* layout,const wchar_t* operation,unsigned delivery_id,const char* correlation,char* output,size_t capacity){
    if(output && capacity)output[0]=0;
    if(!output || !capacity){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    L4RemoteStatus status;
    if(!l4_remote_status_observe(layout,operation,&status))return false;
    return update_status_result_json(&status,delivery_id,correlation,output,capacity);
}
bool update_status_rpc_json(const L4RemoteStatus* status,const char* task_id,char* output,size_t capacity){
    if(output && capacity)output[0]=0;
    static const char* phases[]={"","request_recorded","controller_recorded","route_recorded","packages_progress","packages_recorded","planning","plan_recorded","finished","worker_starting","handoff_recorded","worker_recorded","recovery_required"};
    if(!status || !output || !capacity || !rpc_uuid(task_id) || !rpc_uuid(status->operation_id) ||
       status->recorded_phase<L4_REMOTE_RECORDED_REQUEST || status->recorded_phase>L4_REMOTE_RECORDED_RECOVERY_REQUIRED ||
       !rpc_update_version(status->request.version) || status->request.target!=L4_REMOTE_SUITE || !outcome_state(status)){SetLastError(ERROR_INVALID_DATA);return false;}
    char resolved[40]="null",previous[40]="null",extra[512]="",installed[40]="null";const char* result="pending",*completion="null";DWORD error=0;
    if(status->resolved_version[0]){if(!rpc_update_version(status->resolved_version) || !strcmp(status->resolved_version,"latest"))return false;
        snprintf(resolved,sizeof(resolved),"\"%s\"",status->resolved_version);}
    if(status->has_host){if(!rpc_update_version(status->host.source_version) || !strcmp(status->host.source_version,"latest"))return false;
        snprintf(previous,sizeof(previous),"\"%s\"",status->host.source_version);}
    if(status->has_result){BYTE bytes[L4_REMOTE_RESULT_BYTES];if(!l4_remote_result_encode(&status->result,bytes))return false;
        result=status->result.result==L4_REMOTE_RESULT_CANCELLED?"cancelled":"failed";error=status->result.error;
        if(status->result.resolved_version[0])snprintf(resolved,sizeof(resolved),"\"%s\"",status->result.resolved_version);}
    if(status->has_launch_failure){BYTE bytes[L4_REMOTE_LAUNCH_FAILURE_BYTES];if(!status->has_result || !l4_remote_launch_failure_encode(&status->launch_failure,bytes))return false;
        snprintf(extra,sizeof(extra),",\"underlying_result\":\"%s\",\"cleanup_error\":%lu,\"start_stage\":%lu",result,status->launch_failure.cleanup_error,status->launch_failure.stage);result="recovery_required";}
    if(status->has_outcome){BYTE bytes[L4_REMOTE_OUTCOME_BYTES];if(status->has_result || status->has_launch_failure || !l4_remote_outcome_encode(&status->outcome,bytes))return false;
        const L4RemoteResult* r=&status->outcome.result;if(strcmp(r->operation_id,status->operation_id)||strcmp(r->requested_version,status->request.version))return false;
        snprintf(resolved,sizeof(resolved),"\"%s\"",r->resolved_version);snprintf(previous,sizeof(previous),"\"%s\"",r->previous_version);error=r->error;
        completion=r->result==L4_REMOTE_OUTCOME_SUCCESS?"\"succeeded\"":r->result==L4_REMOTE_OUTCOME_RESTORED?"\"restored\"":"\"recovery_required\"";
        if(r->result==L4_REMOTE_OUTCOME_RECOVERY_REQUIRED)result="recovery_required";
        else if(status->outcome_cleared){result=r->result==L4_REMOTE_OUTCOME_SUCCESS?"succeeded":"restored";
            snprintf(installed,sizeof(installed),"\"%s\"",r->result==L4_REMOTE_OUTCOME_RESTORED?r->previous_version:r->resolved_version);}
    }
    int n=snprintf(output,capacity,"{\"correlationData\":\"%s\",\"status_code\":200,\"operation_id\":\"%s\",\"target\":\"suite\",\"requested_version\":\"%s\",\"resolved_version\":%s,\"previous_version\":%s,\"installed_version\":%s,\"phase\":\"%s\",\"result\":\"%s\",\"completion_result\":%s,\"completion_recorded\":%s,\"state_clear_recorded\":%s,\"state_cleared\":%s,\"error\":%lu,\"plan_sequence\":%llu,\"recorded_history\":true%s}",
        task_id,status->operation_id,status->request.version,resolved,previous,installed,phases[status->recorded_phase],result,completion,status->has_outcome?"true":"false",status->outcome_clear_recorded?"true":"false",status->outcome_cleared?"true":"false",error,status->plan_sequence,extra);
    if(n<1 || (size_t)n>=capacity){output[0]=0;SetLastError(ERROR_INSUFFICIENT_BUFFER);return false;}return true;
}
