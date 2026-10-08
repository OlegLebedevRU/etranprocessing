#include "broker_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool reject(DWORD code){SetLastError(code);return false;}
bool setup_broker_render(const L4Layout* layout,const char* sn,SetupBrokerConfig* out){
    if(!out)return reject(ERROR_INVALID_PARAMETER);memset(out,0,sizeof(*out));
    if(!layout || !sn || !sn[0] || strlen(sn)>127)return reject(ERROR_INVALID_PARAMETER);
    for(const char* p=sn;*p;p++)if(!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9')))return reject(ERROR_INVALID_DATA);
    const wchar_t* version=wcsrchr(layout->release,L'\\');L4Layout canonical;
    if(!version || !l4_layout_from_roots(&canonical,layout->binaries,layout->data,version+1) || memcmp(layout,&canonical,sizeof(canonical)))return reject(ERROR_INVALID_DATA);
    wchar_t path[MAX_PATH];char log[MAX_PATH*3];
    if(!l4_layout_data_path(layout,L"logs\\mosquitto\\mosquitto.log",path) || !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,log,sizeof(log),NULL,NULL))return false;
    for(char* p=log;*p;p++){if(*p=='\\')*p='/';if(*p=='#' || *p=='\r' || *p=='\n')return reject(ERROR_INVALID_NAME);}
    int size=snprintf(out->bytes,sizeof(out->bytes),
        "# l4-topic-contract: 3\nlistener 1883 127.0.0.1\nallow_anonymous true\n"
        "connection platerra-upstream\nbridge_protocol_version mqttv50\naddress 127.0.0.1:18883\n"
        "remote_clientid %s\ntry_private false\nnotifications false\n",sn);
    if(size<0 || size>=(int)sizeof(out->bytes))return reject(ERROR_INSUFFICIENT_BUFFER);
    const char* outbound[]={"app","svc","evt","req","res","out","ctl","fmr"};
    const char* inbound[]={"tsk","rsp","eva","cmt","ctl","fmc"};
    for(unsigned direction=0;direction<2;direction++)for(unsigned i=0;i<(direction?_countof(inbound):_countof(outbound));i++){
        int n=snprintf(out->bytes+size,sizeof(out->bytes)-(size_t)size,"topic %s/%s/%s %s 1\n",direction?"srv":"dev",sn,direction?inbound[i]:outbound[i],direction?"in":"out");
        if(n<0 || n>=(int)(sizeof(out->bytes)-(size_t)size))return reject(ERROR_INSUFFICIENT_BUFFER);size+=n;
    }
    int n=snprintf(out->bytes+size,sizeof(out->bytes)-(size_t)size,
        "cleansession true\nrestart_timeout 5 60\nkeepalive_interval 60\npersistence false\n"
        "log_dest file %s\nlog_type error\nlog_type warning\nlog_type notice\nlog_type information\n"
        "log_type subscribe\nlog_type unsubscribe\nconnection_messages true\n",log);
    if(n<0 || n>=(int)(sizeof(out->bytes)-(size_t)size))return reject(ERROR_INSUFFICIENT_BUFFER);
    out->size=(DWORD)(size+n);return true;
}

bool setup_broker_render_standby(const L4Layout* layout,SetupBrokerConfig* out){
    if(!out)return reject(ERROR_INVALID_PARAMETER);memset(out,0,sizeof(*out));
    SetupBrokerConfig checked;if(!setup_broker_render(layout,"standby",&checked))return false;
    const char* log=strstr(checked.bytes,"log_dest file ");if(!log)return reject(ERROR_INVALID_DATA);
    int n=snprintf(out->bytes,sizeof(out->bytes),"# l4-topic-contract: 3\n# l4-local-standby: no terminal identity\nlistener 1883 127.0.0.1\nallow_anonymous true\npersistence false\n%s",log);
    if(n<0 || n>=(int)sizeof(out->bytes))return reject(ERROR_INSUFFICIENT_BUFFER);out->size=(DWORD)n;return true;
}
