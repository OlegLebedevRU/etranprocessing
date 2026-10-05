#ifndef LEO4_FM_CONNECT_H
#define LEO4_FM_CONNECT_H
#include "policy.h"
#include "endpoints.h"
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Restricted loopback HTTPS tunnel. The allowlist comes only from verified PB
 * policy; it never attaches the terminal credential to storage or logs grants.
 * WinHTTP validates storage TLS end-to-end; all outbound TCP is owned by this proxy. */
static bool fm_connect_target(const char* authority,char host[256],int* port) {
    if (!authority || strlen(authority)>=272) return false;
    const char* colon=strchr(authority,':');
    if (!colon || colon==authority || (size_t)(colon-authority)>=256 || !colon[1]) return false;
    for(const char* digit=colon+1;*digit;digit++) if(*digit<'0' || *digit>'9') return false;
    if(strlen(colon+1)>5) return false;
    *port=atoi(colon+1);if(*port<1 || *port>65535) return false;
    memcpy(host,authority,(size_t)(colon-authority));host[colon-authority]=0;
    return endpoint_host_valid(host) && policy_fm_authority_allowed(authority);
}
static bool fm_tunnel_send(SOCKET socket,const char* data,int length,ULONGLONG deadline) {
    int sent=0;
    while(sent<length && GetTickCount64()<deadline) {
        int n=send(socket,data+sent,length-sent,0);
        if(n<=0) return false;sent+=n;
    }
    return sent==length;
}
static void fm_connect_storage(SOCKET client,const char* authority) {
    static volatile LONG active;
    char host[256];int port;
    if (!fm_connect_target(authority,host,&port) || InterlockedCompareExchange(&active,1,0)) {
        const char* denied="HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(client,denied,(int)strlen(denied),0);return;
    }
    SOCKET remote=INVALID_SOCKET;PolicySocket registered={0};bool ready=false;
    char address[16];ULONGLONG deadline=GetTickCount64()+45000;
    /* Numeric connect to the validated public result prevents a second DNS lookup. */
    if (endpoint_resolve_ipv4(host,address,2000) && endpoint_ipv4(address) && policy_fm_authority_allowed(authority)) {
        struct sockaddr_in target={0};target.sin_family=AF_INET;target.sin_port=htons((u_short)port);
        if(InetPtonA(AF_INET,address,&target.sin_addr)==1) remote=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(remote!=INVALID_SOCKET) {
            u_long nonblocking=1;ioctlsocket(remote,FIONBIO,&nonblocking);
            int connected=policy_media_connect(&registered,remote,(struct sockaddr*)&target,sizeof(target));
            if(connected==0) ready=true;
            else if(WSAGetLastError()==WSAEWOULDBLOCK) {
                fd_set writes,errors;FD_ZERO(&writes);FD_ZERO(&errors);FD_SET(remote,&writes);FD_SET(remote,&errors);
                struct timeval wait={2,0};int error=1,length=sizeof(error);
                ready=select(0,NULL,&writes,&errors,&wait)>0 && FD_ISSET(remote,&writes) &&
                    !FD_ISSET(remote,&errors) && !getsockopt(remote,SOL_SOCKET,SO_ERROR,(char*)&error,&length) && !error;
            }
            nonblocking=0;ioctlsocket(remote,FIONBIO,&nonblocking);
            DWORD timeout=1000;setsockopt(remote,SOL_SOCKET,SO_SNDTIMEO,(char*)&timeout,sizeof(timeout));
            setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,(char*)&timeout,sizeof(timeout));
        }
    }
    ready=ready && policy_fm_authority_allowed(authority);
    const char* response=ready?"HTTP/1.1 200 Connection Established\r\n\r\n":
        "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    if(!fm_tunnel_send(client,response,(int)strlen(response),deadline)) ready=false;
    unsigned long long counts[2]={0,0};char buffer[65536];
    while(ready && GetTickCount64()<deadline && policy_fm_authority_allowed(authority)) {
        fd_set reads;FD_ZERO(&reads);FD_SET(client,&reads);FD_SET(remote,&reads);
        struct timeval wait={0,250000};int selected=select(0,&reads,NULL,NULL,&wait);
        if(selected==SOCKET_ERROR) break;if(!selected) continue;
        SOCKET from[]={client,remote},to[]={remote,client};
        for(int direction=0;direction<2;direction++) if(FD_ISSET(from[direction],&reads)) {
            int n=recv(from[direction],buffer,sizeof(buffer),0);
            if(n<=0 || counts[direction]+n>70ULL*1024*1024 || !policy_fm_authority_allowed(authority) ||
                !fm_tunnel_send(to[direction],buffer,n,deadline)) {ready=false;break;}
            counts[direction]+=n;
        }
    }
    policy_socket_unregister(&registered);
    if(remote!=INVALID_SOCKET) {shutdown(remote,SD_BOTH);closesocket(remote);}
    InterlockedExchange(&active,0);
}
#endif
