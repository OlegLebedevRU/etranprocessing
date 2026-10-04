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
    puts("RPC native consumer: producer fixture / malformed envelope / safe TSK default passed");
    return 0;
}
