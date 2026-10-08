#include <winsock2.h>
#include <ws2tcpip.h>
#include "communication_signals.h"
#include "../../l4common/proxy_certificate.h"
#include "../../l4common/switch_decode.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/probe_ipc.h"
#include "../../leo4proxy/src/policy_json.h"
#include "../../l4common/proxy_policy_gate.h"
#include <iphlpapi.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct L4SignalProfile {
    L4ServiceSwitch pair[2];L4ServiceInventory con;HANDLE con_process;FILETIME con_created;
    L4ReleaseFence* con_fence;DWORD con_pid;WORD http,mqtt,broker;char sn[64],thumbprint[64];wchar_t con_image[MAX_PATH];
};
static bool fail(DWORD error){SetLastError(error);return false;}
static DWORD left(ULONGLONG end){ULONGLONG t=GetTickCount64();return t<end?(DWORD)(end-t):0;}
static bool port(const wchar_t* text,WORD* output){unsigned value=0;if(!text || !*text)return false;
    for(const wchar_t* p=text;*p;p++){if(*p<L'0' || *p>L'9' || value>6553)return false;value=value*10+*p-L'0';}
    if(!value || value>65535)return false;*output=(WORD)value;return true;
}
static const char* directive(const char* line,const char* name){size_t n=strlen(name);
    if(strncmp(line,name,n) || (line[n] && line[n]!=' ' && line[n]!='\t'))return NULL;
    line+=n;while(*line==' ' || *line=='\t')line++;return line;
}
/* Saved old broker bytes, no external include or alternative bridge. This is
 * the fixed supported profile, not a general Mosquitto configuration parser. */
static bool broker_port(const BYTE* record,DWORD size,WORD mqtt,WORD* output){
    if(!record || size<24)return fail(ERROR_INVALID_DATA);DWORD n=l4_store_get32(record+8),old=l4_store_get32(record+12);
    if(old>65536 || 24ull+n+old>size)return fail(ERROR_INVALID_DATA);char* text=calloc(1,old+1);if(!text)return fail(ERROR_NOT_ENOUGH_MEMORY);
    memcpy(text,record+24+n,old);bool ok=!memchr(text,0,old);unsigned listeners=0,bridges=0,addresses=0;char expected[64];sprintf_s(expected,64,"127.0.0.1:%u",mqtt);
    for(char* line=text;ok && line && *line;){char* next=strchr(line,'\n');if(next)*next++=0;
        size_t len=strlen(line);if(len && line[len-1]=='\r')line[len-1]=0;while(*line==' ' || *line=='\t')line++;
        if(*line && *line!='#'){
            if(directive(line,"include_dir") || directive(line,"port") || directive(line,"bind_address")){ok=false;break;}
            const char* value_text=directive(line,"listener");
            if(value_text){char value[16],host[32],extra[8];
                ok=sscanf_s(value_text,"%15s %31s %7s",value,(unsigned)sizeof(value),host,(unsigned)sizeof(host),extra,(unsigned)sizeof(extra))==2 && !strcmp(host,"127.0.0.1");
                wchar_t number[16];if(ok){for(unsigned i=0;i<=strlen(value);i++)number[i]=(wchar_t)value[i];ok=port(number,output) && ++listeners==1;}}
            value_text=directive(line,"connection");if(value_text)ok=*value_text && ++bridges==1;
            value_text=directive(line,"address");if(value_text){char address[64],extra[8];ok=sscanf_s(value_text,"%63s %7s",address,(unsigned)sizeof(address),extra,(unsigned)sizeof(extra))==1 && !strcmp(address,expected) && ++addresses==1;}
        }line=next;
    }
    free(text);return ok && listeners==1 && bridges==1 && addresses==1 && *output!=mqtt?true:fail(ERROR_NOT_SUPPORTED);
}
static bool listener(DWORD pid,WORD p){
    DWORD size=0,status=GetExtendedTcpTable(NULL,&size,FALSE,AF_INET,TCP_TABLE_OWNER_PID_LISTENER,0);
    if(status!=ERROR_INSUFFICIENT_BUFFER || size<sizeof(DWORD) || size>1048576)return fail(ERROR_INVALID_DATA);
    MIB_TCPTABLE_OWNER_PID* table=malloc(size);if(!table)return fail(ERROR_NOT_ENOUGH_MEMORY);
    status=GetExtendedTcpTable(table,&size,FALSE,AF_INET,TCP_TABLE_OWNER_PID_LISTENER,0);bool ok=status==NO_ERROR,found=false;
    if(ok && table->dwNumEntries>(size-sizeof(DWORD))/sizeof(MIB_TCPROW_OWNER_PID))ok=false;
    for(DWORD i=0;ok && i<table->dwNumEntries;i++){MIB_TCPROW_OWNER_PID* row=table->table+i;
        if(ntohs((u_short)row->dwLocalPort)!=p)continue;
        if(found || row->dwLocalAddr!=htonl(INADDR_LOOPBACK) || row->dwOwningPid!=pid)ok=false;else found=true;}
    free(table);return ok && found?true:fail(ERROR_NOT_READY);
}
static bool socket_ready(SOCKET s,bool write,ULONGLONG end){DWORD ms=left(end);if(!ms)return fail(ERROR_TIMEOUT);
    fd_set ready,errors;FD_ZERO(&ready);FD_ZERO(&errors);FD_SET(s,&ready);FD_SET(s,&errors);struct timeval t={(long)(ms/1000),(long)(ms%1000)*1000};
    if(select(0,write?NULL:&ready,write?&ready:NULL,&errors,&t)<=0 || FD_ISSET(s,&errors))return fail(ERROR_NOT_READY);
    int error=0,len=sizeof(error);return !getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&error,&len) && !error?true:fail(ERROR_NOT_READY);
}
static SOCKET connect_local(WORD p,ULONGLONG end){SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET)return s;
    u_long mode=1;struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(p);
    bool ok=!ioctlsocket(s,FIONBIO,&mode);if(ok){int result=connect(s,(struct sockaddr*)&a,sizeof(a));ok=!result || (WSAGetLastError()==WSAEWOULDBLOCK && socket_ready(s,true,end));}
    if(!ok){closesocket(s);return INVALID_SOCKET;}return s;
}
static bool tcp(DWORD pid,WORD p,ULONGLONG end){if(!listener(pid,p))return false;SOCKET s=connect_local(p,end);if(s==INVALID_SOCKET)return fail(ERROR_NOT_READY);
    closesocket(s);return left(end) && listener(pid,p); /* No application bytes, no MQTT CONNECT/client ID. */
}
static bool info_parse(L4SignalProfile* p,const char* bytes,size_t size,bool capture){
    PolicyJson j;char status[32],sn[64],thumb[64],http[64],mqtt[64],expected[64];bool cert=false,key=false,routes=false;
    bool ok=policy_json_parse(&j,bytes,size) && policy_json_string(&j,policy_json_field(&j,0,"status"),status,sizeof(status)) && !strcmp(status,"ready") &&
        policy_json_bool(&j,policy_json_field(&j,0,"certificate_found"),&cert) && cert && policy_json_bool(&j,policy_json_field(&j,0,"has_private_key"),&key) && key &&
        policy_json_bool(&j,policy_json_field(&j,0,"routes_active"),&routes) && routes && policy_json_string(&j,policy_json_field(&j,0,"sn"),sn,sizeof(sn)) && *sn &&
        policy_json_string(&j,policy_json_field(&j,0,"thumbprint"),thumb,sizeof(thumb)) && strlen(thumb)==40;
    for(unsigned i=0;ok && i<40;i++){char c=thumb[i];ok=(c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F');}
    int l=ok?policy_json_field(&j,0,"listeners"):-1;
    if(ok)ok=policy_json_string(&j,policy_json_field(&j,l,"http_local"),http,sizeof(http)) && policy_json_string(&j,policy_json_field(&j,l,"mqtt_local"),mqtt,sizeof(mqtt));
    sprintf_s(expected,64,"127.0.0.1:%u",p->http);if(ok)ok=!strcmp(http,expected);sprintf_s(expected,64,"127.0.0.1:%u",p->mqtt);if(ok)ok=!strcmp(mqtt,expected);
    if(ok && *p->thumbprint)ok=!_stricmp(p->thumbprint,thumb);if(ok && *p->sn)ok=!strcmp(p->sn,sn);
    if(ok)ok=l4_proxy_policy_gate(&j,l4_proxy_policy_utc());
    if(ok && capture){strcpy_s(p->sn,64,sn);strcpy_s(p->thumbprint,64,thumb);}return ok?true:fail(ERROR_NOT_READY);
}
/* Bounded literal loopback HTTP; no DNS/system proxy/redirect, all I/O shares end. */
static bool info(L4SignalProfile* p,DWORD pid,ULONGLONG end,bool capture){
    if(!listener(pid,p->http) || !listener(pid,p->mqtt))return false;SOCKET s=connect_local(p->http,end);if(s==INVALID_SOCKET)return false;
    char request[192];int n=sprintf_s(request,192,"GET /_leo4/info HTTP/1.1\r\nHost: 127.0.0.1:%u\r\nConnection: close\r\n\r\n",p->http);bool ok=true;
    for(int at=0;ok && at<n;){ok=socket_ready(s,true,end);if(ok){int sent=send(s,request+at,n-at,0);ok=sent>0;if(ok)at+=sent;}}
    char bytes[24577]={0};size_t used=0,header=0,body=0;
    while(ok){if(used==sizeof(bytes)-1 || !socket_ready(s,false,end)){ok=false;break;}int got=recv(s,bytes+used,(int)(sizeof(bytes)-1-used),0);if(got<=0){ok=false;break;}used+=got;bytes[used]=0;
        if(!header){char* tail=strstr(bytes,"\r\n\r\n");if(!tail)continue;
            ok=!strncmp(bytes,"HTTP/1.1 200 ",13) || !strncmp(bytes,"HTTP/1.0 200 ",13);header=(size_t)(tail-bytes)+4;bool length=false;
            for(char* line=strstr(bytes,"\r\n");ok && line && line<tail;){line+=2;char* next=strstr(line,"\r\n");if(!next || next>tail){ok=false;break;}
                if(!_strnicmp(line,"Transfer-Encoding:",18))ok=false;
                if(!_strnicmp(line,"Content-Length:",15)){char* value=line+15;while(*value==' ' || *value=='\t')value++;char* stop=NULL;
                    if(length || *value<'0' || *value>'9'){ok=false;break;}unsigned long amount=strtoul(value,&stop,10);while(*stop==' ' || *stop=='\t')stop++;
                    ok=stop==next && amount<=16384;body=amount;length=true;}line=next;}
            ok=ok && length;}
        if(ok && used<header+body)continue;
        if(ok)ok=used==header+body && info_parse(p,bytes+header,body,capture);break;
    }
    DWORD error=GetLastError();closesocket(s);if(ok)ok=left(end) && listener(pid,p->http) && listener(pid,p->mqtt);return ok?true:fail(error?error:ERROR_NOT_READY);
}
static bool service_status(const wchar_t* name,const L4ServiceInventory* expected,SERVICE_STATUS_PROCESS* status){
    SC_HANDLE m=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!m)return false;SC_HANDLE s=OpenServiceW(m,name,SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS);DWORD code=GetLastError();CloseServiceHandle(m);if(!s)return fail(code);
    DWORD size=0;QueryServiceConfigW(s,NULL,0,&size);bool ok=GetLastError()==ERROR_INSUFFICIENT_BUFFER && size>=sizeof(QUERY_SERVICE_CONFIGW) && size<=65536;
    QUERY_SERVICE_CONFIGW* c=ok?malloc(size):NULL;if(ok && !c)ok=fail(ERROR_NOT_ENOUGH_MEMORY);
    if(ok)ok=QueryServiceConfigW(s,c,size,&size) && c->dwServiceType==SERVICE_WIN32_OWN_PROCESS && c->dwStartType==expected->start_type &&
        c->lpServiceStartName && !wcscmp(c->lpServiceStartName,L"LocalSystem") && c->lpBinaryPathName && !wcscmp(c->lpBinaryPathName,expected->image_path);
    if(ok)ok=QueryServiceStatusEx(s,SC_STATUS_PROCESS_INFO,(BYTE*)status,sizeof(*status),&size);
    code=GetLastError();free(c);CloseServiceHandle(s);return ok?true:fail(code?code:ERROR_REVISION_MISMATCH);
}
static bool service_pid(const wchar_t* name,const L4ServiceInventory* expected,DWORD* pid){
    SERVICE_STATUS_PROCESS status={0};if(!service_status(name,expected,&status))return false;
    if(status.dwCurrentState!=SERVICE_RUNNING || !status.dwProcessId)return fail(ERROR_NOT_READY);
    *pid=status.dwProcessId;return true;
}
/* Wait only for SCM's automatic source Con startup. No SCM mutation or new
 * reserve: every observation revalidates the exact saved config and admission.
 * Missing/failed/stopping services refuse instead of being repaired here. */
static bool con_startup(const L4Layout* roots,L4SignalProfile* p,L4CommunicationBoot* boot,const L4UpdateState* expected,ULONGLONG end){
    for(;;){
        DWORD ms=left(end),reserve=l4_communication_boot_remaining(boot);
        if(!ms || !reserve)return fail(ERROR_TIMEOUT);
        if(!l4_communication_boot_check(boot,roots,expected))return false;
        SERVICE_STATUS_PROCESS status={0};if(!service_status(L"L4Con",&p->con,&status))return false;
        if(status.dwCurrentState==SERVICE_RUNNING){if(!status.dwProcessId)return fail(ERROR_NOT_READY);p->con_pid=status.dwProcessId;return true;}
        if(p->con.start_type!=SERVICE_AUTO_START || status.dwWin32ExitCode || status.dwServiceSpecificExitCode ||
            (status.dwCurrentState!=SERVICE_START_PENDING && status.dwCurrentState!=SERVICE_STOPPED))return fail(ERROR_NOT_READY);
        ms=left(end);reserve=l4_communication_boot_remaining(boot);if(ms>reserve)ms=reserve;
        if(!ms)return fail(ERROR_TIMEOUT);Sleep(ms<25?ms:25);
    }
}
static bool con_live(L4SignalProfile* p){DWORD pid=0;FILETIME c,e,k,u;
    return p->con_process && service_pid(L"L4Con",&p->con,&pid) && pid==p->con_pid && GetProcessTimes(p->con_process,&c,&e,&k,&u) &&
        !CompareFileTime(&c,&p->con_created) && WaitForSingleObject(p->con_process,0)==WAIT_TIMEOUT?true:fail(ERROR_REVISION_MISMATCH);
}
static bool probe(unsigned service,DWORD pid,DWORD timeout,void* context){L4SignalProfile* p=context;if(service>1 || !timeout)return fail(ERROR_INVALID_PARAMETER);ULONGLONG end=GetTickCount64()+timeout;
    DWORD error=ERROR_NOT_READY;while(left(end)){DWORD ms=left(end),slice=ms<1000?ms:1000;ULONGLONG part=GetTickCount64()+slice;
        bool ok=service?tcp(pid,p->broker,part):info(p,pid,part,false);if(ok && left(end))return true;error=GetLastError();ms=left(end);if(ms)Sleep(ms<25?ms:25);}
    return fail(error?error:ERROR_TIMEOUT);
}
static bool channels(DWORD pid,DWORD timeout,void* context){L4SignalProfile* p=context;ULONGLONG end=GetTickCount64()+timeout;
    return timeout && info(p,pid,end,false) && tcp(pid,p->mqtt,end) && left(end)?true:fail(GetLastError()?GetLastError():ERROR_TIMEOUT);
}
static bool barrier(DWORD timeout,void* context){L4SignalProfile* p=context;ULONGLONG end=GetTickCount64()+timeout;
    return timeout && con_live(p) && left(end) && l4_probe_call(L"con",p->con_pid,1,left(end)) && left(end) && con_live(p)?true:fail(GetLastError()?GetLastError():ERROR_TIMEOUT);
}
void supervisor_signals_close(L4SignalProfile* p){if(!p)return;if(p->con_process)CloseHandle(p->con_process);l4_release_unpin(p->con_fence);free(p);WSACleanup();}
static bool signals_open(const L4Layout* roots,const L4CommunicationPin* pin,DWORD timeout,L4CommunicationBoot* boot,const L4UpdateState* expected,L4SignalProfile** output,L4CommunicationSignals* signals){
    if(!output || !signals)return fail(ERROR_INVALID_PARAMETER);*output=NULL;memset(signals,0,sizeof(*signals));
    const L4CommunicationPlan* plan=l4_communication_pinned_plan(pin);if(!roots || !plan || !timeout || timeout>300000)return fail(ERROR_INVALID_PARAMETER);
    if(boot){DWORD ms=l4_communication_boot_remaining(boot);if(!ms || !l4_communication_plan_matches(pin,expected) || !l4_communication_boot_check(boot,roots,expected))return false;if(timeout>ms)timeout=ms;}
    ULONGLONG end=GetTickCount64()+timeout;WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa))return fail(ERROR_NOT_READY);
    L4SignalProfile* p=calloc(1,sizeof(*p));if(!p){WSACleanup();return fail(ERROR_NOT_ENOUGH_MEMORY);}
    bool ok=true;for(unsigned i=0;ok && i<2;i++)ok=l4_switch_decode_bytes(roots,plan->switches[i],plan->switch_size[i],&p->pair[i]);
    if(ok)ok=l4_proxy_signal_source(p->pair[0].before.image_path,&p->http,&p->mqtt,p->thumbprint) && broker_port(plan->configs[0],plan->config_size[0],p->mqtt,&p->broker) && p->broker!=p->http;
    /* Source-version console only; do not learn/adopt a replacement during the barrier. */
    const wchar_t* command=p->pair[0].before.image_path;const wchar_t* q=wcschr(command+1,L'"');wchar_t prefix[MAX_PATH],version[32];L4Layout source;
    if(ok){swprintf_s(prefix,MAX_PATH,L"%ls\\releases\\",roots->binaries);size_t n=wcslen(prefix);const wchar_t* slash=NULL;
        ok=q && command[0]==L'"' && (size_t)(q-command-1)>n && !wcsncmp(command+1,prefix,n);
        if(ok)slash=wcschr(command+1+n,L'\\');ok=ok && slash && slash<q && slash-command-1-n<32;
        if(ok){wcsncpy_s(version,32,command+1+n,(size_t)(slash-command-1-n));ok=l4_layout_from_roots(&source,roots->binaries,roots->data,version) && l4_layout_component(&source,L"l4con",L"l4con.exe",p->con_image);}}
    L4ServiceSwitch con;
    if(ok)ok=l4_switch_decode_bytes(roots,plan->con_switch,plan->con_size,&con);
    if(ok){p->con=con.before;L4ReleaseFile file={L"l4con",L"l4con.exe",con.before_size,{0}};memcpy(file.sha256,con.before_sha256,32);
        wchar_t image[MAX_PATH];ok=l4_release_pin(&source,&file,&p->con_fence,image) && !wcscmp(image,p->con_image);
        if(ok && *p->thumbprint)ok=!_stricmp(p->thumbprint,plan->thumbprint);
        if(ok)strcpy_s(p->thumbprint,64,plan->thumbprint);
    }
    if(ok){wchar_t expected_command[MAX_PATH+3];swprintf_s(expected_command,_countof(expected_command),L"\"%ls\"",p->con_image);size_t n=wcslen(expected_command);
        ok=!wcsncmp(p->con.image_path,expected_command,n) && (!p->con.image_path[n] || p->con.image_path[n]==L' ' || p->con.image_path[n]==L'\t');
        if(ok)ok=boot?con_startup(roots,p,boot,expected,end):service_pid(L"L4Con",&p->con,&p->con_pid);}
    if(ok){p->con_process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,p->con_pid);ok=p->con_process!=NULL;FILETIME e,k,u;
        wchar_t image[MAX_PATH];DWORD size=MAX_PATH;if(ok)ok=GetProcessTimes(p->con_process,&p->con_created,&e,&k,&u) && QueryFullProcessImageNameW(p->con_process,0,image,&size) && !wcscmp(image,p->con_image);
        HANDLE token=NULL;BYTE user[512];DWORD needed=0;if(ok)ok=OpenProcessToken(p->con_process,TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,user,sizeof(user),&needed) && IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid);if(token)CloseHandle(token);}
    if(ok && (p->con_pid!=plan->con_pid || CompareFileTime(&p->con_created,&plan->con_created))){
        /* Only authenticated restart may adopt a new epoch of the SAME pinned
         * old Con. Prove original epoch gone first; never kill/start it here. */
        ok=boot!=NULL;
        if(ok){HANDLE original=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,plan->con_pid);
            if(original){FILETIME c,e,k,u;ok=GetProcessTimes(original,&c,&e,&k,&u) &&
                (CompareFileTime(&c,&plan->con_created) || WaitForSingleObject(original,0)==WAIT_OBJECT_0);CloseHandle(original);}
            else ok=GetLastError()==ERROR_INVALID_PARAMETER;
        }
        if(!ok)SetLastError(ERROR_REVISION_MISMATCH);
    }
    DWORD proxy=0,broker=0;
    if(ok && !boot)ok=left(end) && service_pid(L"Leo4Proxy",&p->pair[0].before,&proxy) && service_pid(L"mosquitto",&p->pair[1].before,&broker) &&
        info(p,proxy,end,true) && tcp(broker,p->broker,end) && con_live(p) && left(end) && l4_probe_call(L"con",p->con_pid,0,left(end)) && left(end) && con_live(p);
    if(ok)ok=con_live(p) && left(end) && (!boot || (l4_communication_boot_remaining(boot) && l4_communication_boot_check(boot,roots,expected)));
    if(!ok){DWORD code=GetLastError();supervisor_signals_close(p);return fail(code?code:ERROR_NOT_READY);}
    *signals=(L4CommunicationSignals){probe,channels,barrier,p};*output=p;return true;
}

bool supervisor_signals_open(const L4Layout* roots,const L4CommunicationPin* pin,DWORD timeout,L4SignalProfile** profile,L4CommunicationSignals* signals){
    return signals_open(roots,pin,timeout,NULL,NULL,profile,signals);
}
bool supervisor_signals_open_boot(const L4Layout* roots,const L4CommunicationPin* pin,L4CommunicationBoot* boot,const L4UpdateState* expected,L4SignalProfile** profile,L4CommunicationSignals* signals){
    if(!boot){if(profile)*profile=NULL;if(signals)memset(signals,0,sizeof(*signals));return fail(ERROR_INVALID_PARAMETER);}
    DWORD ms=l4_communication_boot_remaining(boot);if(!ms){if(profile)*profile=NULL;if(signals)memset(signals,0,sizeof(*signals));return false;}
    return signals_open(roots,pin,ms,boot,expected,profile,signals);
}
