#include <winsock2.h>
#include <windows.h>
#include "proxy_probe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {SOCKET listener;const char* body;bool slow;bool malformed;} Fixture;
static DWORD WINAPI serve(void* value) {
    Fixture* f=(Fixture*)value;SOCKET sock=accept(f->listener,NULL,NULL);if(sock==INVALID_SOCKET)return 1;
    DWORD timeout=1000;setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));
    char request[256];recv(sock,request,sizeof(request),0);
    char response[512];int size=sprintf_s(response,sizeof(response),"HTTP/1.1 200 OK\r\nContent-Length: %u\r\n%s\r\n%s",(unsigned int)strlen(f->body),f->malformed?"Content-Length: 1\r\n":"",f->body);
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
    ULONGLONG start=GetTickCount64();assert(setup_proxy_probe(ntohs(address.sin_port),require,150)==expected);
    assert(GetTickCount64()-start<400);assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);
    CloseHandle(thread);closesocket(listener);
}
int main(void) {
    WSADATA wsa;assert(!WSAStartup(MAKEWORD(2,2),&wsa));
    const char* ready="{\"status\":\"ready\",\"certificate_found\":true}";
    const char* standby="{\"status\":\"waiting_for_certificate\",\"certificate_found\":false}";
    run(ready,true,false,false,true);run(standby,false,false,false,true);run(standby,true,false,false,false);
    run("{\"status\":\"ready\",\"certificate_found\":false}",true,false,false,false);
    run("{}",false,false,false,false);run(ready,true,true,false,false);run(ready,true,false,true,false);
    assert(!setup_proxy_probe(0,false,1));assert(!setup_proxy_probe(1,false,0));
    WSACleanup();puts("Local HTTP readiness/deadline: 9 cases PASS");return 0;
}
