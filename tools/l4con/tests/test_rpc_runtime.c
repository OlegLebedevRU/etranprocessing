/* Isolated consumer state; never connects to the production broker or services. */
#include "../src/mqtt_client.c"
#include <assert.h>
static void deliver(MqttClientState* state,AppConfig* config,const char* topic,const char* body) {
    MqttRpcMetadata metadata={0};
    handle_incoming_publish(state,topic,body,strlen(body),config,&metadata);
}
int main(void) {
    WSADATA wsa;assert(!WSAStartup(MAKEWORD(2,2),&wsa));
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(listener!=INVALID_SOCKET);
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(!bind(listener,(const struct sockaddr*)&address,sizeof(address)) && !listen(listener,1));
    int address_size=sizeof(address);assert(!getsockname(listener,(struct sockaddr*)&address,&address_size));
    SOCKET client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(client!=INVALID_SOCKET);
    assert(socket_connect_bounded(client,(const struct sockaddr*)&address,sizeof(address),NULL));
    SOCKET peer=accept(listener,NULL,NULL);assert(peer!=INVALID_SOCKET);assert(send(peer,"x",1,0)==1);
    unsigned char bytes[2];ULONGLONG started=GetTickCount64();
    assert(!socket_recv_all(client,bytes,2,started+100) && GetTickCount64()-started<1500);
    closesocket(peer);closesocket(client);closesocket(listener);WSACleanup();
    MqttClientState state={0};state.sock=INVALID_SOCKET;
    strcpy_s(state.sn,sizeof(state.sn),"fixture");InitializeCriticalSection(&state.send_cs);
    AppConfig config={0};config.default_cmd_timeout=30;
    command_runner_init_context(&state.current_cmd);state.current_cmd.is_running=true;
    strcpy_s(state.current_cmd.session_id,sizeof(state.current_cmd.session_id),"ea9c98c0-bcd2-4e87-9ab5-2df74eab41e7");
    const char* wrong="{\"id\":\"135a4120-9ba6-4f6c-8cac-4baf5df8f1df\",\"header\":{\"method_code\":7002},\"payload\":{\"dt\":[{\"session_id\":\"f4106499-d645-4e31-8913-c2b73f41937a\"}]}}";
    deliver(&state,&config,"srv/fixture/rsp",wrong);
    assert(!state.current_cmd.cancel_requested);
    const char* tsk="{\"id\":\"bc57f0f1-a588-4837-8188-02f007cfb0e4\",\"header\":{\"method_code\":7002},\"payload_required\":false}";
    deliver(&state,&config,"srv/fixture/tsk",tsk);assert(state.current_cmd.cancel_requested);
    state.current_cmd.cancel_requested=false;
    deliver(&state,&config,"srv/fixture/tsk",tsk);assert(!state.current_cmd.cancel_requested);
    const char* empty="{\"id\":\"bc57f0f1-a588-4837-8188-02f007cfb0e4\",\"header\":{\"method_code\":7002},\"payload\":{\"dt\":[]}}";
    deliver(&state,&config,"srv/fixture/rsp",empty);assert(!state.current_cmd.cancel_requested);
    state.current_cmd.is_running=false;
    const char* exec="{\"id\":\"f4106499-d645-4e31-8913-c2b73f41937a\",\"header\":{\"method_code\":7001},\"payload\":{\"dt\":[{\"session_id\":\"ea9c98c0-bcd2-4e87-9ab5-2df74eab41e7\",\"command_line\":\"echo fixture\",\"ttl_sec\":1}]}}";
    deliver(&state,&config,"srv/fixture/tsk",exec);assert(!state.hWorkerThread);
    deliver(&state,&config,"srv/fixture/rsp",exec);assert(state.hWorkerThread);
    assert(WaitForSingleObject(state.hWorkerThread,5000)==WAIT_OBJECT_0);
    HANDLE original=state.hWorkerThread;
    deliver(&state,&config,"srv/fixture/rsp",exec);assert(state.hWorkerThread==original);
    int n=rpc_recent(&state,"f4106499-d645-4e31-8913-c2b73f41937a",false);
    assert(n>=0 && state.recent[n].status==200 && rpc_uuid(state.recent[n].result_uid));
    CloseHandle(state.hWorkerThread);state.hWorkerThread=NULL;
    CommandContext ctx;command_runner_init_context(&ctx);
    strcpy_s(ctx.command_line,sizeof(ctx.command_line),"ping -n 6 127.0.0.1 >nul");
    strcpy_s(ctx.session_id,sizeof(ctx.session_id),"ea9c98c0-bcd2-4e87-9ab5-2df74eab41e7");
    ctx.ttl_sec=1;int code;uint64_t duration;
    command_runner_execute(&ctx,NULL,NULL,&code,&duration);
    assert(code==124 && duration<4000);
    state.hWorkerThread=CreateEventW(NULL,TRUE,FALSE,NULL);
    state.current_cmd.is_running=true;state.current_cmd.cancel_requested=false;
    InterlockedExchange(&state.current_cmd.protected_renewal,1);
    const char* blocked="{\"id\":\"013a4120-9ba6-4f6c-8cac-4baf5df8f1df\",\"header\":{\"method_code\":7001},\"payload\":{\"dt\":[{\"session_id\":\"ea9c98c0-bcd2-4e87-9ab5-2df74eab41e7\",\"command_line\":\"echo blocked\"}]}}";
    deliver(&state,&config,"srv/fixture/rsp",blocked);
    assert(!state.current_cmd.cancel_requested);
    n=rpc_recent(&state,"013a4120-9ba6-4f6c-8cac-4baf5df8f1df",false);assert(n>=0 && state.recent[n].status==409);
    assert(!event_job_cancel_replacement(&state.current_cmd.cancel_requested,&state.current_cmd.protected_renewal));
    assert(!state.current_cmd.cancel_requested);
    InterlockedExchange(&state.current_cmd.protected_renewal,0);
    assert(event_job_cancel_replacement(&state.current_cmd.cancel_requested,&state.current_cmd.protected_renewal));
    assert(state.current_cmd.cancel_requested);
    CloseHandle(state.hWorkerThread);
    DeleteCriticalSection(&state.send_cs);
    puts("RPC runtime: addressed cancel / TSK-RSP duplicate / result status / bounded TTL passed");
    return 0;
}
