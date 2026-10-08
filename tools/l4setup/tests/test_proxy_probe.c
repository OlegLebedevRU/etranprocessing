#include <winsock2.h>
#include <windows.h>
#include "proxy_probe.h"
#include "../../l4common/proxy_policy_gate.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {SOCKET listener;const char* body;bool slow;bool malformed;} Fixture;
static bool production_policy_fixture;
static DWORD WINAPI serve(void* value) {
    Fixture* f=(Fixture*)value;SOCKET sock=accept(f->listener,NULL,NULL);if(sock==INVALID_SOCKET)return 1;
    DWORD timeout=1000;setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));
    char request[256];recv(sock,request,sizeof(request),0);
    char response[2048];int size=sprintf_s(response,sizeof(response),"HTTP/1.1 200 OK\r\nContent-Length: %u\r\n%s\r\n%s",(unsigned int)strlen(f->body),f->malformed?"Content-Length: 1\r\n":"",f->body);
    if(f->slow) {for(int n=0;n<size;n++){if(send(sock,response+n,1,0)!=1)break;Sleep(10);}}
    else send(sock,response,size,0);
    closesocket(sock);return 0;
}
static void run(const char* body,bool require,bool slow,bool malformed,bool expected) {
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(listener!=INVALID_SOCKET);
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(!bind(listener,(struct sockaddr*)&address,sizeof(address)));assert(!listen(listener,1));
    int length=sizeof(address);assert(!getsockname(listener,(struct sockaddr*)&address,&length));
    Fixture f={listener,body,slow,malformed};HANDLE thread=CreateThread(NULL,0,serve,&f,0,NULL);assert(thread);
    ULONGLONG start=GetTickCount64();bool ok=production_policy_fixture?
        setup_proxy_probe_policy(ntohs(address.sin_port),150,"1111111111111111111111111111111111111111"):
        setup_proxy_probe(ntohs(address.sin_port),require,150);assert(ok==expected);
    assert(GetTickCount64()-start<400);assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);
    CloseHandle(thread);closesocket(listener);
}
int main(void) {
    const char* policies[]={
        "{\"policy\":{\"mqtt_rtp_allowed\":true,\"outgoing_https_allowed\":true,\"storage_pending\":false,\"last_success_at\":1000,\"offline_allowed_until\":260200,\"generation\":1,\"stop_facts\":\"\",\"last_error\":\"\"}}",
        "{\"policy\":{\"mqtt_rtp_allowed\":false,\"outgoing_https_allowed\":true,\"storage_pending\":false,\"last_success_at\":1000,\"offline_allowed_until\":260200,\"generation\":1,\"stop_facts\":\"denied\",\"last_error\":\"\"}}",
        "{\"policy\":{\"mqtt_rtp_allowed\":true,\"outgoing_https_allowed\":true,\"storage_pending\":false,\"last_success_at\":0,\"offline_allowed_until\":260200,\"generation\":1,\"stop_facts\":\"\",\"last_error\":\"unavailable\"}}",
        "{}"};
    PolicyJson policy;assert(policy_json_parse(&policy,policies[0],strlen(policies[0])));assert(l4_proxy_policy_gate(&policy,1000));
    assert(!l4_proxy_policy_gate(&policy,999));assert(!l4_proxy_policy_gate(&policy,260200));
    for(unsigned i=1;i<sizeof(policies)/sizeof(policies[0]);i++){assert(policy_json_parse(&policy,policies[i],strlen(policies[i])));assert(!l4_proxy_policy_gate(&policy,1000));}
    WSADATA wsa;assert(!WSAStartup(MAKEWORD(2,2),&wsa));
    const char* ready="{\"status\":\"ready\",\"certificate_found\":true}";
    const char* standby="{\"status\":\"waiting_for_certificate\",\"certificate_found\":false}";
    run(ready,true,false,false,true);run(standby,false,false,false,true);run(standby,true,false,false,false);
    run("{\"status\":\"ready\",\"certificate_found\":false}",true,false,false,false);
    run("{}",false,false,false,false);run(ready,true,true,false,false);run(ready,true,false,true,false);
    assert(!setup_proxy_probe(0,false,1));assert(!setup_proxy_probe(1,false,0));
    production_policy_fixture=true;char body[1024];unsigned long long now=l4_proxy_policy_utc();
    sprintf_s(body,sizeof(body),"{\"status\":\"ready\",\"certificate_found\":true,\"has_private_key\":true,\"routes_active\":true,\"sn\":\"773\",\"thumbprint\":\"1111111111111111111111111111111111111111\",\"policy\":{\"mqtt_rtp_allowed\":true,\"outgoing_https_allowed\":true,\"storage_pending\":false,\"last_success_at\":%llu,\"offline_allowed_until\":%llu,\"generation\":1,\"stop_facts\":\"\",\"last_error\":\"\"}}",now,now+259200ULL);
    run(body,true,false,false,true);run(ready,true,false,false,false);
    char* selected=strstr(body,"1111111111111111111111111111111111111111");assert(selected);*selected='2';run(body,true,false,false,false);*selected='1';
    char* permission=strstr(body,"mqtt_rtp_allowed");assert(permission);permission=strchr(permission,':')+1;memcpy(permission,"null",4);run(body,true,false,false,false);
    WSACleanup();puts("Local HTTP readiness/deadline + strict production policy, identity, expired/denied/unavailable guards PASS");return 0;
}
