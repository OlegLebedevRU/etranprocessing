#include <winsock2.h>
#include <ws2tcpip.h>
#include "../src/communication_signals.h"
#include "../../l4common/probe_ipc.h"
#include "../../l4common/switch_decode.h"
#include <stdio.h>
#include <string.h>
#include <iphlpapi.h>
static volatile LONG checks,failures;static unsigned ipc_calls;static bool ipc_ok=true,con_changed=false,wildcard_fixture=false;static WORD fixture_port;
#define CHECK(x) do{InterlockedIncrement(&checks);if(!(x)){InterlockedIncrement(&failures);printf("FAIL signals %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static DWORD fake_table(PVOID bytes,PDWORD size,BOOL order,ULONG family,TCP_TABLE_CLASS type,ULONG reserved){
    DWORD result=GetExtendedTcpTable(bytes,size,order,family,type,reserved);
    if(!result && bytes && wildcard_fixture){MIB_TCPTABLE_OWNER_PID* table=bytes;
        for(DWORD i=0;i<table->dwNumEntries;i++)if(ntohs((u_short)table->table[i].dwLocalPort)==fixture_port)table->table[i].dwLocalAddr=INADDR_ANY;}
    return result;
}
static L4CommunicationPlan factory_plan;static bool factory_fixture;static unsigned factory_fault,boot_checks,pin_calls;static L4Layout factory_roots;static wchar_t factory_con[MAX_PATH],factory_command[2048];
static unsigned startup_case,startup_queries;static ULONGLONG startup_begin,startup_deadline;
static const L4CommunicationPlan* fake_plan(const L4CommunicationPin* p){(void)p;return factory_fixture?&factory_plan:NULL;}
static bool fake_switch(const L4Layout* roots,const BYTE* bytes,DWORD size,L4ServiceSwitch* s){(void)size;CHECK(roots && bytes);memset(s,0,sizeof(*s));
    unsigned i=(unsigned)(ULONG_PTR)bytes-1;CHECK(i<3);const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con"};const wchar_t* files[]={L"leo4proxy.exe",L"mosquitto.exe",L"l4con.exe"};
    L4Layout source;CHECK(l4_layout_from_roots(&source,roots->binaries,roots->data,L"1.13.2"));wchar_t path[MAX_PATH];CHECK(l4_layout_component(&source,components[i],files[i],path));
    s->before.installed=true;s->before.start_type=SERVICE_AUTO_START;wcscpy_s(s->before.account,256,L"LocalSystem");s->before_size=100;memset(s->before_sha256,1,32);
    swprintf_s(s->before.image_path,2048,L"\"%ls\" --service",path);if(i==2)wcscpy_s(factory_command,2048,s->before.image_path);
    if(factory_fault==7 && i==0)wcscat_s(s->before.image_path,2048,L" --cert-thumbprint 2222222222222222222222222222222222222222");return true;
}
static bool fake_pin(const L4Layout* source,const L4ReleaseFile* file,L4ReleaseFence** f,wchar_t path[MAX_PATH]){
    ++pin_calls;CHECK(file->size==100 && file->sha256[0]==1 && !wcscmp(file->component,L"l4con"));if(factory_fault==1){SetLastError(ERROR_CRC);return false;}
    *f=(L4ReleaseFence*)1;return l4_layout_component(source,file->component,file->file,path);
}
static void fake_unpin(L4ReleaseFence* f){(void)f;}
static bool fake_matches(const L4CommunicationPin* p,const L4UpdateState* s){CHECK(p && s);return true;}
static bool fake_boot_check(L4CommunicationBoot* b,const L4Layout* r,const L4UpdateState* s){++boot_checks;CHECK(b && r && s);return factory_fault!=6 && !(startup_case==8 && startup_queries);}
static DWORD fake_boot_remaining(const L4CommunicationBoot* b){CHECK(b);if(startup_case){ULONGLONG now=GetTickCount64();return now<startup_deadline?(DWORD)(startup_deadline-now):0;}return factory_fault==9?0:1000;}
static BOOL fake_image(HANDLE process,DWORD flags,LPWSTR image,PDWORD size){
    if(!factory_fixture)return QueryFullProcessImageNameW(process,flags,image,size);wcscpy_s(image,*size,factory_fault==3?L"wrong":factory_con);return TRUE;
}
static BOOL fake_sid(PSID sid,WELL_KNOWN_SID_TYPE type){(void)sid;CHECK(type==WinLocalSystemSid);return factory_fault!=4;}
static HANDLE fake_process(DWORD access,BOOL inherit,DWORD pid){
    if(factory_fixture && pid==900001){if(factory_fault==8)return OpenProcess(access,inherit,GetCurrentProcessId());SetLastError(ERROR_INVALID_PARAMETER);return NULL;}
    return OpenProcess(access,inherit,pid);
}
static bool fake_ipc(const wchar_t* n,DWORD pid,DWORD mode,DWORD ms){CHECK(!wcscmp(n,L"con") && pid==GetCurrentProcessId() && mode==1 && ms);++ipc_calls;return ipc_ok;}
static bool fake_barrier(DWORD pid,DWORD ms){return fake_ipc(L"con",pid,1,ms);}
static SC_HANDLE fake_manager(LPCWSTR a,LPCWSTR b,DWORD mask){(void)a;(void)b;CHECK(mask==SC_MANAGER_CONNECT);return (SC_HANDLE)1;}
static SC_HANDLE fake_service(SC_HANDLE m,LPCWSTR n,DWORD mask){(void)m;CHECK(!wcscmp(n,L"L4Con") && mask==(SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS));if(startup_case==9){SetLastError(ERROR_SERVICE_DOES_NOT_EXIST);return NULL;}return (SC_HANDLE)2;}
static BOOL fake_close(SC_HANDLE s){(void)s;return TRUE;}
static BOOL fake_config(SC_HANDLE s,LPQUERY_SERVICE_CONFIGW c,DWORD n,LPDWORD needed){(void)s;*needed=sizeof(*c);if(!c || n<*needed){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    memset(c,0,sizeof(*c));c->dwServiceType=SERVICE_WIN32_OWN_PROCESS;c->dwStartType=SERVICE_AUTO_START;c->lpServiceStartName=L"LocalSystem";c->lpBinaryPathName=factory_fixture?(factory_fault==2 || (startup_case==7 && startup_queries)?L"wrong":factory_command):L"old-con";return TRUE;}
static BOOL fake_status(SC_HANDLE s,SC_STATUS_TYPE t,LPBYTE b,DWORD n,LPDWORD needed){(void)s;(void)t;CHECK(n==sizeof(SERVICE_STATUS_PROCESS));SERVICE_STATUS_PROCESS p={0};p.dwCurrentState=SERVICE_RUNNING;p.dwProcessId=GetCurrentProcessId()+(con_changed?1:0);
    if(startup_case){++startup_queries;ULONGLONG elapsed=GetTickCount64()-startup_begin;
        if(startup_case==1 && elapsed<60)p.dwCurrentState=SERVICE_START_PENDING;
        if(startup_case==2 && elapsed<80)p.dwCurrentState=elapsed<30?SERVICE_STOPPED:SERVICE_START_PENDING;
        if(startup_case>=3)p.dwCurrentState=SERVICE_START_PENDING;
        if(startup_case==4)p.dwCurrentState=SERVICE_STOPPED;
        if(startup_case==5){p.dwCurrentState=SERVICE_STOPPED;p.dwWin32ExitCode=ERROR_SERVICE_SPECIFIC_ERROR;p.dwServiceSpecificExitCode=42;}
        if(startup_case==6)p.dwCurrentState=SERVICE_STOP_PENDING;
        if(p.dwCurrentState!=SERVICE_RUNNING)p.dwProcessId=0;
    }memcpy(b,&p,sizeof(p));*needed=sizeof(p);return TRUE;}
#define l4_communication_pinned_plan fake_plan
#define l4_switch_decode_bytes fake_switch
#define l4_release_pin fake_pin
#define l4_release_unpin fake_unpin
#define l4_communication_plan_matches fake_matches
#define l4_communication_boot_check fake_boot_check
#define l4_communication_boot_remaining fake_boot_remaining
#define QueryFullProcessImageNameW fake_image
#define IsWellKnownSid fake_sid
#define OpenProcess fake_process
#define l4_probe_call fake_ipc
#define l4_probe_barrier_call fake_barrier
#define OpenSCManagerW fake_manager
#define OpenServiceW fake_service
#define CloseServiceHandle fake_close
#define QueryServiceConfigW fake_config
#define QueryServiceStatusEx fake_status
#define GetExtendedTcpTable fake_table
#include "../src/communication_signals.c"
static const char thumb[]="1111111111111111111111111111111111111111";
static void json(L4SignalProfile* p,char* b,size_t n){unsigned long long now=l4_proxy_policy_utc();sprintf_s(b,n,"{\"status\":\"ready\",\"certificate_found\":true,\"has_private_key\":true,\"routes_active\":true,\"sn\":\"773\",\"thumbprint\":\"%s\",\"listeners\":{\"http_local\":\"127.0.0.1:%u\",\"mqtt_local\":\"127.0.0.1:%u\"},\"policy\":{\"mqtt_rtp_allowed\":true,\"outgoing_https_allowed\":true,\"storage_pending\":false,\"last_success_at\":%llu,\"offline_allowed_until\":%llu,\"generation\":1,\"stop_facts\":\"\",\"last_error\":\"\"}}",thumb,p->http,p->mqtt,now,now+259200ULL);}
typedef struct{SOCKET server;HANDLE thread;const char* body;unsigned bytes;int mode;} Server;
static SOCKET bind_local(WORD* p){SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(s!=INVALID_SOCKET);BOOL exclusive=TRUE;CHECK(!setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(char*)&exclusive,sizeof(exclusive)));struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);CHECK(!bind(s,(struct sockaddr*)&a,sizeof(a)));int n=sizeof(a);CHECK(!getsockname(s,(struct sockaddr*)&a,&n));*p=ntohs(a.sin_port);CHECK(!listen(s,8));return s;}
static DWORD WINAPI serve(void* context){Server* p=context;SOCKET c=accept(p->server,NULL,NULL);CHECK(c!=INVALID_SOCKET);if(c==INVALID_SOCKET)return 1;
    char request[1024];int n=recv(c,request,sizeof(request)-1,0);if(n>0){p->bytes=(unsigned)n;request[n]=0;CHECK(strstr(request,"GET /_leo4/info HTTP/1.1\r\n")!=NULL);
        char response[2048];int len=sprintf_s(response,sizeof(response),p->mode==1?"HTTP/1.1 302 Found\r\nContent-Length: %zu\r\n\r\n%s":p->mode==2?"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: %zu\r\n\r\n%s":"HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s",strlen(p->body),p->body);CHECK(send(c,response,len,0)==len);
    }else CHECK(n==0);closesocket(c);return 0;}
static void run_info(L4SignalProfile* p,const char* body,int mode,bool expected){Server s={bind_local(&p->http),NULL,body,0,mode};char correct[1024];if(!body){json(p,correct,sizeof(correct));s.body=correct;}s.thread=CreateThread(NULL,0,serve,&s,0,NULL);CHECK(s.thread);CHECK(info(p,GetCurrentProcessId(),GetTickCount64()+2000,false)==expected);CHECK(WaitForSingleObject(s.thread,3000)==WAIT_OBJECT_0);CloseHandle(s.thread);closesocket(s.server);}
static void parser_tests(void){L4SignalProfile p={0};p.http=18443;p.mqtt=18883;char body[1024];json(&p,body,sizeof(body));CHECK(info_parse(&p,body,strlen(body),true));CHECK(!strcmp(p.sn,"773") && !strcmp(p.thumbprint,thumb));
    strcpy_s(p.sn,64,"another");CHECK(!info_parse(&p,body,strlen(body),false));p.sn[0]=0;strcpy_s(p.thumbprint,64,"2222222222222222222222222222222222222222");CHECK(!info_parse(&p,body,strlen(body),false));p.thumbprint[0]=0;p.http++;CHECK(!info_parse(&p,body,strlen(body),false));p.http--;
    const char duplicate[]="{\"status\":\"ready\",\"status\":\"ready\"}";CHECK(!info_parse(&p,duplicate,strlen(duplicate),false));CHECK(!info_parse(&p,body,strlen(body)-1,false));CHECK(!info_parse(&p,"{}",2,false));
    const char* fields[]={"certificate_found","has_private_key","routes_active"};for(unsigned i=0;i<3;i++){json(&p,body,sizeof(body));char* value=strstr(body,fields[i]);CHECK(value);value=strchr(value,':')+1;memcpy(value,"null",4);CHECK(!info_parse(&p,body,strlen(body),false));}
    const char* policy_fields[]={"mqtt_rtp_allowed","outgoing_https_allowed","storage_pending","last_success_at","offline_allowed_until","generation","stop_facts"};
    for(unsigned i=0;i<_countof(policy_fields);i++){json(&p,body,sizeof(body));char* value=strstr(body,policy_fields[i]);CHECK(value);value=strchr(value,':')+1;memcpy(value,"null",4);CHECK(!info_parse(&p,body,strlen(body),false));}
    WORD h=0,m=0;char expected[64];CHECK(l4_proxy_signal_source(L"\"C:\\source.exe\" --service",&h,&m,expected) && h==18443 && m==18883);
    CHECK(l4_proxy_signal_source(L"\"C:\\source.exe\" --http-local 127.0.0.1:19000 --http-local 127.0.0.1:19001 --mqtt-local 127.0.0.1:19002",&h,&m,expected) && h==19001 && m==19002);
    CHECK(l4_proxy_signal_source(L"\"C:\\source.exe\" --cert-email --http-local --service",&h,&m,expected) && h==18443);
    const wchar_t* bad[]={L"--http-local 0.0.0.0:19000",L"--http-local 127.0.0.1:0",L"--http-local 127.0.0.1:65536",L"--http-local 127.0.0.1:18883",L"--local-ssl",L"--http-local-ssl",L"--unknown",L"--mqtt-local",L"--http-local localhost:19000"};
    for(unsigned i=0;i<_countof(bad);i++){wchar_t cmd[256];swprintf_s(cmd,256,L"\"C:\\source.exe\" %ls",bad[i]);CHECK(!l4_proxy_signal_source(cmd,&h,&m,expected));}
    const char* config[]={"listener 1883 127.0.0.1\nconnection bridge\naddress 127.0.0.1:18883\n","listener 1883 0.0.0.0\nconnection bridge\naddress 127.0.0.1:18883\n","listener 1883 127.0.0.1\nconnection bridge\naddress 127.0.0.1:8883\n","listener 1883 127.0.0.1\nconnection bridge\naddress 127.0.0.1:18883\ninclude_dir elsewhere\n","listener 1883 127.0.0.1\nlistener 1884 127.0.0.1\nconnection bridge\naddress 127.0.0.1:18883\n","listener 1883 127.0.0.1\nlistener\t1884 127.0.0.1\nconnection bridge\naddress 127.0.0.1:18883\n","listener 1883 127.0.0.1\nconnection bridge\naddress 127.0.0.1:18883\nport\t8883\n"};
    for(unsigned i=0;i<_countof(config);i++){BYTE r[1024]={0};l4_store_u32(r+12,(DWORD)strlen(config[i]));memcpy(r+24,config[i],strlen(config[i]));WORD b=0;CHECK(broker_port(r,sizeof(r),18883,&b)==(i==0));if(!i)CHECK(b==1883);}
    BYTE malformed[24]={0};l4_store_u32(malformed+12,1024);CHECK(!broker_port(malformed,24,18883,&h));
}
static void boot_factory_tests(void){
    CHECK(l4_layout_from_roots(&factory_roots,L"C:\\SignalFixturePF",L"C:\\SignalFixtureData",L"1.13.2"));
    CHECK(l4_layout_component(&factory_roots,L"l4con",L"l4con.exe",factory_con));factory_fixture=true;
    memset(&factory_plan,0,sizeof(factory_plan));factory_plan.switches[0]=(BYTE*)1;factory_plan.switches[1]=(BYTE*)2;
    factory_plan.switch_size[0]=1;factory_plan.switch_size[1]=1;factory_plan.con_switch=(BYTE*)3;factory_plan.con_size=1;
    factory_plan.con_pid=GetCurrentProcessId();FILETIME e,k,u;CHECK(GetProcessTimes(GetCurrentProcess(),&factory_plan.con_created,&e,&k,&u));
    strcpy_s(factory_plan.thumbprint,64,thumb);const char* config="listener 1883 127.0.0.1\nconnection bridge\naddress 127.0.0.1:18883\n";BYTE record[1024]={0};l4_store_u32(record+12,(DWORD)strlen(config));memcpy(record+24,config,strlen(config));
    factory_plan.configs[0]=record;factory_plan.config_size[0]=sizeof(record);L4UpdateState expected={0};L4SignalProfile* profile=NULL;L4CommunicationSignals signals;unsigned calls=ipc_calls;
    /* No proxy/broker listener exists at these ports in this fixture. Positive
     * boot acquisition must make no HTTP/TCP/Con-IPC preflight at all. */
    CHECK(supervisor_signals_open_boot(&factory_roots,(L4CommunicationPin*)1,(L4CommunicationBoot*)1,&expected,&profile,&signals));
    CHECK(profile && signals.context==profile && signals.probe && signals.channels && signals.barrier && ipc_calls==calls && boot_checks>=2 && pin_calls);
    CHECK(!strcmp(profile->thumbprint,thumb));CHECK(barrier(1000,profile) && ipc_calls==calls+1);supervisor_signals_close(profile);profile=NULL;
    for(unsigned fault=1;fault<=9;fault++){if(fault==5 || fault==8)continue;factory_fault=fault;CHECK(!supervisor_signals_open_boot(&factory_roots,(L4CommunicationPin*)1,(L4CommunicationBoot*)1,&expected,&profile,&signals) && !profile && !signals.context);}
    factory_fault=0;factory_plan.con_created.dwLowDateTime^=1;
    CHECK(!supervisor_signals_open(&factory_roots,(L4CommunicationPin*)1,1000,&profile,&signals) && !profile);factory_plan.con_created.dwLowDateTime^=1;
    factory_plan.con_pid=900001;factory_fault=8;CHECK(!supervisor_signals_open_boot(&factory_roots,(L4CommunicationPin*)1,(L4CommunicationBoot*)1,&expected,&profile,&signals) && !profile);
    factory_fault=0;CHECK(supervisor_signals_open_boot(&factory_roots,(L4CommunicationPin*)1,(L4CommunicationBoot*)1,&expected,&profile,&signals) && profile);supervisor_signals_close(profile);
    CHECK(!supervisor_signals_open_boot(&factory_roots,(L4CommunicationPin*)1,NULL,&expected,&profile,&signals) && !profile && !signals.context);
    factory_plan.con_pid=GetCurrentProcessId();profile=NULL;
    for(unsigned scenario=1;scenario<=9;scenario++){
        startup_case=scenario;startup_queries=0;startup_begin=GetTickCount64();startup_deadline=startup_begin+180;
        bool accepted=supervisor_signals_open_boot(&factory_roots,(L4CommunicationPin*)1,(L4CommunicationBoot*)1,&expected,&profile,&signals);
        CHECK(accepted==(scenario<=2));CHECK(GetTickCount64()-startup_begin<1000);
        if(scenario<=2){CHECK(profile && startup_queries>=3 && GetTickCount64()-startup_begin>=50);supervisor_signals_close(profile);profile=NULL;}
        else CHECK(!profile && !signals.context);
        if(scenario==3 || scenario==4)CHECK(GetLastError()==ERROR_TIMEOUT && startup_queries>=3);
    }
    startup_case=0;CHECK(ipc_calls==calls+1); /* Waiting emits no IPC or MQTT. */
    factory_fixture=false;factory_fault=0;
}
int main(void){WSADATA wsa;CHECK(!WSAStartup(MAKEWORD(2,2),&wsa));parser_tests();L4SignalProfile p={0};SOCKET mqtt=bind_local(&p.mqtt);CHECK(listener(GetCurrentProcessId(),p.mqtt));CHECK(!listener(GetCurrentProcessId()+1,p.mqtt));
    fixture_port=p.mqtt;wildcard_fixture=true;CHECK(!listener(GetCurrentProcessId(),p.mqtt));wildcard_fixture=false;
    run_info(&p,NULL,0,true);run_info(&p,"{}",0,false);run_info(&p,"{}",1,false);run_info(&p,"{}",2,false);
    Server raw={mqtt,NULL,NULL,0,0};raw.thread=CreateThread(NULL,0,serve,&raw,0,NULL);CHECK(raw.thread);CHECK(tcp(GetCurrentProcessId(),p.mqtt,GetTickCount64()+1000));CHECK(WaitForSingleObject(raw.thread,2000)==WAIT_OBJECT_0 && !raw.bytes);CloseHandle(raw.thread);closesocket(mqtt);CHECK(!tcp(GetCurrentProcessId(),p.mqtt,GetTickCount64()+100));
    p.con.installed=true;p.con.start_type=SERVICE_AUTO_START;wcscpy_s(p.con.image_path,2048,L"old-con");p.con_pid=GetCurrentProcessId();p.con_process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,p.con_pid);FILETIME e,k,u;CHECK(p.con_process && GetProcessTimes(p.con_process,&p.con_created,&e,&k,&u));
    CHECK(barrier(1000,&p) && ipc_calls==1);ipc_ok=false;CHECK(!barrier(1000,&p) && ipc_calls==2);ipc_ok=true;con_changed=true;CHECK(!barrier(1000,&p) && ipc_calls==2);con_changed=false;p.con_created.dwLowDateTime^=1;CHECK(!barrier(1000,&p) && ipc_calls==2);CloseHandle(p.con_process);
    boot_factory_tests();L4SignalProfile* out=(L4SignalProfile*)1;L4CommunicationSignals s={0};CHECK(!supervisor_signals_open(NULL,NULL,1000,&out,&s) && !out && !s.context);WSACleanup();
    printf("Communication signals: %ld passed, %ld failed; real loopback HTTP/TCP/PID ownership, zero MQTT bytes, modeled wildcard row/SCM/Con IPC; no broker/IoT/service mutation\n",checks-failures,failures);return failures?1:0;
}
