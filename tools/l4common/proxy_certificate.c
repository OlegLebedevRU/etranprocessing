#include "proxy_certificate.h"
#include <shellapi.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
static bool fail(void){SetLastError(ERROR_INVALID_DATA);return false;}
static bool text(const char* value,char* output,size_t capacity) {
    size_t size=strlen(value);if(size>=capacity)return fail();
    for(size_t i=0;i<size;i++)if((unsigned char)value[i]<32 || (unsigned char)value[i]>126)return fail();
    memcpy(output,value,size+1);return true;
}
static bool valid(const L4ProxyCertificate* profile) {
    char email[256],thumb[64];
    if(!memchr(profile->email,0,sizeof(profile->email)) || !memchr(profile->thumbprint,0,sizeof(profile->thumbprint)) ||
       !text(profile->email,email,sizeof(email)) || !text(profile->thumbprint,thumb,sizeof(thumb)))return fail();
    if(!*thumb)return true;if(strlen(thumb)!=40)return fail();
    for(unsigned i=0;i<40;i++)if(!((thumb[i]>='0' && thumb[i]<='9') || (thumb[i]>='A' && thumb[i]<='F') || (thumb[i]>='a' && thumb[i]<='f')))return fail();
    return true;
}
static bool option(L4ProxyCertificate* p,const char* name,const char* value) {
    if(!_stricmp(name,"--cert-email"))return text(value,p->email,sizeof(p->email));
    if(!_stricmp(name,"--cert-thumbprint"))return text(value,p->thumbprint,sizeof(p->thumbprint));
    return fail();
}
bool l4_proxy_certificate_probe(int argc,char** argv,L4ProxyCertificate* result) {
    if(!result || !argv || argc<6 || strcmp(argv[1],"--update-probe") || strcmp(argv[argc-1],"--version"))return fail();
    L4ProxyCertificate p={0};p.machine=true;bool email=false,thumb=false,user=false;
    for(int i=5;i<argc-1;i++) {
        if(!_stricmp(argv[i],"--user-store")){if(user)return fail();user=true;p.machine=false;continue;}
        bool e=!_stricmp(argv[i],"--cert-email"),t=!_stricmp(argv[i],"--cert-thumbprint");
        if((!e && !t) || (e && email) || (t && thumb) || i+1>=argc-1)return fail();
        if(!option(&p,argv[i],argv[i+1]))return false;i++;email|=e;thumb|=t;
    }
    if(!valid(&p))return false;*result=p;return true;
}
bool l4_proxy_certificate_source(const wchar_t* command,L4ProxyCertificate* result) {
    if(!command || !result || !*command || wcslen(command)>=2048)return fail();
    int count=0;wchar_t** args=CommandLineToArgvW(command,&count);if(!args)return false;
    const wchar_t* flags[]={L"--service",L"-f",L"--console",L"--foreground",L"-v",L"--verbose",L"--secure",L"--no-srv",L"--no-reverse",
        L"--no-discovery",L"--no-firewall",L"--no-elevate",L"--local-ssl",L"--http-local-ssl",L"--mqtt-local-ssl",L"--drop-on-expire",
        L"--stream",L"--no-stream",L"--rtp-tunnel",L"--no-rtp-tunnel"};
    const wchar_t* values[]={L"--policy-bootstrap-ip",L"--policy-bootstrap-port",L"--mqtt-srv",L"--http-srv",L"--stream-srv",L"--rtp-srv",
        L"--mqtt-remote",L"--mqtt-local",L"--http-remote",L"--http-local",L"--reverse-target",L"--reverse-listen",L"--reverse-local",
        L"--reverse-port",L"--domain",L"--local-domain",L"--cert-poll-interval",L"--stream-local",L"--stream-remote",L"--stream-max-clients",
        L"--stream-idle-timeout",L"--rtp-local",L"--rtp-port",L"--rtcp-port",L"--rtp-remote",L"--rtp-idle-timeout",L"--rtp-reconnect"};
    L4ProxyCertificate p={0};p.machine=true;bool ok=count>0 && *args[0];
    for(int i=1;ok && i<count;i++) {
        if(!_wcsicmp(args[i],L"--user-store")){p.machine=false;continue;}
        bool e=!_wcsicmp(args[i],L"--cert-email"),t=!_wcsicmp(args[i],L"--cert-thumbprint");
        if(e || t) {
            if(++i>=count){ok=false;break;}char value[256];size_t length=wcslen(args[i]);
            if(length>=sizeof(value)){ok=false;break;}
            for(size_t j=0;j<=length;j++){if(args[i][j]>126 || (args[i][j] && args[i][j]<32)){ok=false;break;}value[j]=(char)args[i][j];}
            if(ok)ok=option(&p,e?"--cert-email":"--cert-thumbprint",value);continue;
        }
        bool known=false;
        for(unsigned j=0;j<_countof(flags);j++)if(!_wcsicmp(args[i],flags[j])){known=true;break;}
        if(known)continue;
        for(unsigned j=0;j<_countof(values);j++)if(!_wcsicmp(args[i],values[j])){known=true;i++;break;}
        if(!known || i>=count)ok=false;
    }
    LocalFree(args);if(!ok || !valid(&p))return fail();*result=p;return true;
}
static bool put(wchar_t* output,size_t* at,wchar_t c) {
    if(*at>=2047)return fail();output[(*at)++]=c;output[*at]=0;return true;
}
static bool quote(wchar_t* output,size_t* at,const wchar_t* value) {
    if(!put(output,at,L'"'))return false;
    for(const wchar_t* p=value;;) {
        size_t slashes=0;while(*p==L'\\'){slashes++;p++;}
        size_t emitted=(*p==L'"' || !*p)?slashes*2:slashes;
        for(size_t i=0;i<emitted;i++)if(!put(output,at,L'\\'))return false;
        if(!*p)break;if(*p==L'"' && !put(output,at,L'\\'))return false;
        if(!put(output,at,*p++))return false;
    }
    return put(output,at,L'"');
}
bool l4_proxy_certificate_command(const wchar_t* exe,const L4ProxyCertificate* p,WORD http,WORD mqtt,DWORD timeout,wchar_t output[2048]) {
    if(!exe || !*exe || wcslen(exe)>=MAX_PATH || !p || !output || http<49152 || mqtt<49152 || http==mqtt || timeout<100 || timeout>300000 || !valid(p))return fail();
    size_t at=0;output[0]=0;if(!quote(output,&at,exe))return false;
    wchar_t fixed[128];swprintf_s(fixed,128,L" --update-probe %u %u %lu --cert-email ",(unsigned)http,(unsigned)mqtt,timeout);
    for(const wchar_t* c=fixed;*c;c++)if(!put(output,&at,*c))return false;
    wchar_t email[256],thumb[64];for(size_t i=0;i<=strlen(p->email);i++)email[i]=(wchar_t)p->email[i];
    for(size_t i=0;i<=strlen(p->thumbprint);i++)thumb[i]=(wchar_t)p->thumbprint[i];
    if(!quote(output,&at,email))return false;
    for(const wchar_t* c=L" --cert-thumbprint ";*c;c++)if(!put(output,&at,*c))return false;
    if(!quote(output,&at,thumb))return false;
    const wchar_t* end=p->machine?L" --version":L" --user-store --version";
    for(const wchar_t* c=end;*c;c++)if(!put(output,&at,*c))return false;return true;
}

static bool signal_port(const wchar_t* text,WORD* output){
    unsigned value=0;if(!text || !*text)return false;
    for(const wchar_t* p=text;*p;p++){if(*p<L'0' || *p>L'9' || value>6553)return false;value=value*10+*p-L'0';}
    if(!value || value>65535)return false;*output=(WORD)value;return true;
}
static bool signal_loopback(const wchar_t* text,WORD* output){
    if(!wcscmp(text,L"127.0.0.1"))return true;return !wcsncmp(text,L"127.0.0.1:",10) && signal_port(text+10,output);
}
bool l4_proxy_signal_source(const wchar_t* command,WORD* http,WORD* mqtt,char expected_thumb[64]){
    if(!command || !http || !mqtt || !expected_thumb){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    L4ProxyCertificate cert;if(!l4_proxy_certificate_source(command,&cert))return false;
    int count=0;wchar_t** args=CommandLineToArgvW(command,&count);if(!args)return false;
    const wchar_t* values[]={L"--policy-bootstrap-ip",L"--policy-bootstrap-port",L"--mqtt-srv",L"--http-srv",L"--stream-srv",L"--rtp-srv",
        L"--mqtt-remote",L"--mqtt-local",L"--http-remote",L"--http-local",L"--reverse-target",L"--reverse-listen",L"--reverse-local",
        L"--reverse-port",L"--domain",L"--local-domain",L"--cert-poll-interval",L"--stream-local",L"--stream-remote",L"--stream-max-clients",
        L"--stream-idle-timeout",L"--rtp-local",L"--rtp-port",L"--rtcp-port",L"--rtp-remote",L"--rtp-idle-timeout",L"--rtp-reconnect",L"--cert-email",L"--cert-thumbprint"};
    /* Existing normal source defaults, never a deadline or guessed probe port. */
    *http=18443;*mqtt=18883;bool ok=true;
    for(int i=1;ok && i<count;i++){
        if(!_wcsicmp(args[i],L"--local-ssl") || !_wcsicmp(args[i],L"--http-local-ssl")){ok=false;break;} /* Plain loopback HTTP profile only. */
        bool h=!_wcsicmp(args[i],L"--http-local"),m=!_wcsicmp(args[i],L"--mqtt-local");
        if(h || m){if(++i>=count || !signal_loopback(args[i],h?http:mqtt))ok=false;continue;}
        for(unsigned k=0;k<_countof(values);k++)if(!_wcsicmp(args[i],values[k])){i++;break;}
    }
    LocalFree(args);if(!ok || *http==*mqtt)return (SetLastError(ERROR_NOT_SUPPORTED),false);strcpy_s(expected_thumb,64,cert.thumbprint);return true;
}
