/* Real Schannel exchange on ephemeral loopback, with a temporary user KSP key.
 * No production certificate stores, services or external sockets are touched. */
#define main selection_fixture_main
#include "test_certificate_selection.c"
#undef main
#include "schannel_tls.h"
#include "credential_lifetime.h"
ProxyStats g_proxyStats={0};
bool policy_media_allowed(void) {return true;}
int policy_media_connect(PolicySocket* node,SOCKET socket,const struct sockaddr* address,int length) {
    (void)node;return connect(socket,address,length);
}
void policy_socket_unregister(PolicySocket* node) {(void)node;}
typedef struct {SOCKET listener;CredHandle credential;HANDLE ready,go;int result;} Peer;
static DWORD WINAPI stalled_peer(void* argument) {
    SOCKET socket=accept(*(SOCKET*)argument,NULL,NULL);
    if(socket==INVALID_SOCKET) return 1;
    Sleep(200);closesocket(socket);return 0;
}
static DWORD WINAPI peer(void* argument) {
    Peer* state=(Peer*)argument;SOCKET socket=accept(state->listener,NULL,NULL);
    if(socket==INVALID_SOCKET) return 1;
    DWORD timeout=5000;setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof(timeout));
    setsockopt(socket,SOL_SOCKET,SO_SNDTIMEO,(const char*)&timeout,sizeof(timeout));
    SChannelSession session;
    if(!schannel_accept(&session,&state->credential,socket)) return 1;
    SetEvent(state->ready);
    if(WaitForSingleObject(state->go,10000)!=WAIT_OBJECT_0){schannel_close(&session);return 1;}
    char message[16];int size=schannel_recv(&session,message,sizeof(message));
    state->result=size==4 && !memcmp(message,"ping",4) && schannel_send(&session,"pong",4)==4;
    schannel_close(&session);return state->result?0:1;
}
int main(void) {
    WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data))return 1;
    NCRYPT_PROV_HANDLE provider=0;NCRYPT_KEY_HANDLE key=0;
    if(NCryptOpenStorageProvider(&provider,MS_KEY_STORAGE_PROVIDER,0))return 1;
    if(NCryptCreatePersistedKey(provider,&key,BCRYPT_RSA_ALGORITHM,key_name,0,0))return 1;
    DWORD bits=2048;NCryptSetProperty(key,NCRYPT_LENGTH_PROPERTY,(PBYTE)&bits,sizeof(bits),0);
    if(NCryptFinalizeKey(key,0))return 1;
    PCCERT_CONTEXT certificate=make_cert(key,"CN=iot.leo4.ru,E=773.terminal@leo4.ru",-1,30);
    CredHandle client,server;SecInvalidateHandle(&client);SecInvalidateHandle(&server);
    CHECK(certificate && schannel_init_client_creds(certificate,1,&client) && schannel_init_server_creds(certificate,&server));
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    CHECK(!bind(listener,(struct sockaddr*)&address,sizeof(address)));CHECK(!listen(listener,1));
    int address_size=sizeof(address);CHECK(!getsockname(listener,(struct sockaddr*)&address,&address_size));
    HANDLE stalled=CreateThread(NULL,0,stalled_peer,&listener,0,NULL);CHECK(stalled);
    SChannelSession timeout_session;ULONGLONG start=GetTickCount64();
    CHECK(!schannel_connect_endpoint(&timeout_session,&client,"127.0.0.1","logical.example.com",ntohs(address.sin_port),50,false));
    CHECK(GetTickCount64()-start<300);CHECK(WaitForSingleObject(stalled,1000)==WAIT_OBJECT_0);CloseHandle(stalled);
    Peer state={listener,server,CreateEventW(NULL,TRUE,FALSE,NULL),CreateEventW(NULL,TRUE,FALSE,NULL),0};
    HANDLE thread=CreateThread(NULL,0,peer,&state,0,NULL);CHECK(thread);
    SChannelSession session;bool connected=schannel_connect(&session,&client,"127.0.0.1",ntohs(address.sin_port),5000,1);
    CHECK(connected);
    if(connected) {
        CHECK(WaitForSingleObject(state.ready,10000)==WAIT_OBJECT_0);
        schannel_free_creds(&client);schannel_free_creds(&server);
        CertFreeCertificateContext(certificate);certificate=NULL;
        SetEvent(state.go);
        CHECK(schannel_send(&session,"ping",4)==4);
        char reply[16];int size=schannel_recv(&session,reply,sizeof(reply));CHECK(size==4 && !memcmp(reply,"pong",4));
        schannel_close(&session);
    } else SetEvent(state.go);
    CHECK(WaitForSingleObject(thread,12000)==WAIT_OBJECT_0);CHECK(state.result);
    CloseHandle(thread);CloseHandle(state.ready);CloseHandle(state.go);closesocket(listener);
    schannel_free_creds(&client);schannel_free_creds(&server);
    if(certificate)CertFreeCertificateContext(certificate);
    NCryptDeleteKey(key,0);NCryptFreeObject(provider);WSACleanup();
    printf("Real loopback Schannel, stalled handshake deadline, retired owners: failures=%d\n",failed);return failed?1:0;
}
