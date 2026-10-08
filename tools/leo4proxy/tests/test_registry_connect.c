/* Real loopback stream; explicit fixture-only DNS/public-address/443 mapping.
 * No external Registry, policy storage, credentials or live services. */
#include "../src/registry_connect.h"
#include <assert.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
static volatile LONG denied,public_target=1;static unsigned connects;static int fixture_port;
bool policy_registry_allowed(void){return !denied;}
bool endpoint_ipv4(const char* address){(void)address;return public_target!=0;}
bool endpoint_resolve_ipv4(const char* host,char out[16],DWORD timeout){assert(!strcmp(host,L4_REGISTRY_HOST) && timeout==2000);strcpy_s(out,16,"127.0.0.1");return true;}
int policy_registry_connect(PolicySocket* node,SOCKET s,const struct sockaddr* target,int length){
    assert(length==sizeof(struct sockaddr_in));struct sockaddr_in address=*(const struct sockaddr_in*)target;assert(ntohs(address.sin_port)==443);
    if(denied){WSASetLastError(WSAEACCES);return SOCKET_ERROR;}++connects;address.sin_port=htons((u_short)fixture_port);
    node->socket=s;node->registered=true;return connect(s,(struct sockaddr*)&address,sizeof(address));
}
void policy_socket_unregister(PolicySocket* node){node->registered=false;}
static SOCKET listener(int* port){SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(s!=INVALID_SOCKET);struct sockaddr_in address={0};
    address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(!bind(s,(struct sockaddr*)&address,sizeof(address)));assert(!listen(s,1));
    int length=sizeof(address);assert(!getsockname(s,(struct sockaddr*)&address,&length));*port=ntohs(address.sin_port);return s;
}
static SOCKET pair(SOCKET* peer){int port;SOCKET server=listener(&port),client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);struct sockaddr_in address={0};
    address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons((u_short)port);assert(!connect(client,(struct sockaddr*)&address,sizeof(address)));
    *peer=accept(server,NULL,NULL);assert(*peer!=INVALID_SOCKET);closesocket(server);DWORD timeout=3000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));return client;
}
static unsigned __stdcall tunnel(void* value){SOCKET s=*(SOCKET*)value;registry_connect(s,L4_REGISTRY_AUTHORITY);closesocket(s);return 0;}
static void status(SOCKET client,const char* expected){char bytes[256];int n=recv(client,bytes,sizeof(bytes)-1,0);assert(n>0);bytes[n]=0;assert(strstr(bytes,expected));}
int main(void){WSADATA data;assert(!WSAStartup(MAKEWORD(2,2),&data));
    const char* bad[]={NULL,"other.example:443",L4_REGISTRY_HOST ":80",L4_REGISTRY_HOST ":0443",L4_REGISTRY_AUTHORITY "/path",L4_REGISTRY_AUTHORITY "@evil", "127.0.0.1:443", "L4TOOLS-generic.ar.cloud.ru:443"};
    for(unsigned i=0;i<_countof(bad);i++)assert(!registry_connect_target(bad[i]));assert(registry_connect_target(L4_REGISTRY_AUTHORITY));
    const char* reserved[]={"198.18.0.1","198.19.255.254","192.0.0.1","192.0.2.1","192.88.99.1","198.51.100.1","203.0.113.1","invalid"};
    for(unsigned i=0;i<_countof(reserved);i++)assert(!registry_public_address(reserved[i]));assert(registry_public_address("1.1.1.1"));
    SOCKET peer,client=pair(&peer);denied=1;registry_connect(peer,L4_REGISTRY_AUTHORITY);status(client,"403");assert(connects==0);closesocket(peer);closesocket(client);denied=0;
    client=pair(&peer);public_target=0;registry_connect(peer,L4_REGISTRY_AUTHORITY);status(client,"502");assert(connects==0);closesocket(peer);closesocket(client);public_target=1;
    SOCKET server=listener(&fixture_port);client=pair(&peer);HANDLE worker=(HANDLE)_beginthreadex(NULL,0,tunnel,&peer,0,NULL);assert(worker);
    SOCKET upstream=accept(server,NULL,NULL);assert(upstream!=INVALID_SOCKET);status(client,"200 Connection Established");assert(connects==1);
    SOCKET other_peer,other_client=pair(&other_peer);registry_connect(other_peer,L4_REGISTRY_AUTHORITY);status(other_client,"503");assert(connects==1);closesocket(other_peer);closesocket(other_client);
    char bytes[32];assert(send(client,"TLS-up",6,0)==6);assert(recv(upstream,bytes,sizeof(bytes),0)==6 && !memcmp(bytes,"TLS-up",6));
    assert(send(upstream,"TLS-down",8,0)==8);assert(recv(client,bytes,sizeof(bytes),0)==8 && !memcmp(bytes,"TLS-down",8));
    assert(!registry_send(client,"x",1,GetTickCount64()));
    other_client=pair(&other_peer);HANDLE next=(HANDLE)_beginthreadex(NULL,0,tunnel,&other_peer,0,NULL);assert(next);Sleep(50);closesocket(client);
    assert(WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);SOCKET next_upstream=accept(server,NULL,NULL);assert(next_upstream!=INVALID_SOCKET);
    status(other_client,"200 Connection Established");assert(connects==2);denied=1;assert(WaitForSingleObject(next,2000)==WAIT_OBJECT_0);assert(recv(other_client,bytes,sizeof(bytes),0)==0);
    CloseHandle(worker);CloseHandle(next);closesocket(other_client);closesocket(upstream);closesocket(next_upstream);closesocket(server);WSACleanup();
    puts("Registry CONNECT PASS: fixed authority/443, public DNS, single slot, actual duplex, deadline, close handoff and active deny closure");return 0;
}
