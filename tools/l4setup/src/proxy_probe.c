#include "proxy_probe.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "../../leo4proxy/src/policy_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Literal loopback HTTP: connect/send/receive share the caller's deadline. */
static bool socket_ready(SOCKET sock,bool writing,ULONGLONG deadline) {
    ULONGLONG now=GetTickCount64();if(now>=deadline)return false;
    DWORD remaining=(DWORD)(deadline-now);struct timeval timeout={(long)(remaining/1000),(long)(remaining%1000)*1000};
    fd_set ready,errors;FD_ZERO(&ready);FD_ZERO(&errors);FD_SET(sock,&ready);FD_SET(sock,&errors);
    int selected=select(0,writing?NULL:&ready,writing?&ready:NULL,&errors,&timeout);
    if(selected<=0 || FD_ISSET(sock,&errors))return false;
    int error=0,length=sizeof(error);
    return !getsockopt(sock,SOL_SOCKET,SO_ERROR,(char*)&error,&length) && !error;
}
bool setup_proxy_probe(int port,bool require_ready,int timeout_ms) {
    if(timeout_ms<1 || port<1 || port>65535)return false;
    ULONGLONG deadline=GetTickCount64()+(ULONGLONG)timeout_ms;
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa))return false;
    SOCKET sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);bool valid=false;
    if(sock==INVALID_SOCKET){WSACleanup();return false;}
    u_long nonblocking=1;
    if(ioctlsocket(sock,FIONBIO,&nonblocking))goto done;
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_port=htons((u_short)port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(sock,(struct sockaddr*)&address,sizeof(address)) && WSAGetLastError()!=WSAEWOULDBLOCK)goto done;
    char request[192];int size=sprintf_s(request,sizeof(request),"GET /_leo4/info HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",port);
    for(int sent=0;sent<size;) {
        if(!socket_ready(sock,true,deadline))goto done;
        int count=send(sock,request+sent,size-sent,0);if(count<=0)goto done;sent+=count;
    }
    char response[24577]={0};size_t used=0,header_size=0,body_size=0;
    for(;;) {
        if(used==sizeof(response)-1 || !socket_ready(sock,false,deadline))goto done;
        int count=recv(sock,response+used,(int)(sizeof(response)-1-used),0);if(count<=0)goto done;
        used+=(size_t)count;response[used]=0;
        if(!header_size) {
            char* end=strstr(response,"\r\n\r\n");if(!end)continue;
            if(strncmp(response,"HTTP/1.1 200 ",13) && strncmp(response,"HTTP/1.0 200 ",13))goto done;
            header_size=(size_t)(end-response)+4;bool length_seen=false;
            for(char* line=strstr(response,"\r\n");line && line<end;) {
                line+=2;char* next=strstr(line,"\r\n");if(!next || next>end)goto done;
                if(!_strnicmp(line,"Transfer-Encoding:",18))goto done;
                if(!_strnicmp(line,"Content-Length:",15)) {
                    if(length_seen)goto done;length_seen=true;char* value=line+15;while(*value==' ' || *value=='\t')++value;
                    if(*value<'0' || *value>'9')goto done;
                    char* stop;unsigned long amount=strtoul(value,&stop,10);while(*stop==' ' || *stop=='\t')++stop;
                    if(stop!=next || amount>16384)goto done;body_size=amount;
                }
                line=next;
            }
            if(!length_seen)goto done;
        }
        if(used<header_size+body_size)continue;
        if(used!=header_size+body_size)goto done;
        PolicyJson json;char status[64];bool found=false;
        valid=policy_json_parse(&json,response+header_size,body_size) &&
            policy_json_string(&json,policy_json_field(&json,0,"status"),status,sizeof(status)) &&
            policy_json_bool(&json,policy_json_field(&json,0,"certificate_found"),&found);
        valid=valid && ((!strcmp(status,"ready") && found) || (!require_ready && !strcmp(status,"waiting_for_certificate") && !found));
        break;
    }
done:
    closesocket(sock);WSACleanup();return valid;
}
