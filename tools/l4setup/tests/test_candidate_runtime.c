/* Optional local stand gate: needs an accessible real LocalMachine terminal
 * certificate. Uses only candidate loopback sockets; no MQTT CONNECT is sent. */
#include <winsock2.h>
#include "../../l4common/child_probe.h"
#include "../../l4common/proxy_certificate.h"
#include "../src/proxy_probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sddl.h>
static unsigned checks,failures;static HANDLE observed;
static L4ServiceToken* service_token;
static const wchar_t* canary=L"L4UPDATE_TOKEN_ENV_CANARY";
static char selected_thumbprint[64];static L4ProxyCertificate original_profile;
#define CHECK(x) do {++checks;if(!(x)){++failures;printf("FAIL %u %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
typedef struct {WORD http,mqtt;} Ports;
static bool run_probe(const wchar_t* exe,const wchar_t* directory,wchar_t* command,DWORD timeout,L4ChildCheck check,void* context) {
    return service_token?l4_child_probe_service(service_token,exe,directory,command,timeout,check,context):
        l4_child_probe(exe,directory,command,timeout,check,context);
}
static SOCKET bind_port(WORD* port) {
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);BOOL exclusive=TRUE;
    struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);int size=sizeof(a);
    if(s==INVALID_SOCKET)return s;
    bool bound=false;
    if(!setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(char*)&exclusive,sizeof(exclusive)))
        for(unsigned attempt=0;attempt<128;attempt++) {
            a.sin_port=htons((WORD)(49152+(GetTickCount64()+attempt)%16384));
            if(!bind(s,(struct sockaddr*)&a,sizeof(a))){bound=true;break;}
        }
    if(!bound || listen(s,1) || getsockname(s,(struct sockaddr*)&a,&size)){closesocket(s);return INVALID_SOCKET;}
    *port=ntohs(a.sin_port);return s;
}
static bool request(WORD port,const char* text,bool http) {
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET)return false;
    DWORD timeout=1000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));
    setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(char*)&timeout,sizeof(timeout));
    struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);
    bool ok=!connect(s,(struct sockaddr*)&a,sizeof(a));char response[512]={0};
    if(ok && text)ok=send(s,text,(int)strlen(text),0)>0;
    if(ok){int n=recv(s,response,sizeof(response)-1,0);ok=http?(n>12 && strstr(response," 403 ")!=NULL):(n==0 || (n<0 && WSAGetLastError()==WSAECONNRESET));}
    closesocket(s);return ok;
}
static bool check_candidate(HANDLE process,DWORD pid,ULONGLONG deadline,void* context) {
    Ports* ports=context;CHECK(DuplicateHandle(GetCurrentProcess(),process,GetCurrentProcess(),&observed,SYNCHRONIZE,FALSE,0));
    if(service_token) {
        HANDLE token=NULL;BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD size,session=99;
        CHECK(OpenProcessToken(process,TOKEN_QUERY,&token));
        if(token){CHECK(GetTokenInformation(token,TokenUser,user,sizeof(user),&size));CHECK(IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid));
            CHECK(GetTokenInformation(token,TokenSessionId,&session,sizeof(session),&size));CHECK(session==0);CloseHandle(token);}
    }
    bool ready=false;
    while(GetTickCount64()<deadline && WaitForSingleObject(process,0)==WAIT_TIMEOUT) {
        if(l4_child_listeners(process,pid,ports->http,ports->mqtt) && setup_proxy_probe(ports->http,true,200)){ready=true;break;}
        Sleep(20);
    }
    if(!ready)return false;
    CHECK(setup_proxy_probe_thumbprint(ports->http,200,selected_thumbprint));
    if(*original_profile.thumbprint)CHECK(setup_proxy_probe_certificate(ports->http,200,original_profile.thumbprint));
    CHECK(!setup_proxy_probe_certificate(ports->http,200,"0000000000000000000000000000000000000000"));
    CHECK(request(ports->http,"CONNECT forbidden.invalid:443 HTTP/1.1\r\nHost: forbidden.invalid\r\n\r\n",true));
    CHECK(request(ports->http,"GET /api/payment HTTP/1.1\r\nHost: localhost\r\n\r\n",true));
    CHECK(request(ports->mqtt,NULL,false));
    CHECK(l4_child_listeners(process,pid,ports->http,ports->mqtt));return !failures;
}
static bool check_rejected_arguments(HANDLE process,DWORD pid,ULONGLONG deadline,void* context) {
    (void)pid;DWORD* code=context;ULONGLONG now=GetTickCount64();
    if(now<deadline && WaitForSingleObject(process,(DWORD)(deadline-now))==WAIT_OBJECT_0)CHECK(GetExitCodeProcess(process,code));
    return false;
}
static bool environment_check(HANDLE process,DWORD pid,ULONGLONG deadline,void* context) {
    (void)process;(void)pid;ULONGLONG now=GetTickCount64();
    return now<deadline && WaitForSingleObject((HANDLE)context,(DWORD)(deadline-now))==WAIT_OBJECT_0;
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3 && !wcscmp(argv[1],L"--environment-probe")) {
        HANDLE primary=NULL;BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD size,session=99;
        bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&primary)!=0;
        if(ok)ok=GetTokenInformation(primary,TokenUser,user,sizeof(user),&size)!=0 &&
            IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid) &&
            GetTokenInformation(primary,TokenSessionId,&session,sizeof(session),&size) && session==0;
        if(primary)CloseHandle(primary);wchar_t value[4];SetLastError(ERROR_SUCCESS);
        if(ok)ok=!GetEnvironmentVariableW(canary,value,4) && GetLastError()==ERROR_ENVVAR_NOT_FOUND;
        HANDLE event=ok?OpenEventW(EVENT_MODIFY_STATE,FALSE,argv[2]):NULL;
        if(!event || !SetEvent(event))return 7;CloseHandle(event);Sleep(INFINITE);return 0;
    }
    if(argc!=2 && !(argc==3 && !wcscmp(argv[2],L"--system")))return 2;
    HANDLE parent_token=NULL;BYTE before_privileges[4096],after_privileges[4096];DWORD before_size=0,after_size=0;
    CHECK(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&parent_token));
    CHECK(GetTokenInformation(parent_token,TokenPrivileges,before_privileges,sizeof(before_privileges),&before_size));
    original_profile.machine=true;
    if(argc==3){L4ServiceInventory expected;CHECK(l4_service_inventory(L"Leo4Proxy",&expected));CHECK(l4_proxy_certificate_source(expected.image_path,&original_profile));CHECK(l4_service_token_capture(&expected,&service_token));if(!service_token)return 1;}
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa))return 1;
    Ports ports={0};SOCKET a=bind_port(&ports.http),b=bind_port(&ports.mqtt);
    CHECK(a!=INVALID_SOCKET && b!=INVALID_SOCKET && ports.http>=49152 && ports.mqtt>=49152);
    if(failures)return 1;closesocket(a);closesocket(b);
    wchar_t directory[MAX_PATH],command[2048];wcscpy_s(directory,MAX_PATH,argv[1]);wchar_t* slash=wcsrchr(directory,L'\\');if(!slash)return 2;*slash=0;
    CHECK(l4_proxy_certificate_command(argv[1],&original_profile,ports.http,ports.mqtt,10000,command));
    CHECK(run_probe(argv[1],directory,command,10000,check_candidate,&ports));
    CHECK(observed && WaitForSingleObject(observed,0)==WAIT_OBJECT_0);if(observed)CloseHandle(observed);observed=NULL;
    /* Explicit current thumbprint overrides a deliberately nonmatching email;
     * verify both candidate selection and health fingerprint, under service token. */
    strcpy_s(original_profile.thumbprint,64,selected_thumbprint);strcpy_s(original_profile.email,256,"l4update-no-such-certificate.invalid");
    CHECK(l4_proxy_certificate_command(argv[1],&original_profile,ports.http,ports.mqtt,10000,command));
    CHECK(run_probe(argv[1],directory,command,10000,check_candidate,&ports));
    CHECK(observed && WaitForSingleObject(observed,0)==WAIT_OBJECT_0);if(observed)CloseHandle(observed);observed=NULL;
    strcpy_s(original_profile.thumbprint,64,"0000000000000000000000000000000000000000");
    CHECK(l4_proxy_certificate_command(argv[1],&original_profile,ports.http,ports.mqtt,1000,command));DWORD no_certificate=STILL_ACTIVE;
    CHECK(!run_probe(argv[1],directory,command,3000,check_rejected_arguments,&no_certificate));CHECK(no_certificate==3);
    *original_profile.thumbprint=0;
    CHECK(l4_proxy_certificate_command(argv[1],&original_profile,ports.http,ports.mqtt,1000,command));no_certificate=STILL_ACTIVE;
    CHECK(!run_probe(argv[1],directory,command,3000,check_rejected_arguments,&no_certificate));CHECK(no_certificate==3);
    a=bind_port(&ports.http);b=bind_port(&ports.mqtt);CHECK(a!=INVALID_SOCKET && b!=INVALID_SOCKET);
    /* The HTTP port belongs to this test, candidate must refuse exclusive bind. */
    closesocket(b);swprintf_s(command,512,L"\"%ls\" --update-probe %u %u 1000 --version",argv[1],(unsigned)ports.http,(unsigned)ports.mqtt);
    CHECK(!run_probe(argv[1],directory,command,1000,check_candidate,&ports));
    CHECK(observed && WaitForSingleObject(observed,0)==WAIT_OBJECT_0);if(observed)CloseHandle(observed);
    closesocket(a);
    const wchar_t* invalid[]={L"--update-probe 59471 59472 100",L"--update-probe 59471 59471 100 --version",
        L"--update-probe 18883 59472 100 --version",L"--update-probe 59471 59472 99 --version",
        L"--update-probe 59471 59472 100 --version --install",L"--install --update-probe 59471 59472 100 --version"};
    for(unsigned i=0;i<_countof(invalid);i++) {
        DWORD code=STILL_ACTIVE;swprintf_s(command,512,L"\"%ls\" %ls",argv[1],invalid[i]);
        CHECK(!run_probe(argv[1],directory,command,3000,check_rejected_arguments,&code));CHECK(code==2);
    }
    if(service_token) {
        wchar_t self[MAX_PATH],name[128];CHECK(GetModuleFileNameW(NULL,self,MAX_PATH));
        swprintf_s(name,128,L"Global\\L4TokenEnv-%lu-%llu",GetCurrentProcessId(),GetTickCount64());
        PSECURITY_DESCRIPTOR sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",SDDL_REVISION_1,&sd,NULL));
        SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};HANDLE event=CreateEventW(&sa,TRUE,FALSE,name);CHECK(event!=NULL);LocalFree(sd);
        CHECK(SetEnvironmentVariableW(canary,L"synthetic-non-secret"));
        wcscpy_s(directory,MAX_PATH,self);slash=wcsrchr(directory,L'\\');*slash=0;
        swprintf_s(command,512,L"\"%ls\" --environment-probe \"%ls\"",self,name);
        CHECK(run_probe(self,directory,command,5000,environment_check,event));
        CHECK(SetEnvironmentVariableW(canary,NULL));if(event)CloseHandle(event);
        HANDLE thread=NULL;CHECK(!OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread) && GetLastError()==ERROR_NO_TOKEN);
        if(thread)CloseHandle(thread);CHECK(l4_service_token_verify(service_token));
    }
    CHECK(GetTokenInformation(parent_token,TokenPrivileges,after_privileges,sizeof(after_privileges),&after_size));
    CHECK(before_size==after_size && !memcmp(before_privileges,after_privileges,before_size));CloseHandle(parent_token);
    WSACleanup();l4_service_token_close(service_token);printf("actual candidate runtime: %u passed, %u failed (%s)\n",checks-failures,failures,argc==3?"LocalSystem":"caller");return failures?1:0;
}
