/* Isolated consumer state; never connects to the production broker or services. */
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include "../src/update_reporting.h"
#include "../src/update_admission.h"
static unsigned fixture_status_queued;
static bool fixture_status_queue(L4UpdateReporting* reporting,const char* task,const char* operation){
    assert(reporting);assert(!strcmp(task,"34106499-d645-4e31-8913-c2b73f41937a"));assert(!strcmp(operation,"135a4120-9ba6-4f6c-8cac-4baf5df8f1df"));++fixture_status_queued;return true;
}
#define update_reporting_status fixture_status_queue
static bool fixture_admission_active;
static bool fixture_admission_busy(L4UpdateAdmission* admission){(void)admission;return fixture_admission_active;}
#define update_admission_busy fixture_admission_busy
static bool fixture_fm_active;
static bool fixture_fm_busy(void){return fixture_fm_active;}
#define fm_busy fixture_fm_busy
#include "../src/mqtt_client.c"
#undef update_reporting_status
#undef update_admission_busy
#undef fm_busy
#include "../../l4common/tests/update_state_fixture.h"
#include <assert.h>
#include "../../l4common/layout.h"
static void deliver(MqttClientState* state,AppConfig* config,const char* topic,const char* body) {
    MqttRpcMetadata metadata={0};
    handle_incoming_publish(state,topic,body,strlen(body),config,&metadata);
}
static bool packet_has(const unsigned char* packet,size_t size,const char* needle){
    size_t length=strlen(needle);for(size_t i=0;i+length<=size;i++)if(!memcmp(packet+i,needle,length))return true;return false;
}
static size_t read_publish(SOCKET peer,unsigned char packet[2048]){
    unsigned char header,encoded[4];ULONGLONG deadline=GetTickCount64()+2000;
    assert(socket_recv_all(peer,&header,1,deadline) && header==0x32);
    int count=0;do{assert(count<4 && socket_recv_all(peer,encoded+count,1,deadline));}while(encoded[count++]&0x80);
    uint32_t remaining=0;int used=0;assert(!mqtt_decode_remaining_length(encoded,count,&remaining,&used) && used==count && remaining<2048);
    assert(socket_recv_all(peer,packet,remaining,deadline));return remaining;
}
static void test_drain(MqttClientState* state,UpdateFixture* fixture){
    L4UpdateState expected;assert(update_fixture_live(fixture,&expected));
    L4ProbeServer* server=NULL;assert(l4_probe_server_start_ex(L"con",probe_local,drain_local,state,&server));
    assert(l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,1000));
    fixture_admission_active=true;
    assert(probe_local(0,40,NULL,state)==ERROR_BUSY);
    assert(!l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,40));
    fixture_admission_active=false;
    assert(l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,1000));
    state->current_cmd.is_running=true;
    assert(!l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,40));
    state->current_cmd.is_running=false;fixture_fm_active=true;
    assert(!l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,40));fixture_fm_active=false;
    state->hWorkerThread=CreateEventW(NULL,TRUE,FALSE,NULL);assert(state->hWorkerThread);
    assert(!l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,40)); /* Final publication/cleanup. */
    SetEvent(state->hWorkerThread);assert(l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,1000));
    CloseHandle(state->hWorkerThread);state->hWorkerThread=NULL;
    expected.generation++;assert(!l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,1000));assert(GetLastError()==ERROR_REVISION_MISMATCH);expected.generation--;
    assert(update_fixture_put(fixture,2));assert(!l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,1000));
    assert(update_fixture_live(fixture,&expected));
    AcquireSRWLockShared(&state->update.admission);
    assert(!l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,40)); /* Earlier dispatch cannot be overtaken. */
    ReleaseSRWLockShared(&state->update.admission);
    assert(l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,1000));
    assert(update_fixture_put(fixture,0));assert(!l4_probe_drain_call(L"con",GetCurrentProcessId(),&expected,1000));
    l4_probe_server_stop(server);
    puts("Con drain: actual private IPC, command/FM/final cleanup, dispatch fence, stale identity/window and clear refusal PASS");
}
int main(void) {
    L4Layout layout; wchar_t exe[MAX_PATH], work[MAX_PATH], tools[MAX_PATH], expected[MAX_PATH];
    assert(l4_layout_resolve(&layout,L"1.13.2"));
    assert(l4_layout_component(&layout,L"l4con",L"l4con.exe",exe));
    assert(command_runner_paths(exe,work,tools));
    swprintf_s(expected,MAX_PATH,L"%ls\\l4con\\work",layout.state);
    assert(!wcscmp(work,expected) && !wcscmp(tools,layout.launchers));
    assert(command_runner_paths(L"D:\\dev\\tools\\l4con\\bin\\x64\\l4con.exe",work,tools));
    assert(!wcscmp(work,L"D:\\dev\\tools") && !wcscmp(work,tools));
    assert(!command_runner_paths(L"D:\\dev\\other\\l4con.exe",work,tools));
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
    strcpy_s(state.sn,sizeof(state.sn),"fixture");InitializeCriticalSection(&state.send_cs);InitializeCriticalSection(&state.probe_cs);
    AppConfig config={0};config.default_cmd_timeout=30;
    const char* update_start="{\"id\":\"45906499-d645-4e31-8913-c2b73f41937a\",\"header\":{\"method_code\":7031},\"payload\":{\"dt\":[{\"version\":\"latest\",\"target\":\"suite\"}]}}";
    assert(!update_admission_enabled());deliver(&state,&config,"srv/fixture/rsp",update_start);
    int admission_result=rpc_recent(&state,"45906499-d645-4e31-8913-c2b73f41937a",false);
    assert(admission_result>=0 && state.recent[admission_result].status==501 && strstr(state.recent[admission_result].result,"controller_engine_unavailable"));
    state.reporting=(L4UpdateReporting*)(ULONG_PTR)1;
    const char* update_status="{\"id\":\"34106499-d645-4e31-8913-c2b73f41937a\",\"header\":{\"method_code\":7032},\"payload\":{\"dt\":[{\"operation_id\":\"135a4120-9ba6-4f6c-8cac-4baf5df8f1df\"}]}}";
    deliver(&state,&config,"srv/fixture/rsp",update_status);assert(fixture_status_queued==1);
    deliver(&state,&config,"srv/fixture/rsp",update_status);assert(fixture_status_queued==1); /* RPC delivery replay cannot enqueue twice. */
    state.reporting=NULL;
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
    CloseHandle(state.hWorkerThread);state.hWorkerThread=NULL;
    state.current_cmd.is_running=false;
    assert(!probe_work_busy(&state,false));
    assert(probe_work_busy(&state,true)); /* Queued/processing writes or FM lease. */
    state.hWorkerThread=CreateEventW(NULL,TRUE,FALSE,NULL);assert(state.hWorkerThread);
    assert(probe_work_busy(&state,false)); /* Worker may still emit its result. */
    SetEvent(state.hWorkerThread);assert(!probe_work_busy(&state,false));
    CloseHandle(state.hWorkerThread);state.hWorkerThread=NULL;
    state.current_cmd.is_running=true;state.probe_ready=7;
    assert(link_probe_begin(&state.probe,GetTickCount64(),1000));
    probe_tick(&state,&config);assert(state.probe.stage==LINK_FAILED && state.probe.error==ERROR_BUSY);
    state.current_cmd.is_running=false;fixture_fm_active=true;
    assert(link_probe_begin(&state.probe,GetTickCount64(),1000));
    probe_tick(&state,&config);assert(state.probe.stage==LINK_FAILED && state.probe.error==ERROR_BUSY);
    assert(link_probe_begin(&state.probe,GetTickCount64(),1000));state.probe.stage=LINK_NEED_EVENT;
    probe_tick(&state,&config);assert(state.probe.stage==LINK_FAILED && state.probe.error==ERROR_BUSY);
    fixture_fm_active=false;
    puts("Link barrier: command, file-manager and worker cleanup busy gates PASS");
    /* Exact incoming RSP/EVA producer contract; stale/error/foreign cannot pass. */
    state.current_cmd.is_running=false;state.probe_ready=7;
    assert(link_probe_begin(&state.probe,GetTickCount64(),1000));strcpy_s(state.probe.request_nonce,40,"22222222-2222-4222-8222-222222222222");state.probe.stage=LINK_WAIT_RSP;
    MqttRpcMetadata meta={0};strcpy_s(meta.method_code,sizeof(meta.method_code),"0");strcpy_s(meta.correlation,sizeof(meta.correlation),"00000000-0000-0000-0000-000000000000");
    const char* nop="{\"method_code\":0}";
    const char* probe_rsp="{\"v\":1,\"type\":\"channel_probe\",\"status\":\"success\"}";
    handle_incoming_publish(&state,"srv/fixture/rsp",nop,strlen(nop),&config,&meta);assert(state.probe.stage==LINK_WAIT_RSP);
    strcpy_s(meta.iot_probe,sizeof(meta.iot_probe),"1");strcpy_s(meta.correlation,sizeof(meta.correlation),state.probe.request_nonce);
    meta.correlation[0]='9';handle_incoming_publish(&state,"srv/fixture/rsp",probe_rsp,strlen(probe_rsp),&config,&meta);assert(state.probe.stage==LINK_WAIT_RSP);
    meta.correlation[0]='2';meta.iot_probe[0]=0;handle_incoming_publish(&state,"srv/fixture/rsp",probe_rsp,strlen(probe_rsp),&config,&meta);assert(state.probe.stage==LINK_WAIT_RSP);
    strcpy_s(meta.iot_probe,sizeof(meta.iot_probe),"1");
    handle_incoming_publish(&state,"srv/fixture/rsp",probe_rsp,strlen(probe_rsp),&config,&meta);assert(state.probe.stage==LINK_NEED_EVENT);
    state.probe.stage=LINK_WAIT_EVA;state.probe.event_id=123;strcpy_s(state.probe.correlation,40,"135a4120-9ba6-4f6c-8cac-4baf5df8f1df");
    const MqttUserProperty eva_properties[]={{"event_type_code","0"},{"dev_event_id","123"},{"correlationData","135a4120-9ba6-4f6c-8cac-4baf5df8f1df"},{"iot_probe","1"}};
    unsigned char encoded[512];int length=mqtt_build_publish_with_properties(encoded,sizeof(encoded),"srv/fixture/eva","{\"status\":\"success\"}",20,10,1,0,eva_properties,4);assert(length>0);
    uint32_t rem=0;int used=0;assert(!mqtt_decode_remaining_length(encoded+1,length-1,&rem,&used));
    assert(!mqtt_parse_rpc_metadata(encoded+1+used,rem,2,&meta));assert(!strcmp(meta.dev_event_id,"123") && !strcmp(meta.event_type_code,"0") && !strcmp(meta.iot_probe,"1"));
    const char* success="{\"v\":1,\"type\":\"channel_probe\",\"status\":\"success\",\"request_nonce\":\"22222222-2222-4222-8222-222222222222\"}";
    meta.dev_event_id[0]='9';handle_incoming_publish(&state,"srv/fixture/eva",success,strlen(success),&config,&meta);assert(state.probe.stage==LINK_WAIT_EVA);
    meta.dev_event_id[0]='1';handle_incoming_publish(&state,"srv/other/eva",success,strlen(success),&config,&meta);assert(state.probe.stage==LINK_WAIT_EVA);
    const char* duplicate="{\"status\":\"success\",\"status\":\"error\"}";handle_incoming_publish(&state,"srv/fixture/eva",duplicate,strlen(duplicate),&config,&meta);assert(state.probe.stage==LINK_WAIT_EVA);
    handle_incoming_publish(&state,"srv/fixture/eva",success,strlen(success),&config,&meta);assert(state.probe.stage==LINK_DONE);
    assert(link_probe_begin(&state.probe,GetTickCount64(),1000));strcpy_s(state.probe.request_nonce,40,"22222222-2222-4222-8222-222222222222");state.probe.stage=LINK_WAIT_EVA;state.probe.event_id=123;strcpy_s(state.probe.correlation,40,meta.correlation);
    const char* error="{\"v\":1,\"type\":\"channel_probe\",\"status\":\"error\",\"request_nonce\":\"22222222-2222-4222-8222-222222222222\"}";handle_incoming_publish(&state,"srv/fixture/eva",error,strlen(error),&config,&meta);assert(state.probe.stage==LINK_FAILED && state.probe.error==ERROR_BAD_NET_RESP);
    assert(link_probe_begin(&state.probe,GetTickCount64(),1));strcpy_s(state.probe.request_nonce,40,"22222222-2222-4222-8222-222222222222");state.probe.stage=LINK_WAIT_EVA;state.probe.event_id=123;strcpy_s(state.probe.correlation,40,meta.correlation);Sleep(2);
    handle_incoming_publish(&state,"srv/fixture/eva",success,strlen(success),&config,&meta);assert(state.probe.stage==LINK_WAIT_EVA);
    probe_disconnect(&state);assert(state.probe.stage==LINK_FAILED && !state.probe_ready);
    puts("Link barrier: orphan nonce/marker, ordinary NOP refusal, wrong SN/ID, duplicate JSON, errors, late reply and disconnect PASS");
    UpdateFixture update_fixture;assert(update_fixture_init(&update_fixture));
    state.update.enabled=true;state.update.layout=update_fixture.layout;
    assert(update_busy(&state)); /* Missing control file never means normal. */
    assert(update_fixture_put(&update_fixture,1));assert(update_busy(&state));
    state.current_cmd.is_running=true;state.current_cmd.cancel_requested=false;
    char update_tsk[512],update_cancel[512];strcpy_s(update_tsk,sizeof(update_tsk),tsk);strcpy_s(update_cancel,sizeof(update_cancel),empty);
    char* update_id=strstr(update_tsk,"bc57f0f1");assert(update_id);*update_id='9';
    update_id=strstr(update_cancel,"bc57f0f1");assert(update_id);*update_id='9';
    deliver(&state,&config,"srv/fixture/tsk",update_tsk);assert(!state.current_cmd.cancel_requested);
    deliver(&state,&config,"srv/fixture/rsp",update_cancel);assert(!state.current_cmd.cancel_requested);
    n=rpc_recent(&state,"9c57f0f1-a588-4837-8188-02f007cfb0e4",false);assert(n>=0 && state.recent[n].status==409 && strstr(state.recent[n].result,"update_in_progress"));
    state.current_cmd.is_running=false;
    char update_exec[2048];strcpy_s(update_exec,sizeof(update_exec),exec);update_id=strstr(update_exec,"f4106499");assert(update_id);*update_id='9';
    deliver(&state,&config,"srv/fixture/rsp",update_exec);assert(!state.hWorkerThread);
    n=rpc_recent(&state,"94106499-d645-4e31-8913-c2b73f41937a",false);assert(n>=0 && state.recent[n].status==409);
    /* Completed task replay preserves its existing result without starting work. */
    deliver(&state,&config,"srv/fixture/rsp",exec);assert(!state.hWorkerThread);
    n=rpc_recent(&state,"f4106499-d645-4e31-8913-c2b73f41937a",false);assert(n>=0 && state.recent[n].status==200);
    state.probe_ready=7;assert(link_probe_begin(&state.probe,GetTickCount64(),1000));strcpy_s(state.probe.request_nonce,40,"22222222-2222-4222-8222-222222222222");state.probe.stage=LINK_WAIT_RSP;
    strcpy_s(meta.method_code,sizeof(meta.method_code),"0");strcpy_s(meta.correlation,sizeof(meta.correlation),"00000000-0000-0000-0000-000000000000");
    handle_incoming_publish(&state,"srv/fixture/rsp",nop,strlen(nop),&config,&meta);assert(state.probe.stage==LINK_WAIT_RSP);
    strcpy_s(meta.iot_probe,sizeof(meta.iot_probe),"1");strcpy_s(meta.correlation,sizeof(meta.correlation),state.probe.request_nonce);
    handle_incoming_publish(&state,"srv/fixture/rsp",probe_rsp,strlen(probe_rsp),&config,&meta);assert(state.probe.stage==LINK_NEED_EVENT);
    state.probe.stage=LINK_WAIT_EVA;state.probe.event_id=123;strcpy_s(state.probe.correlation,40,"135a4120-9ba6-4f6c-8cac-4baf5df8f1df");
    strcpy_s(meta.correlation,sizeof(meta.correlation),state.probe.correlation);strcpy_s(meta.dev_event_id,sizeof(meta.dev_event_id),"123");strcpy_s(meta.event_type_code,sizeof(meta.event_type_code),"0");
    handle_incoming_publish(&state,"srv/fixture/eva",success,strlen(success),&config,&meta);assert(state.probe.stage==LINK_DONE);
    /* Plain local TCP fixture, no MQTT CONNECT/client ID or real broker. */
    assert(!WSAStartup(MAKEWORD(2,2),&wsa));
    listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(listener!=INVALID_SOCKET);
    address.sin_port=0;assert(!bind(listener,(const struct sockaddr*)&address,sizeof(address)) && !listen(listener,1));
    address_size=sizeof(address);assert(!getsockname(listener,(struct sockaddr*)&address,&address_size));
    client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(client!=INVALID_SOCKET);
    assert(socket_connect_bounded(client,(const struct sockaddr*)&address,sizeof(address),NULL));
    peer=accept(listener,NULL,NULL);assert(peer!=INVALID_SOCKET);state.sock=client;state.connected=true;
    FILETIME report_time;GetSystemTimeAsFileTime(&report_time);ULONGLONG report_stamp=((ULONGLONG)report_time.dwHighDateTime<<32)|report_time.dwLowDateTime;
    L4RemoteResult report={0};strcpy_s(report.operation_id,37,"135a4120-9ba6-4f6c-8cac-4baf5df8f1df");strcpy_s(report.requested_version,32,"latest");strcpy_s(report.previous_version,32,"1.13.6");
    report.target=1;report.result=L4_REMOTE_RESULT_FAILED;report.error=ERROR_TIMEOUT;report.started_at=report_stamp;report.finished_at=report_stamp;char report_json[2048];
    const char* report_nonce="44106499-d645-4e31-8913-c2b73f41937a";
    assert(update_event_preparation_json(&report,123,report_nonce,report_json,sizeof(report_json)));
    assert(reporting_ready(&state) && reporting_event(&state,report_json,123,report_nonce));
    unsigned char report_packet[2048];size_t report_size=read_publish(peer,report_packet);
    assert(packet_has(report_packet,report_size,"event_type_code") && packet_has(report_packet,report_size,"76") && packet_has(report_packet,report_size,"\"449\"") && packet_has(report_packet,report_size,report_nonce));
    puts("Registered7032 dispatch and event76: existing client QoS1 nonretained frame during update, no CONNECT/persistence PASS");
    UserEvent event={0};event.code=999;assert(!publish_user_event(&event,&state));
    fd_set readable;FD_ZERO(&readable);FD_SET(peer,&readable);struct timeval immediate={0};assert(!select(0,&readable,NULL,NULL,&immediate));
    const char* navigation="{\"v\":2,\"command_id\":\"24106499-d645-4e31-8913-c2b73f41937a\",\"lease_id\":\"ea9c98c0-bcd2-4e87-9ab5-2df74eab41e7\",\"action\":\"list\"}";
    handle_incoming_publish(&state,"srv/fixture/fmc",navigation,strlen(navigation),&config,&meta);
    unsigned char response[2048];size_t response_size=read_publish(peer,response);
    assert(packet_has(response,response_size,"update_in_progress") && packet_has(response,response_size,"24106499-d645-4e31-8913-c2b73f41937a") && packet_has(response,response_size,"ea9c98c0-bcd2-4e87-9ab5-2df74eab41e7"));
    assert(link_probe_begin(&state.probe,GetTickCount64(),1000));probe_tick(&state,&config);
    response_size=read_publish(peer,response);assert(state.probe.stage==LINK_WAIT_RSP);
    assert(packet_has(response,response_size,"iot_probe") && packet_has(response,response_size,"channel_probe") && packet_has(response,response_size,state.probe.request_nonce));
    assert(!packet_has(response,response_size,"rpc_methods") && !packet_has(response,response_size,"client_id"));
    memset(&meta,0,sizeof(meta));strcpy_s(meta.iot_probe,sizeof(meta.iot_probe),"1");strcpy_s(meta.method_code,sizeof(meta.method_code),"0");strcpy_s(meta.correlation,sizeof(meta.correlation),state.probe.request_nonce);
    handle_incoming_publish(&state,"srv/fixture/rsp",probe_rsp,strlen(probe_rsp),&config,&meta);assert(state.probe.stage==LINK_NEED_EVENT);
    probe_tick(&state,&config);response_size=read_publish(peer,response);assert(state.probe.stage==LINK_WAIT_EVA);
    assert(strcmp(state.probe.request_nonce,state.probe.correlation) && state.probe.event_id);
    assert(packet_has(response,response_size,"request_nonce") && packet_has(response,response_size,state.probe.request_nonce) && packet_has(response,response_size,state.probe.correlation));
    assert(!packet_has(response,response_size,"\"200\":75") && !packet_has(response,response_size,"certificate"));
    probe_disconnect(&state);
    puts("Orphan outbound: QoS1 nonretained request/event, distinct nonce, existing client only, no normal polling/inventory PASS");
    state.sock=INVALID_SOCKET;state.connected=false;closesocket(peer);closesocket(client);closesocket(listener);WSACleanup();
    assert(update_fixture_put(&update_fixture,2));assert(update_busy(&state));
    L4UpdateConsumer restarted={0};restarted.enabled=true;restarted.layout=update_fixture.layout;L4UpdateState persisted;
    assert(l4_update_consumer_read(&restarted,&persisted) && persisted.window==2);
    test_drain(&state,&update_fixture);assert(update_fixture_put(&update_fixture,0));assert(!update_busy(&state));
    const char* ping="{\"id\":\"04106499-d645-4e31-8913-c2b73f41937a\",\"header\":{\"method_code\":7003},\"payload\":{\"dt\":[]}}";
    deliver(&state,&config,"srv/fixture/rsp",ping);
    n=rpc_recent(&state,"04106499-d645-4e31-8913-c2b73f41937a",false);assert(n>=0 && state.recent[n].status==200);
    assert(update_fixture_dispose(&update_fixture));assert(update_busy(&state));
    HANDLE cancel=CreateEventW(NULL,TRUE,FALSE,NULL);assert(cancel);
    assert(probe_local(0,10,cancel,&state)==ERROR_INVALID_DATA);CloseHandle(cancel);
    puts("Persistent update consumer: closed admission/cancel, completed replay, fresh NOP/EVA, explicit clear and missing-state refusal PASS");
    DeleteCriticalSection(&state.probe_cs);DeleteCriticalSection(&state.send_cs);
    puts("RPC runtime: addressed cancel / TSK-RSP duplicate / result status / bounded TTL passed");
    return 0;
}
