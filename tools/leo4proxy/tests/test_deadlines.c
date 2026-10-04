/* Slow fragmented I/O must consume one absolute deadline, not renew per call. */
#include "schannel_tls.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static ULONGLONG clock_ms;
static DWORD socket_timeout;
static ULONGLONG WINAPI fixture_clock(void) {return clock_ms;}
static int WSAAPI fixture_options(SOCKET sock,int level,int name,const char* value,int size) {
    (void)sock;(void)level;(void)name;assert(size==sizeof(DWORD));memcpy(&socket_timeout,value,sizeof(DWORD));return 0;
}
static int fragment(void) {
    if(socket_timeout<10) {clock_ms+=socket_timeout;WSASetLastError(WSAETIMEDOUT);return -1;}
    clock_ms+=10;return 1;
}
static int WSAAPI fixture_send(SOCKET sock,const char* data,int len,int flags) {(void)sock;(void)data;(void)flags;assert(len>0);return fragment();}
static int WSAAPI fixture_recv(SOCKET sock,char* data,int len,int flags) {(void)sock;(void)flags;assert(len>0);data[0]=0;return fragment();}
static SECURITY_STATUS SEC_ENTRY fixture_decrypt(PCtxtHandle context,PSecBufferDesc message,ULONG sequence,PULONG quality) {
    (void)context;(void)message;(void)sequence;(void)quality;return SEC_E_INCOMPLETE_MESSAGE;
}
#define GetTickCount64 fixture_clock
#define setsockopt fixture_options
#define send fixture_send
#define recv fixture_recv
#define DecryptMessage fixture_decrypt
#include "../src/schannel_tls.c"
bool policy_media_allowed(void) {return true;}
int policy_media_connect(PolicySocket* p,SOCKET s,const struct sockaddr* a,int n) {(void)p;(void)s;(void)a;(void)n;return SOCKET_ERROR;}
void policy_socket_unregister(PolicySocket* p) {(void)p;}
bool endpoint_resolve_ipv4(const char* h,char out[16],DWORD t) {(void)h;(void)out;(void)t;return false;}
int endpoint_resolve_ipv4_all(const char* h,char out[8][16],DWORD t) {(void)h;(void)out;(void)t;return 0;}
int endpoints_candidates_timed(const ProxyConfig* c,int n,bool r,Leo4Endpoint* e,DWORD t) {(void)c;(void)n;(void)r;(void)e;(void)t;return 0;}
void endpoints_connected(int n,const Leo4Endpoint* e) {(void)n;(void)e;}
const char* endpoint_logical_name(const ProxyConfig* c,int n) {(void)c;(void)n;return "example.com";}
int endpoint_attempt_timeout(const Leo4Endpoint* c,int count,int n,int remaining) {(void)c;(void)count;(void)n;return remaining;}
int main(void) {
    const DWORD budgets[]={1,25,50,200};int cases=0;
    for(int n=0;n<4;n++) {
        BYTE bytes[128]={0};clock_ms=1000;
        assert(send_all_until((SOCKET)1,bytes,sizeof(bytes),1000+budgets[n])<0);
        assert(clock_ms==1000+budgets[n]);++cases;
        SChannelSession s={0};s.sock=(SOCKET)1;s.isConnected=true;s.io_deadline=1000+budgets[n];
        s.recvBufAlloc=65536;s.recvBuf=(BYTE*)malloc(s.recvBufAlloc);assert(s.recvBuf);
        clock_ms=1000;char output[16];assert(schannel_recv(&s,output,sizeof(output))<0);
        assert(clock_ms==s.io_deadline);free(s.recvBuf);++cases;
        assert(!socket_deadline((SOCKET)1,clock_ms));assert(socket_deadline((SOCKET)1,0));++cases;
    }
    puts("Absolute send/fragmented TLS receive deadlines: 12 cases PASS");return 0;
}
