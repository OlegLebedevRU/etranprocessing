#include "../src/rpc_contract.h"
#include "../src/mqtt_protocol.h"
#include "../../leo4proxy/src/policy_json.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
static bool parse(const char* s,bool tsk,RpcCommand* out) {
    return rpc_contract_parse(s,strlen(s),tsk,"","","",out);
}
int main(int argc,char** argv) {
    assert(argc==2); FILE* f=NULL;assert(fopen_s(&f,argv[1],"rb")==0);
    char fixture[8192];size_t length=fread(fixture,1,sizeof(fixture)-1,f);fclose(f);fixture[length]=0;
    PolicyJson j;assert(policy_json_parse(&j,fixture,length));RpcCommand command;
    const char* names[]={"tsk_empty_cancel","rsp_empty_cancel","rsp_exec"};
    for (int n=0;n<3;n++) {
        int token=policy_json_field(&j,0,names[n]);assert(token>=0);
        assert(rpc_contract_parse(fixture+j.tokens[token].start,(size_t)(j.tokens[token].end-j.tokens[token].start),
            n==0,"","","",&command));
        assert(command.method==(n==2?7001:7002));
        if (n==0) assert(!command.payload_required);
        if (n==2) assert(!strcmp(command.command_line,"echo fixture") && command.ttl_sec==30);
    }
    const char* bad[]={
        "{\"id\":\"135a4120-9ba6-4f6c-8cac-4baf5df8f1df\",\"header\":{\"method_code\":7001},\"payload\":{\"dt\":[]}}",
        "{\"id\":\"135a4120-9ba6-4f6c-8cac-4baf5df8f1df\",\"header\":{\"method_code\":7002},\"payload\":{\"dt\":{},\"command_line\":\"echo 7001\"}}",
        "{\"id\":\"135a4120-9ba6-4f6c-8cac-4baf5df8f1df\",\"header\":{\"method_code\":7002},\"payload\":{\"x\":1,\"dt\":[]}}",
        "{\"id\":\"135a4120-9ba6-4f6c-8cac-4baf5df8f1df\",\"header\":{\"method_code\":7002,\"method_code\":7001},\"payload\":{\"dt\":[]}}",
        "{\"id\":\"not-a-task-id\",\"header\":{\"method_code\":7002},\"payload\":{\"dt\":[]}}"
    };
    for(size_t n=0;n<sizeof(bad)/sizeof(bad[0]);n++) assert(!parse(bad[n],false,&command));
    const char* announcement="{\"id\":\"135a4120-9ba6-4f6c-8cac-4baf5df8f1df\",\"header\":{\"method_code\":7002}}";
    assert(parse(announcement,true,&command) && command.payload_required);
    assert(!rpc_contract_parse(announcement,strlen(announcement),true,"7001","","",&command));
    f=NULL;assert(fopen_s(&f,"../../docs/contracts/rpc7011-gate4.json","rb")==0);
    length=fread(fixture,1,sizeof(fixture)-1,f);fclose(f);fixture[length]=0;
    assert(policy_json_parse(&j,fixture,length));
    int token=policy_json_field(&j,0,"rsp");assert(token>=0);
    assert(rpc_contract_parse(fixture+j.tokens[token].start,(size_t)(j.tokens[token].end-j.tokens[token].start),false,"","","",&command));
    assert(command.method==7011 && command.ttl_sec==120 && !strcmp(command.pin,"000000") && command.pin_expires_at==4102444800ULL);
    assert(!strcmp(command.session_id,command.task_id));

    const char* fm_start="{\"id\":\"135a4120-9ba6-4f6c-8cac-4baf5df8f1df\",\"header\":{\"method_code\":7023},\"payload\":{\"dt\":[{\"session_id\":\"135a4120-9ba6-4f6c-8cac-4baf5df8f1df\",\"action\":\"start\",\"expires_at\":4102444800,\"ttl_sec\":60}]}}";
    assert(parse(fm_start,false,&command) && command.method==7023 && !strcmp(command.fm_action,"start"));
    const char* fm_transfer="{\"id\":\"135a4120-9ba6-4f6c-8cac-4baf5df8f1df\",\"header\":{\"method_code\":7021},\"payload\":{\"dt\":[{\"session_id\":\"135a4120-9ba6-4f6c-8cac-4baf5df8f1df\",\"action\":\"transfer\",\"operation_id\":\"22222222-2222-4222-8222-222222222222\",\"expires_at\":4102444800,\"ttl_sec\":60}]}}";
    assert(parse(fm_transfer,false,&command) && !strcmp(command.fm_operation_id,"22222222-2222-4222-8222-222222222222"));
    char invalid_fm[2048];strcpy_s(invalid_fm,sizeof(invalid_fm),fm_transfer);
    char* action=strstr(invalid_fm,"transfer");assert(action);memcpy(action,"unknown!",8);
    assert(!parse(invalid_fm,false,&command));
    char update[1024];
    const char* task="135a4120-9ba6-4f6c-8cac-4baf5df8f1df";
    const char* operation="22222222-2222-4222-8222-222222222222";
    for(int method=7030;method<=7033;++method){
        snprintf(update,sizeof(update),"{\"id\":\"%s\",\"header\":{\"method_code\":%d},\"payload\":{\"dt\":[{%s}]}}",task,method,
            method<=7031?"\"version\":\"1.13.2\",\"target\":\"suite\"":"\"operation_id\":\"22222222-2222-4222-8222-222222222222\"");
        assert(parse(update,false,&command));
        if(method<=7031)assert(!strcmp(command.update_version,"1.13.2") && !strcmp(command.update_target,"suite"));
        if(method==7031)assert(!strcmp(command.update_operation_id,task));
        if(method>=7032)assert(!strcmp(command.update_operation_id,operation) && strcmp(command.update_operation_id,command.task_id));
    }
    const char* fields[]={"\"version\":\"latest\"","\"version\":\"1.13.2\",\"target\":\"updater\"",
        "\"version\":\"../1.13.2\"","\"version\":\"01.13.2\"","\"version\":\"1.13.2-beta\"","\"version\":null",
        "\"version\":\"1.13.2\",\"target\":null","\"version\":\"1.13.2\",\"url\":\"https://example.invalid\"",
        "\"version\":\"1.13.2\",\"operation_id\":\"22222222-2222-4222-8222-222222222222\"","\"version\":\"1.13.2\",\"version\":\"latest\""};
    for(unsigned k=0;k<sizeof(fields)/sizeof(fields[0]);++k){
        snprintf(update,sizeof(update),"{\"id\":\"%s\",\"header\":{\"method_code\":7031},\"payload\":{\"dt\":[{%s}]}}",task,fields[k]);
        assert(parse(update,false,&command)==(k<2));
    }
    puts("Update RPC7030-7033: strict version/target, original task identity, no URL/session/operation override PASS");
    assert(!parse("{\"id\":\"00000000-0000-0000-0000-000000000000\",\"header\":{\"method_code\":7031},\"payload\":{\"dt\":[{\"version\":\"latest\"}]}}",false,&command));
    assert(!parse("{\"id\":\"135a4120-9ba6-4f6c-8cac-4baf5df8f1df\",\"header\":{\"method_code\":7032},\"payload\":{\"dt\":[{\"operation_id\":\"00000000-0000-0000-0000-000000000000\"}]}}",false,&command));
    strcpy_s(invalid_fm,sizeof(invalid_fm),fm_transfer);
    char* method=strstr(invalid_fm,"7021");assert(method);memcpy(method,"7020",4);
    action=strstr(invalid_fm,"transfer");assert(action);memmove(action+4,action+8,strlen(action+8)+1);memcpy(action,"list",4);
    assert(!parse(invalid_fm,false,&command));
    strcpy_s(invalid_fm,sizeof(invalid_fm),fm_start);
    action=strstr(invalid_fm,"start");assert(action);memmove(action+4,action+5,strlen(action+5)+1);memcpy(action,"stop",4);
    assert(!parse(invalid_fm,false,&command));
    puts("RPC native consumer: producer fixture / malformed envelope / safe TSK default passed");
    return 0;
}
