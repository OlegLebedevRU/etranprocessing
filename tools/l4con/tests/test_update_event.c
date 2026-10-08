#include "../src/update_event.h"
#include "../src/update_status.h"
#include "../../leo4proxy/src/policy_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static DWORD observed_error;static L4RemoteStatus observed;
bool l4_remote_status_observe(const L4Layout* layout,const wchar_t* operation,L4RemoteStatus* result){
    (void)layout;(void)operation;if(observed_error){SetLastError(observed_error);return false;}*result=observed;return true;
}
int main(void){
    FILETIME now;GetSystemTimeAsFileTime(&now);ULONGLONG ticks=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;
    const char* delivery="22222222-2222-4222-8222-222222222222";
    L4UpdateOutcome value={"135a4120-9ba6-4f6c-8cac-4baf5df8f1df","suite","succeeded","latest","1.13.7","1.13.6",0,ticks,ticks};char output[2048];
    assert(update_event_json(&value,123,delivery,output,sizeof(output)));PolicyJson json;assert(policy_json_parse(&json,output,strlen(output)));
    unsigned long long code=0;assert(policy_json_uint(&json,policy_json_field(&json,0,"200"),&code) && code==76);
    int array=policy_json_field(&json,0,"300"),item=array+1,object=policy_json_field(&json,item,"449");
    assert(json.tokens[array].type=='[' && json.tokens[item].next==json.tokens[array].next && json.tokens[item].next==json.tokens[object].next);
    char operation[40];assert(policy_json_string(&json,policy_json_field(&json,object,"operation_id"),operation,sizeof(operation)) && !strcmp(operation,value.operation_id));
    assert(!update_event_json(&value,123,value.operation_id,output,sizeof(output)) && !output[0]);
    assert(!update_event_json(&value,123,delivery,output,16) && !output[0]);
    value.error=5;assert(!update_event_json(&value,123,delivery,output,sizeof(output)));value.result="failed";value.resolved_version=NULL;
    assert(update_event_json(&value,123,delivery,output,sizeof(output)) && strstr(output,"\"resolved_version\":null"));
    value.requested_version="1.13.7\",\"secret\":\"x";assert(!update_event_json(&value,123,delivery,output,sizeof(output)));
    value.requested_version="latest";value.previous_version="latest";assert(!update_event_json(&value,123,delivery,output,sizeof(output)));
    value.previous_version="1.13.6";value.finished_at=ticks-1;assert(!update_event_json(&value,123,delivery,output,sizeof(output)));
    L4RemoteResult terminal={0};strcpy_s(terminal.operation_id,37,value.operation_id);
    strcpy_s(terminal.requested_version,32,"latest");strcpy_s(terminal.previous_version,32,"1.13.6");
    terminal.target=1;terminal.result=L4_REMOTE_RESULT_FAILED;terminal.error=ERROR_TIMEOUT;terminal.started_at=ticks;terminal.finished_at=ticks;
    assert(update_event_preparation_json(&terminal,123,delivery,output,sizeof(output)) && strstr(output,"\"result\":\"failed\"") && strstr(output,"\"resolved_version\":null"));
    strcpy_s(terminal.resolved_version,32,"1.13.7");terminal.result=L4_REMOTE_RESULT_CANCELLED;terminal.error=ERROR_CANCELLED;
    assert(update_event_preparation_json(&terminal,123,delivery,output,sizeof(output)) && strstr(output,"\"result\":\"cancelled\""));
    assert(!update_event_preparation_json(&terminal,123,terminal.operation_id,output,sizeof(output)) && !output[0]);
    terminal.result=3;assert(!update_event_preparation_json(&terminal,123,delivery,output,sizeof(output)) && !output[0]);
    observed_error=ERROR_ACCESS_DENIED;assert(!update_status_event_json(NULL,L"model",123,delivery,output,sizeof(output)) && !output[0] && GetLastError()==ERROR_ACCESS_DENIED);
    observed_error=0;assert(!update_status_event_json(NULL,L"model",123,delivery,output,sizeof(output)) && !output[0] && GetLastError()==ERROR_IO_PENDING);
    terminal.result=L4_REMOTE_RESULT_CANCELLED;observed.has_result=true;observed.result=terminal;
    assert(update_status_event_json(NULL,L"model",123,delivery,output,sizeof(output)) && strstr(output,"\"449\""));
    assert(!update_status_event_json(NULL,L"model",123,terminal.operation_id,output,sizeof(output)) && !output[0]);
    observed.recorded_phase=L4_REMOTE_RECORDED_FINISHED;strcpy_s(observed.operation_id,37,terminal.operation_id);
    observed.request.target=L4_REMOTE_SUITE;strcpy_s(observed.request.version,32,"latest");observed.plan_sequence=11;
    assert(update_status_rpc_json(&observed,delivery,output,sizeof(output)) && strstr(output,"\"recorded_history\":true") && strstr(output,"\"result\":\"cancelled\""));
    observed.has_launch_failure=true;observed.launch_failure.result=terminal;observed.launch_failure.plan_sequence=11;observed.launch_failure.stage=6;observed.launch_failure.cleanup_error=ERROR_TIMEOUT;
    observed.recorded_phase=L4_REMOTE_RECORDED_RECOVERY_REQUIRED;
    assert(update_status_rpc_json(&observed,delivery,output,sizeof(output)) && strstr(output,"\"result\":\"recovery_required\"") && strstr(output,"\"underlying_result\":\"cancelled\""));
    assert(update_status_event_json(NULL,L"model",123,delivery,output,sizeof(output)) && strstr(output,"\"449\"") && strstr(output,"\"cleanup_error\":1460") && strstr(output,"\"start_stage\":6"));
    assert(!strstr(output,"\"result\":\"succeeded\"") && !strstr(output,"\"result\":\"failed\""));
    observed.launch_failure.stage=9;assert(!update_status_event_json(NULL,L"model",123,delivery,output,sizeof(output)) && !output[0]);
    observed.has_result=false;assert(!update_status_rpc_json(&observed,delivery,output,sizeof(output)) && !output[0]);
    memset(&observed,0,sizeof(observed));strcpy_s(observed.operation_id,37,terminal.operation_id);observed.request.target=L4_REMOTE_SUITE;strcpy_s(observed.request.version,32,"latest");
    observed.plan_sequence=11;observed.recorded_phase=L4_REMOTE_RECORDED_WORKER;observed.has_outcome=true;
    observed.outcome.result=terminal;observed.outcome.result.result=L4_REMOTE_OUTCOME_SUCCESS;observed.outcome.result.error=0;
    observed.outcome.plan_sequence=11;observed.outcome.plan_sha256[0]=1;observed.outcome.proof_sequence=20;observed.outcome.proof_sha256[0]=2;
    assert(!update_status_event_json(NULL,L"model",123,delivery,output,sizeof(output)) && !output[0] && GetLastError()==ERROR_IO_PENDING);
    assert(update_status_rpc_json(&observed,delivery,output,sizeof(output)) && strstr(output,"\"result\":\"pending\"") && strstr(output,"\"completion_result\":\"succeeded\"") && strstr(output,"\"installed_version\":null"));
    observed.outcome_clear_recorded=true;assert(!update_status_event_json(NULL,L"model",123,delivery,output,sizeof(output)) && !output[0]);
    observed.outcome_cleared=true;observed.recorded_phase=L4_REMOTE_RECORDED_FINISHED;
    assert(update_status_event_json(NULL,L"model",123,delivery,output,sizeof(output)) && strstr(output,"\"result\":\"succeeded\"") && strstr(output,"\"installed_version\":\"1.13.7\""));
    assert(update_status_rpc_json(&observed,delivery,output,sizeof(output)) && strstr(output,"\"state_cleared\":true"));
    observed.outcome.result.result=L4_REMOTE_OUTCOME_RESTORED;observed.outcome.result.error=ERROR_TIMEOUT;
    assert(update_status_event_json(NULL,L"model",123,delivery,output,sizeof(output)) && strstr(output,"\"result\":\"restored\"") && strstr(output,"\"installed_version\":\"1.13.6\"") && strstr(output,"\"error\":1460"));
    observed.outcome_cleared=false;assert(!update_status_event_json(NULL,L"model",123,delivery,output,sizeof(output)) && !output[0]);
    observed.outcome.result.result=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED;observed.outcome.proof_sequence=0;memset(observed.outcome.proof_sha256,0,32);observed.outcome_clear_recorded=false;observed.recorded_phase=L4_REMOTE_RECORDED_RECOVERY_REQUIRED;
    assert(update_status_event_json(NULL,L"model",123,delivery,output,sizeof(output)) && strstr(output,"\"result\":\"recovery_required\"") && strstr(output,"\"installed_version\":null"));
    observed.outcome_cleared=true;assert(!update_status_event_json(NULL,L"model",123,delivery,output,sizeof(output)) && !output[0]);observed.outcome_cleared=false;
    observed.has_result=true;assert(!update_status_rpc_json(&observed,delivery,output,sizeof(output)) && !output[0]);
    assert(!update_status_event_json(NULL,L"model",123,delivery,output,sizeof(output)) && !output[0]);
    observed.has_result=false;observed.has_outcome=false;observed.outcome_clear_recorded=true;assert(!update_status_rpc_json(&observed,delivery,output,sizeof(output)) && !output[0]);
    puts("Update event76/tag449: one object, original operation/distinct delivery ID, bounded serialization and no false success PASS");return 0;
}
