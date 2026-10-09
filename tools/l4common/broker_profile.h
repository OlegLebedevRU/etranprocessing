#pragma once
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char bytes[8192]; DWORD size; } L4BrokerProfile;

/* One built-in wire profile for setup and supervisor, including byte order.
 * Pure rendering only: no files, services, credentials or MQTT connections.
 * NULL identity means local standby; an active identity is never a wildcard. */
static __inline bool l4_broker_profile_render(const char* log_path,int port,
                                             const char* sn,L4BrokerProfile* out){
    if(!out){SetLastError(ERROR_INVALID_PARAMETER);return false;}memset(out,0,sizeof(*out));
    if(!log_path || !log_path[0] || strlen(log_path)>=MAX_PATH*3 || port<1 || port>65535 ||
       (sn && (!sn[0] || strlen(sn)>127))){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    char log[MAX_PATH*3];strcpy_s(log,sizeof(log),log_path);
    for(char* p=log;*p;p++){if(*p=='\\')*p='/';if(*p=='#' || *p=='\r' || *p=='\n'){SetLastError(ERROR_INVALID_NAME);return false;}}
    if(sn)for(const char* p=sn;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9'))){SetLastError(ERROR_INVALID_DATA);return false;}
    int size=snprintf(out->bytes,sizeof(out->bytes),sn?
        "# l4-topic-contract: 3\nlistener %d 127.0.0.1\nallow_anonymous true\n":
        "# l4-topic-contract: 3\n# l4-local-standby: no terminal identity\nlistener %d 127.0.0.1\nallow_anonymous true\n",port);
    if(size<0 || size>=(int)sizeof(out->bytes))goto overflow;
    if(sn){
        int n=snprintf(out->bytes+size,sizeof(out->bytes)-(size_t)size,
            "connection platerra-upstream\nbridge_protocol_version mqttv50\naddress 127.0.0.1:18883\n"
            "remote_clientid %s\ntry_private false\nnotifications false\n",sn);
        if(n<0 || n>=(int)(sizeof(out->bytes)-(size_t)size))goto overflow;size+=n;
        const char* outbound[]={"app","svc","evt","req","res","out","ctl","fmr"};
        const char* inbound[]={"tsk","rsp","eva","cmt","ctl","fmc"};
        for(unsigned direction=0;direction<2;direction++)for(unsigned i=0;i<(direction?_countof(inbound):_countof(outbound));i++){
            n=snprintf(out->bytes+size,sizeof(out->bytes)-(size_t)size,"topic %s/%s/%s %s 1\n",
                direction?"srv":"dev",sn,direction?inbound[i]:outbound[i],direction?"in":"out");
            if(n<0 || n>=(int)(sizeof(out->bytes)-(size_t)size))goto overflow;size+=n;
        }
        n=snprintf(out->bytes+size,sizeof(out->bytes)-(size_t)size,"cleansession true\nrestart_timeout 5 60\nkeepalive_interval 60\n");
        if(n<0 || n>=(int)(sizeof(out->bytes)-(size_t)size))goto overflow;size+=n;
    }
    {
        int n=snprintf(out->bytes+size,sizeof(out->bytes)-(size_t)size,
            "persistence false\nlog_dest file %s\nlog_type error\nlog_type warning\nlog_type notice\n"
            "log_type information\nlog_type subscribe\nlog_type unsubscribe\nconnection_messages true\n",log);
        if(n<0 || n>=(int)(sizeof(out->bytes)-(size_t)size))goto overflow;
        out->size=(DWORD)(size+n);return true;
    }
overflow:
    memset(out,0,sizeof(*out));SetLastError(ERROR_INSUFFICIENT_BUFFER);return false;
}
