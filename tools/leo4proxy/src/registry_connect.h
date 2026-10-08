#pragma once
#include "policy.h"
#include "endpoints.h"
#include "../../l4common/registry_target.h"
#include <ws2tcpip.h>
#include <string.h>

/* Public Registry only, distinct from FM policy storage. Caller (WinHTTP) owns
 * end-to-end TLS verification; proxy never attaches a terminal certificate. */
static bool registry_connect_target(const char* authority){
    return authority && !strcmp(authority,L4_REGISTRY_AUTHORITY) && policy_registry_allowed();
}
static bool registry_public_address(const char* text){
    IN_ADDR address;if(!endpoint_ipv4(text) || InetPtonA(AF_INET,text,&address)!=1)return false;unsigned long ip=ntohl(address.S_un.S_addr);
    /* In addition to the shared private/link-local/CGN/multicast guard, exclude
     * reserved protocol, benchmarking, documentation and deprecated relay nets. */
    unsigned long subnet=ip&0xffffff00UL;
    return subnet!=0xc0000000UL && subnet!=0xc0000200UL && subnet!=0xc0586300UL && subnet!=0xc6336400UL && subnet!=0xcb007100UL &&
        (ip&0xfffe0000UL)!=0xc6120000UL;
}
static bool registry_send(SOCKET s,const char* bytes,int size,ULONGLONG deadline){
    int at=0;while(at<size && GetTickCount64()<deadline){int n=send(s,bytes+at,size-at,0);if(n<=0)return false;at+=n;}return at==size;
}
static void registry_connect(SOCKET client,const char* authority){
    static volatile LONG active;bool acquired=false,ready=false;SOCKET remote=INVALID_SOCKET;PolicySocket registered={0};
    ULONGLONG slot_deadline=GetTickCount64()+1500;char address[16];DWORD timeout=1000;
    setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,(char*)&timeout,sizeof(timeout));
    /* Closing WinHTTP handles and releasing the proxy slot are asynchronous.
     * Brief bounded handoff avoids rejecting consecutive metadata GETs. */
    do{if(!registry_connect_target(authority))break;if(!InterlockedCompareExchange(&active,1,0)){acquired=true;break;}Sleep(10);}while(GetTickCount64()<slot_deadline);
    if(!acquired){const char* denied=registry_connect_target(authority)?"HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n":
        "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        registry_send(client,denied,(int)strlen(denied),GetTickCount64()+1000);return;}
    ULONGLONG deadline=GetTickCount64()+600000;
    if(endpoint_resolve_ipv4(L4_REGISTRY_HOST,address,2000) && registry_public_address(address) && registry_connect_target(authority)){
        struct sockaddr_in target={0};target.sin_family=AF_INET;target.sin_port=htons(443);
        if(InetPtonA(AF_INET,address,&target.sin_addr)==1)remote=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(remote!=INVALID_SOCKET){u_long mode=1;ioctlsocket(remote,FIONBIO,&mode);
            int rc=policy_registry_connect(&registered,remote,(struct sockaddr*)&target,sizeof(target));
            if(rc==0)ready=true;else if(WSAGetLastError()==WSAEWOULDBLOCK){fd_set writes,errors;FD_ZERO(&writes);FD_ZERO(&errors);FD_SET(remote,&writes);FD_SET(remote,&errors);
                struct timeval wait={2,0};int error=1,length=sizeof(error);
                ready=select(0,NULL,&writes,&errors,&wait)>0 && FD_ISSET(remote,&writes) && !FD_ISSET(remote,&errors) &&
                    !getsockopt(remote,SOL_SOCKET,SO_ERROR,(char*)&error,&length) && !error;}
            mode=0;ioctlsocket(remote,FIONBIO,&mode);
            setsockopt(remote,SOL_SOCKET,SO_SNDTIMEO,(char*)&timeout,sizeof(timeout));setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,(char*)&timeout,sizeof(timeout));}
    }
    ready=ready && registry_connect_target(authority);
    const char* response=ready?"HTTP/1.1 200 Connection Established\r\n\r\n":"HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    if(!registry_send(client,response,(int)strlen(response),deadline))ready=false;
    ULONGLONG counts[2]={0,0},limits[2]={1024*1024,L4_REGISTRY_MAX_ARCHIVE+16ULL*1024*1024};char buffer[16384];
    while(ready && GetTickCount64()<deadline && registry_connect_target(authority)){
        fd_set reads;FD_ZERO(&reads);FD_SET(client,&reads);FD_SET(remote,&reads);struct timeval wait={0,250000};
        int selected=select(0,&reads,NULL,NULL,&wait);if(selected==SOCKET_ERROR)break;if(!selected)continue;
        SOCKET from[]={client,remote},to[]={remote,client};
        for(unsigned direction=0;direction<2;direction++)if(FD_ISSET(from[direction],&reads)){int n=recv(from[direction],buffer,sizeof(buffer),0);
            if(n<=0 || counts[direction]+n>limits[direction] || !registry_connect_target(authority) ||
               !registry_send(to[direction],buffer,n,deadline)){ready=false;break;}counts[direction]+=n;}
    }
    policy_socket_unregister(&registered);if(remote!=INVALID_SOCKET){shutdown(remote,SD_BOTH);closesocket(remote);}InterlockedExchange(&active,0);
}
