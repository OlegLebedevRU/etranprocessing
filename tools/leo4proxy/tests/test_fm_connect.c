/* Real loopback byte stream with explicit fixture-only public-address exception.
 * No external provider, certificate, registry, service or terminal commands. */
#include "../src/fm_connect.h"
#include <assert.h>
#include <process.h>
static char allowed[272];static volatile LONG denied, public_target=1;
bool policy_fm_authority_allowed(const char* authority) {return !denied && (!authority || !strcmp(authority,allowed));}
bool endpoint_host_valid(const char* host) {return !strcmp(host,"storage.example");}
bool endpoint_ipv4(const char* address) {(void)address;return public_target!=0;}
bool endpoint_resolve_ipv4(const char* host,char out[16],DWORD timeout) {
    assert(!strcmp(host,"storage.example") && timeout==2000);strcpy_s(out,16,"127.0.0.1");return true;
}
int policy_media_connect(PolicySocket* node,SOCKET socket,const struct sockaddr* address,int length) {
    node->socket=socket;node->registered=true;return connect(socket,address,length);
}
void policy_socket_unregister(PolicySocket* node) {node->registered=false;}
static SOCKET listener(int* port) {
    SOCKET socket_handle=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(socket_handle!=INVALID_SOCKET);
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(!bind(socket_handle,(struct sockaddr*)&address,sizeof(address)));assert(!listen(socket_handle,1));
    int length=sizeof(address);assert(!getsockname(socket_handle,(struct sockaddr*)&address,&length));*port=ntohs(address.sin_port);return socket_handle;
}
static SOCKET client_pair(SOCKET* peer) {
    int port;SOCKET server=listener(&port),client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons((u_short)port);
    assert(!connect(client,(struct sockaddr*)&address,sizeof(address)));*peer=accept(server,NULL,NULL);assert(*peer!=INVALID_SOCKET);closesocket(server);
    DWORD timeout=3000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));return client;
}
static unsigned __stdcall tunnel(void* data) {SOCKET client=*(SOCKET*)data;fm_connect_storage(client,allowed);closesocket(client);return 0;}
int main(void) {
    WSADATA data;assert(!WSAStartup(MAKEWORD(2,2),&data));char host[256];int port;
    strcpy_s(allowed,sizeof(allowed),"storage.example:443");
    const char* bad[]={"other.example:443","storage.example:0","storage.example:65536","storage.example:443/path","storage.example:443x","storage.example:000443","127.0.0.1:443"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!fm_connect_target(bad[i],host,&port));
    assert(fm_connect_target(allowed,host,&port) && port==443);denied=1;assert(!fm_connect_target(allowed,host,&port));denied=0;
    SOCKET peer,client=client_pair(&peer);public_target=0;fm_connect_storage(peer,allowed);
    char buffer[256];int received=recv(client,buffer,sizeof(buffer)-1,0);assert(received>0);buffer[received]=0;assert(strstr(buffer,"502"));closesocket(peer);closesocket(client);public_target=1;
    SOCKET upstream_listener=listener(&port);sprintf_s(allowed,sizeof(allowed),"storage.example:%d",port);
    client=client_pair(&peer);HANDLE worker=(HANDLE)_beginthreadex(NULL,0,tunnel,&peer,0,NULL);assert(worker);
    SOCKET upstream=accept(upstream_listener,NULL,NULL);assert(upstream!=INVALID_SOCKET);
    received=recv(client,buffer,sizeof(buffer)-1,0);assert(received>0);buffer[received]=0;assert(strstr(buffer,"200 Connection Established"));
    assert(send(client,"upload",6,0)==6);assert(recv(upstream,buffer,sizeof(buffer),0)==6 && !memcmp(buffer,"upload",6));
    assert(send(upstream,"download",8,0)==8);assert(recv(client,buffer,sizeof(buffer),0)==8 && !memcmp(buffer,"download",8));
    denied=1;assert(WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);assert(recv(client,buffer,sizeof(buffer),0)==0);
    CloseHandle(worker);closesocket(client);closesocket(upstream);closesocket(upstream_listener);WSACleanup();
    puts("FM CONNECT: restricted target, private destination refusal, duplex bytes, active deny closure passed");return 0;
}
