#include "policy.h"
#include "../../l4common/layout.h"
#include "policy_json.h"
#include "schannel_tls.h"
#include "endpoints.h"
#include <winhttp.h>
#include <sddl.h>
#include <shlobj.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>

static SRWLOCK lock=SRWLOCK_INIT;
static PolicyRecord record;
static bool media_allowed=true, https_allowed=true, stopping=false, dirty=false;
static bool storage_failed;
static char fm_authority[272];
static ULONGLONG fm_authority_deadline;
static char facts[1024], last_error[64]="not_polled";
static PCCERT_CONTEXT certificate;
static unsigned long long identity_generation;
static ULONGLONG anchor_tick, remaining_ms;
static HANDLE stop_event, wake_event, worker;
static ProxyConfig settings;
static PolicySocket* sockets;
static wchar_t json_paths[3][MAX_PATH];
static PSECURITY_DESCRIPTOR storage_sd;
static SECURITY_ATTRIBUTES storage_sa={sizeof(SECURITY_ATTRIBUTES),NULL,FALSE};
static const wchar_t* registry_path=L"SOFTWARE\\Leo4\\Leo4Proxy\\Policy";
static HKEY storage_roots[2]={HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER};

static unsigned long long utc_now(void) { return (unsigned long long)time(NULL); }
static bool uint_field(const PolicyJson* j,const char* name,unsigned long long* out) {
    return policy_json_uint(j,policy_json_field(j,0,name),out);
}
bool policy_record_parse(const char* text,size_t len,const char* sn,PolicyRecord* out) {
    PolicyJson j; PolicyRecord r={0}; unsigned long long version;
    if (!policy_json_parse(&j,text,len) || !uint_field(&j,"storage_version",&version) || version!=1 ||
        !policy_json_string(&j,policy_json_field(&j,0,"sn"),r.sn,sizeof(r.sn)) || strcmp(r.sn,sn) ||
        !uint_field(&j,"generation",&r.generation) || !r.generation || r.generation==ULLONG_MAX ||
        !uint_field(&j,"last_success_at",&r.last_success_at) ||
        !uint_field(&j,"offline_allowed_until",&r.offline_allowed_until) ||
        !policy_json_bool(&j,policy_json_field(&j,0,"known"),&r.known) ||
        !policy_json_bool(&j,policy_json_field(&j,0,"allowed"),&r.allowed)) return false;
    if (r.known) {
        if (!r.last_success_at || r.last_success_at>ULLONG_MAX-POLICY_GRACE_SECONDS ||
            r.offline_allowed_until!=r.last_success_at+POLICY_GRACE_SECONDS) return false;
    } else if (!r.allowed || r.last_success_at || !r.offline_allowed_until) return false;
    /* Avoid millisecond overflow from corrupt storage; years outside FILETIME range are invalid. */
    if (r.offline_allowed_until>32503680000ULL) return false;
    int reason=policy_json_field(&j,0,"facts");
    if (reason>=0 && !policy_json_string(&j,reason,r.facts,sizeof(r.facts))) return false;
    for (size_t k=0;r.facts[k];k++) if (!((r.facts[k]>='a' && r.facts[k]<='z') ||
        (r.facts[k]>='0' && r.facts[k]<='9') || r.facts[k]=='_' || r.facts[k]==',')) return false;
    *out=r; return true;
}
bool policy_response_parse(const char* text,size_t len,const char* sn,bool* media,bool* https,
                           char* out_facts,size_t size) {
    PolicyJson j; unsigned long long version; char found_sn[MAX_SN_LEN];
    *https=true;
    if (!policy_json_parse(&j,text,len) || !uint_field(&j,"v",&version) || version!=1 ||
        !policy_json_string(&j,policy_json_field(&j,0,"sn"),found_sn,sizeof(found_sn)) || strcmp(sn,found_sn) ||
        !policy_json_bool(&j,policy_json_field(&j,0,"mqtt_rtp_allowed"),media)) return false;
    int f=policy_json_field(&j,0,"stop_facts");
    if (f<0 || j.tokens[f].type!='[' || !size) return false;
    size_t used=0; out_facts[0]=0;
    for (int n=f+1;n<j.tokens[f].next;n=j.tokens[n].next) {
        char fact[128];
        if (!policy_json_string(&j,n,fact,sizeof(fact))) return false;
        for (size_t k=0;fact[k];k++) if (!((fact[k]>='a' && fact[k]<='z') ||
            (fact[k]>='0' && fact[k]<='9') || fact[k]=='_')) return false;
        size_t length=strlen(fact);
        if (!length || used+length+2>size) return false;
        if (used) out_facts[used++]=',';
        memcpy(out_facts+used,fact,length+1); used+=length;
    }
    bool value;
    if (policy_json_bool(&j,policy_json_field(&j,0,"outgoing_https_allowed"),&value) && !value) *https=false;
    return true;
}
bool policy_record_allowed(const PolicyRecord* r,unsigned long long now) {
    return !(r->known && !r->allowed) && now<r->offline_allowed_until;
}
bool policy_record_prefer(const PolicyRecord* a,const PolicyRecord* b) {
    if (a->generation!=b->generation) return a->generation>b->generation;
    bool aa=!(a->known && !a->allowed), ba=!(b->known && !b->allowed);
    if (aa!=ba) return aa;
    return aa && a->offline_allowed_until>b->offline_allowed_until;
}
static const char* identity_denial_locked(void) {
    if (!certificate) return "certificate_missing";
    LONG validity=CertVerifyTimeValidity(NULL,certificate->pCertInfo);
    if (validity>0) return "certificate_expired";
    if (validity<0) return "certificate_not_yet_valid";
    return NULL;
}
static void set_media_locked(bool allowed) {
    const char* identity_denial=identity_denial_locked();
    allowed=allowed && !identity_denial;
    if (media_allowed==allowed) return;
    media_allowed=allowed;
    if (!allowed) for (PolicySocket* n=sockets;n;n=n->next) shutdown(n->socket,SD_BOTH);
    printf("[POLICY] MQTT/RTP/FM %s; facts=%s\n",allowed?"allowed":"denied",
           allowed?"":(identity_denial?identity_denial:
               (record.known && !record.allowed?(facts[0]?facts:"server_denied"):"policy_unavailable")));
}
static void expire_locked(void) {
    if (storage_failed) { set_media_locked(false); https_allowed=false; return; }
    if (record.sn[0]) {
        if (utc_now()>=record.offline_allowed_until) remaining_ms=0;
        set_media_locked(policy_record_allowed(&record,utc_now()) &&
            GetTickCount64()-anchor_tick<remaining_ms);
    } else set_media_locked(!identity_denial_locked());
}
bool policy_media_allowed(void) {
    AcquireSRWLockExclusive(&lock); expire_locked(); bool result=media_allowed && !stopping;
    ReleaseSRWLockExclusive(&lock); return result;
}
int policy_media_connect(PolicySocket* node,SOCKET s,const struct sockaddr* address,int length) {
    AcquireSRWLockExclusive(&lock); expire_locked();
    int rc=SOCKET_ERROR, error=WSAEACCES;
    if (media_allowed && !stopping) {
        node->socket=s; node->registered=true; node->next=sockets; sockets=node;
        rc=connect(s,address,length); error=WSAGetLastError();
    }
    ReleaseSRWLockExclusive(&lock); WSASetLastError(error); return rc;
}
void policy_socket_unregister(PolicySocket* node) {
    AcquireSRWLockExclusive(&lock);
    if (node->registered) {
        PolicySocket** n=&sockets;
        while (*n && *n!=node) n=&(*n)->next;
        if (*n) *n=node->next;
        node->registered=false; node->next=NULL;
    }
    ReleaseSRWLockExclusive(&lock);
}
bool policy_registry_allowed(void){
    AcquireSRWLockExclusive(&lock);expire_locked();bool allowed=media_allowed && https_allowed && !stopping;
    ReleaseSRWLockExclusive(&lock);return allowed;
}
int policy_registry_connect(PolicySocket* node,SOCKET s,const struct sockaddr* address,int length){
    AcquireSRWLockExclusive(&lock);expire_locked();int rc=SOCKET_ERROR,error=WSAEACCES;
    if(media_allowed && https_allowed && !stopping){node->socket=s;node->registered=true;node->next=sockets;sockets=node;
        rc=connect(s,address,length);error=WSAGetLastError();}
    ReleaseSRWLockExclusive(&lock);WSASetLastError(error);return rc;
}
bool policy_https_path_allowed(const char* path) {
    /* Canonicalize the path before FM admission: encoded separators and dot
     * segments must not bypass the common deny through nginx normalization. */
    char decoded[1024],normalized[1024];size_t length=strcspn(path,"?");
    if (!length || length>=sizeof(decoded)) return false;
    memcpy(decoded,path,length);decoded[length]=0;
    for(unsigned pass=0;pass<4 && strchr(decoded,'%');pass++) {
        size_t out=0;
        for(size_t i=0;decoded[i];i++) {
            unsigned char value=(unsigned char)decoded[i];
            if(value=='%') {
                if(!decoded[i+1] || !decoded[i+2] || !isxdigit((unsigned char)decoded[i+1]) || !isxdigit((unsigned char)decoded[i+2])) return false;
                char hex[]={decoded[i+1],decoded[i+2],0};value=(unsigned char)strtoul(hex,NULL,16);i+=2;
            }
            if(value<32 || value==127 || value=='\\' || value=='?' || value=='#') return false;
            decoded[out++]=(char)value;
        }
        decoded[out]=0;
    }
    if(strchr(decoded,'%') || decoded[0]!='/') return false;
    size_t used=0;const char* cursor=decoded;
    while(*cursor) {
        while(*cursor=='/')cursor++;if(!*cursor)break;
        const char* end=strchr(cursor,'/');size_t part=end?(size_t)(end-cursor):strlen(cursor);
        if(part==2 && !memcmp(cursor,"..",2)) {while(used && normalized[used-1]!='/')used--;if(used)used--;}
        else if(!(part==1 && *cursor=='.')) {normalized[used++]='/';memcpy(normalized+used,cursor,part);used+=part;}
        cursor+=part;
    }
    normalized[used]=0;
    if (!strcmp(normalized,"/api/file-manager") || !strncmp(normalized,"/api/file-manager/",18)) return policy_fm_authority_allowed(NULL);
    AcquireSRWLockShared(&lock); bool allowed=https_allowed; ReleaseSRWLockShared(&lock);
    if (allowed) return true;
    const char* query=strchr(path,'?'); size_t len=query?(size_t)(query-path):strlen(path);
    if (len==strlen("/api/leo4proxy/policy") && !memcmp(path,"/api/leo4proxy/policy",len)) return true;
    if (len && path[len-1]=='/') --len;
    const char* exceptions[]={"/api/certificates","/api/licensebilling","/licensebilling"};
    for (int k=0;k<3;k++) if (len==strlen(exceptions[k]) && !memcmp(path,exceptions[k],len)) return true;
    return false;
}
/* Missing extension, identity rotation or stale provider routing closes FM.
 * Common media deny immediately shuts down registered storage tunnel sockets. */
bool policy_fm_authority_allowed(const char* authority) {
    AcquireSRWLockExclusive(&lock);expire_locked();
    bool allowed=media_allowed && https_allowed && !stopping && fm_authority[0] &&
        GetTickCount64()<fm_authority_deadline && (!authority || !_stricmp(authority,fm_authority));
    ReleaseSRWLockExclusive(&lock);return allowed;
}
static void accept_fm_locked(const char* text,size_t length,bool allowed) {
    fm_authority[0]=0;fm_authority_deadline=0;
    PolicyJson json;bool enabled=false;char host[256];unsigned long long port=0;
    if (!allowed || !policy_json_parse(&json,text,length) ||
        !policy_json_bool(&json,policy_json_field(&json,0,"fm_allowed"),&enabled) || !enabled) return;
    int endpoint=policy_json_field(&json,0,"fm_storage_endpoint");
    if (!policy_json_string(&json,policy_json_field(&json,endpoint,"host"),host,sizeof(host)) || !endpoint_host_valid(host) ||
        !policy_json_uint(&json,policy_json_field(&json,endpoint,"port"),&port) || !port || port>65535) return;
    sprintf_s(fm_authority,sizeof(fm_authority),"%s:%u",host,(unsigned)port);
    fm_authority_deadline=GetTickCount64()+2ULL*POLICY_POLL_SECONDS*1000;
}
void policy_diagnostics(char* out,size_t size) {
    AcquireSRWLockExclusive(&lock); expire_locked();
    snprintf(out,size,"{\"mqtt_rtp_allowed\":%s,\"outgoing_https_allowed\":%s,"
        "\"offline_allowed_until\":%llu,\"last_success_at\":%llu,\"generation\":%llu,"
        "\"stop_facts\":\"%s\",\"last_error\":\"%s\",\"storage_pending\":%s}",
        media_allowed?"true":"false",https_allowed?"true":"false",record.offline_allowed_until,
        record.last_success_at,record.generation,media_allowed?"":
        (identity_denial_locked()?identity_denial_locked():
        (record.known && !record.allowed?(facts[0]?facts:"server_denied"):"policy_unavailable")),
        last_error,dirty?"true":"false");
    ReleaseSRWLockExclusive(&lock);
}
static bool init_storage_paths(const wchar_t* exe) {
    ZeroMemory(json_paths, sizeof(json_paths));
    return l4_runtime_exe_path(exe, L"leo4proxy", L4_DATA_STATE,
        L"leo4proxy\\policy.json", L"policy-data\\policy.json", json_paths[0]) &&
        wcslen(json_paths[0]) + 4 < MAX_PATH;
}
static bool init_storage(void) {
    wchar_t exe[MAX_PATH];
    DWORD count = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!count || count >= MAX_PATH || !init_storage_paths(exe)) return false;
    HANDLE token=NULL; DWORD length=0;
    if (OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) {
        GetTokenInformation(token,TokenUser,NULL,0,&length);
        TOKEN_USER* user=(TOKEN_USER*)malloc(length);
        wchar_t* sid=NULL;
        if (user && GetTokenInformation(token,TokenUser,user,length,&length) && ConvertSidToStringSidW(user->User.Sid,&sid)) {
            wchar_t sddl[512];
            swprintf_s(sddl,512,L"D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)(A;OICI;GA;;;%s)",sid);
            ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,&storage_sd,NULL);
            LocalFree(sid);
        }
        free(user); CloseHandle(token);
    }
    storage_sa.lpSecurityDescriptor=storage_sd;
    return storage_sd != NULL;
}

static bool load_file(const wchar_t* path,char* text,DWORD size,DWORD* length) {
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (f==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER bytes; bool ok=GetFileSizeEx(f,&bytes) && bytes.QuadPart>0 && bytes.QuadPart<size &&
        ReadFile(f,text,size-1,length,NULL) && *length==(DWORD)bytes.QuadPart;
    CloseHandle(f); if (ok) text[*length]=0; return ok;
}
static void restore(const char* sn,PolicyRecord* result) {
    PolicyRecord best={0}; char text[4096];
    for (int k=0;k<2;k++) {
        HKEY key; DWORD bytes=sizeof(text),type=0;
        if (RegOpenKeyExW(storage_roots[k],registry_path,0,KEY_QUERY_VALUE|KEY_WOW64_32KEY,&key)==ERROR_SUCCESS) {
            if (RegQueryValueExW(key,L"State",NULL,&type,(BYTE*)text,&bytes)==ERROR_SUCCESS && type==REG_BINARY) {
                PolicyRecord found;
                if (policy_record_parse(text,bytes,sn,&found) && (!best.generation || policy_record_prefer(&found,&best))) best=found;
            }
            RegCloseKey(key);
        }
    }
    for (int k=0;k<3;k++) if (json_paths[k][0]) {
        DWORD bytes=0; PolicyRecord found;
        if (load_file(json_paths[k],text,sizeof(text),&bytes) && policy_record_parse(text,bytes,sn,&found) &&
            (!best.generation || policy_record_prefer(&found,&best))) best=found;
    }
    if (!best.generation) {
        strcpy_s(best.sn,sizeof(best.sn),sn); best.generation=1; best.allowed=true;
        best.offline_allowed_until=utc_now()+POLICY_GRACE_SECONDS;
        printf("[POLICY] No valid copies; starting 72-hour grace\n");
    }
    *result=best;
}
static bool persist(const PolicyRecord* r) {
    char escaped_sn[MAX_SN_LEN*6]; size_t used=0;
    for (size_t k=0;r->sn[k];k++) {
        unsigned char c=(unsigned char)r->sn[k];
        if (c<' ' || c=='"' || c=='\\') {
            int wrote=snprintf(escaped_sn+used,sizeof(escaped_sn)-used,"\\u%04x",c);
            used+=(size_t)wrote;
        } else escaped_sn[used++]=(char)c;
    }
    escaped_sn[used]=0;
    char text[3072]; int bytes=snprintf(text,sizeof(text),
        "{\"storage_version\":1,\"sn\":\"%s\",\"generation\":%llu,\"known\":%s,\"allowed\":%s,"
        "\"last_success_at\":%llu,\"offline_allowed_until\":%llu,\"facts\":\"%s\"}",escaped_sn,r->generation,
        r->known?"true":"false",r->allowed?"true":"false",r->last_success_at,r->offline_allowed_until,r->facts);
    bool registry_ok=false,file_ok=false;
    for (int k=0;k<2 && !registry_ok;k++) {
        HKEY key;
        if (RegCreateKeyExW(storage_roots[k],registry_path,0,NULL,0,KEY_SET_VALUE|KEY_WOW64_32KEY,
                           storage_sd?&storage_sa:NULL,&key,NULL)==ERROR_SUCCESS) {
            registry_ok=RegSetValueExW(key,L"State",0,REG_BINARY,(const BYTE*)text,(DWORD)bytes)==ERROR_SUCCESS;
            if (registry_ok) registry_ok=RegFlushKey(key)==ERROR_SUCCESS;
            RegCloseKey(key);
        }
    }
    for (int k=0;k<3 && !file_ok;k++) if (json_paths[k][0]) {
        wchar_t dir[MAX_PATH],temp[MAX_PATH];
        wcscpy_s(dir,MAX_PATH,json_paths[k]); wchar_t* slash=wcsrchr(dir,L'\\'); if (!slash) continue; *slash=0;
        if (!CreateDirectoryW(dir,storage_sd?&storage_sa:NULL) && GetLastError()!=ERROR_ALREADY_EXISTS) continue;
        swprintf_s(temp,MAX_PATH,L"%s.tmp",json_paths[k]);
        HANDLE f=CreateFileW(temp,GENERIC_WRITE,0,storage_sd?&storage_sa:NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        if (f==INVALID_HANDLE_VALUE) continue;
        DWORD written=0;
        bool ok=WriteFile(f,text,(DWORD)bytes,&written,NULL) && written==(DWORD)bytes && FlushFileBuffers(f);
        CloseHandle(f);
        if (ok) file_ok=MoveFileExW(temp,json_paths[k],MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        if (!file_ok) DeleteFileW(temp);
    }
    return registry_ok && file_ok;
}
static bool bootstrap_probe;
/* Finish a framed reply without relying on the peer closing its connection. */
static bool http_framed_complete(const char* response,size_t used) {
    const char* end=strstr(response,"\r\n\r\n");if(!end)return false;
    const char* body=end+4;size_t available=used-(size_t)(body-response);
    bool chunked=false;unsigned long declared=ULONG_MAX;
    for(const char* line=strstr(response,"\r\n");line && line<end;) {
        line+=2;const char* next=strstr(line,"\r\n");if(!next)return false;
        if(!_strnicmp(line,"Content-Length:",15)) {
            if(declared!=ULONG_MAX)return false;const char* value=line+15;while(*value==' ' || *value=='\t')++value;
            if(*value<'0' || *value>'9')return false;
            char* stop;declared=strtoul(value,&stop,10);if(stop!=next || declared>16384)return false;
        }
        if(!_strnicmp(line,"Transfer-Encoding:",18)) {
            const char* value=line+18;while(*value==' ')++value;
            if(next-value!=7 || _strnicmp(value,"chunked",7))return false;chunked=true;
        }
        line=next;
    }
    if(!chunked)return declared!=ULONG_MAX && available==declared;
    if(declared!=ULONG_MAX)return false;
    const char* cursor=body;const char* limit=response+used;
    for(;;) {
        const char* line=strstr(cursor,"\r\n");if(!line || line-cursor>16 || line==cursor)return false;
        char size[17];size_t digits=(size_t)(line-cursor);memcpy(size,cursor,digits);size[digits]=0;
        for(size_t n=0;n<digits;n++) if(!isxdigit((unsigned char)size[n]))return false;
        char* stop;unsigned long amount=strtoul(size,&stop,16);if(*stop || amount>16384)return false;
        cursor=line+2;if(!amount)return limit-cursor==2 && !memcmp(cursor,"\r\n",2);
        if((size_t)(limit-cursor)<(size_t)amount+2 || memcmp(cursor+amount,"\r\n",2))return false;
        cursor+=amount+2;
    }
}
static bool fetch(PCCERT_CONTEXT cert,char* text,DWORD capacity,DWORD* length) {
    CredHandle creds; SChannelSession tls; *length=0;
    if (!schannel_init_client_creds(cert,1,&creds)) return false;
    bool connected=bootstrap_probe?
        schannel_connect_endpoint(&tls,&creds,settings.policy_bootstrap_ip,settings.http_remote_host,
            settings.policy_bootstrap_port,8000,false):
        schannel_connect_channel(&tls,&creds,&settings,ENDPOINT_HTTPS,15000,true);
    if (!connected) { schannel_free_creds(&creds); return false; }
    DWORD timeout=5000;
    setsockopt(tls.sock,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof(timeout));
    setsockopt(tls.sock,SOL_SOCKET,SO_SNDTIMEO,(const char*)&timeout,sizeof(timeout));
    tls.io_deadline=GetTickCount64()+10000;
    char request[1024]; int len=snprintf(request,sizeof(request),
        "GET /api/leo4proxy/policy HTTP/1.1\r\nHost: %s\r\nAccept: application/json\r\nConnection: close\r\nCache-Control: no-cache\r\n\r\n",settings.http_remote_host);
    bool ok=len>0 && len<(int)sizeof(request) && schannel_send(&tls,request,len)==len;
    char response[24577]; size_t used=0; ULONGLONG deadline=tls.io_deadline;
    while (ok && used<sizeof(response)-1) {
        if (GetTickCount64()>=deadline) { ok=false; break; }
        int got=schannel_recv(&tls,response+used,(int)(sizeof(response)-1-used));
        if (got<0) { ok=false; break; }
        if (!got) break;
        used+=(size_t)got;
        response[used]=0;
        if(http_framed_complete(response,used))break;
    }
    response[used]=0;
    schannel_close(&tls); schannel_free_creds(&creds);
    /* HTTP GET is idempotent; never retry a request after receiving a denial. */
    char* body=strstr(response,"\r\n\r\n");
    if (!ok || !body || used==sizeof(response)-1 ||
        (strncmp(response,"HTTP/1.1 200 ",13) && strncmp(response,"HTTP/1.0 200 ",13))) return false;
    *body=0; body+=4;
    bool json=false,chunked=false; unsigned long long declared=ULLONG_MAX;
    for (char* line=strstr(response,"\r\n");line && *line;) {
        line+=2; char* end=strstr(line,"\r\n"); if (end) *end=0;
        if (!_strnicmp(line,"Content-Type:",13)) {
            char* type=line+13; while (*type==' ' || *type=='\t') ++type;
            json=!_strnicmp(type,"application/json",16) && (!type[16] || type[16]==';' || type[16]==' ');
        }
        if (!_strnicmp(line,"Content-Length:",15)) {
            char* value=line+15; while(*value==' ' || *value=='\t')++value;
            if(*value<'0' || *value>'9')return false;
            char* stop=NULL; unsigned long long number=strtoull(value,&stop,10);
            if (!stop || *stop || number>16384 || declared!=ULLONG_MAX) return false;
            declared=number;
        }
        if (!_strnicmp(line,"Transfer-Encoding:",18)) {
            char* value=line+18; while (*value==' ') ++value;
            if (_stricmp(value,"chunked")) return false; chunked=true;
        }
        if (!end) break; *end='\r'; line=end;
    }
    if (!json || (chunked && declared!=ULLONG_MAX)) return false;
    size_t available=used-(size_t)(body-response);
    if (!chunked) {
        if (declared!=ULLONG_MAX && declared!=available) return false;
        if (available>=capacity) return false;
        memcpy(text,body,available); *length=(DWORD)available;
    } else {
        char* cursor=body; char* limit=body+available;
        for (;;) {
            char* end=strstr(cursor,"\r\n"); if (!end || end-cursor>16) return false;
            if(end==cursor)return false;
            for(char* digit=cursor;digit<end;digit++) if(!isxdigit((unsigned char)*digit))return false;
            *end=0; char* stop=NULL; unsigned long chunk=strtoul(cursor,&stop,16);
            if (!stop || stop==cursor || *stop) return false;
            cursor=end+2;
            if (!chunk) { if (limit-cursor!=2 || memcmp(cursor,"\r\n",2)) return false; break; }
            if (chunk>=capacity-*length || (size_t)(limit-cursor)<(size_t)chunk+2 || memcmp(cursor+chunk,"\r\n",2)) return false;
            memcpy(text+*length,cursor,chunk); *length+=chunk; cursor+=chunk+2;
        }
    }
    text[*length]=0; return true;
}
static unsigned __stdcall run(void* unused) {
    (void)unused; ULONGLONG next_poll=0,next_save=0;
    while (WaitForSingleObject(stop_event,0)==WAIT_TIMEOUT) {
        AcquireSRWLockExclusive(&lock); expire_locked();
        ULONGLONG now=GetTickCount64();
        PolicyRecord saved=record; bool save=dirty && record.sn[0] && now>=next_save;
        PCCERT_CONTEXT cert=NULL; unsigned long long identity=identity_generation;
        if (certificate && now>=next_poll) cert=CertDuplicateCertificateContext(certificate);
        ReleaseSRWLockExclusive(&lock);
        if (save) {
            bool ok=persist(&saved); next_save=GetTickCount64()+60000;
            AcquireSRWLockExclusive(&lock);
            if (record.generation==saved.generation && !strcmp(record.sn,saved.sn)) dirty=!ok;
            ReleaseSRWLockExclusive(&lock);
            if (!ok) fprintf(stderr,"[POLICY] Storage degraded; retaining state in memory and retrying\n");
        }
        if (cert) {
            char text[16385],new_facts[1024]; DWORD length=0; bool allowed=false,https=true;
            bool ok=fetch(cert,text,sizeof(text),&length) &&
                policy_response_parse(text,length,saved.sn,&allowed,&https,new_facts,sizeof(new_facts));
            CertFreeCertificateContext(cert);
            bool accepted=false; PolicyRecord updated={0};
            AcquireSRWLockExclusive(&lock);
            if (identity==identity_generation && !stopping) {
                https_allowed=ok?https:true;
                strcpy_s(last_error,sizeof(last_error),ok?"":"api_unavailable_or_invalid");
                if (ok) {
                    accept_fm_locked(text,length,allowed);
                    record.known=true; record.allowed=allowed; record.last_success_at=utc_now();
                    record.offline_allowed_until=record.last_success_at+POLICY_GRACE_SECONDS; ++record.generation;
                    anchor_tick=GetTickCount64(); remaining_ms=POLICY_GRACE_SECONDS*1000;
                    strcpy_s(facts,sizeof(facts),new_facts); dirty=true; next_save=0;
                    strcpy_s(record.facts,sizeof(record.facts),new_facts);
                    set_media_locked(allowed);
                    updated=record; accepted=true;
                    printf("[POLICY] Accepted policy; MQTT/RTP=%s HTTPS=%s\n",allowed?"true":"false",https?"true":"false");
                }
            }
            ReleaseSRWLockExclusive(&lock);
            if (accepted) {
                endpoints_accept(text,length,updated.sn);
                bool stored=persist(&updated); next_save=GetTickCount64()+60000;
                AcquireSRWLockExclusive(&lock);
                if (record.generation==updated.generation && !strcmp(record.sn,updated.sn)) dirty=!stored;
                ReleaseSRWLockExclusive(&lock);
                if (!stored) fprintf(stderr,"[POLICY] Storage degraded after policy; will retry\n");
            }
            next_poll=GetTickCount64()+POLICY_POLL_SECONDS*1000ULL;
        }
        HANDLE events[]={stop_event,wake_event}; DWORD wait=WaitForMultipleObjects(2,events,FALSE,1000);
        if (wait==WAIT_OBJECT_0) break;
        if (wait==WAIT_OBJECT_0+1) { next_poll=0; next_save=0; }
    }
    return 0;
}
void policy_init(const ProxyConfig* config) {
    settings=*config;
    if (!init_storage()) {
        storage_failed=true;
        media_allowed=https_allowed=false;
        strcpy_s(last_error,sizeof(last_error),"storage_layout");
        fprintf(stderr,"[POLICY] Cannot resolve protected storage; admission denied (win32=%lu)\n",GetLastError());
        return;
    }
    endpoints_init(config);
    stop_event=CreateEventW(NULL,TRUE,FALSE,NULL); wake_event=CreateEventW(NULL,FALSE,FALSE,NULL);
    if (stop_event && wake_event) worker=(HANDLE)_beginthreadex(NULL,0,run,NULL,0,NULL);
    if (!worker) fprintf(stderr,"[POLICY] Cannot start polling worker; retry requires service restart\n");
}
void policy_identity(const CertDetails* details) {
    if (storage_failed) return;
    /* Establish routing identity before waking the admission poll worker. */
    endpoints_identity(details?details->sn:"");
    AcquireSRWLockExclusive(&lock);
    bool changed=details && strcmp(record.sn,details->sn)!=0;
    bool cert_changed=(!details && certificate) || (details && (!certificate ||
        details->pCertContext->cbCertEncoded!=certificate->cbCertEncoded ||
        memcmp(details->pCertContext->pbCertEncoded,certificate->pbCertEncoded,certificate->cbCertEncoded)));
    if (changed) {
        restore(details->sn,&record); dirty=true; strcpy_s(facts,sizeof(facts),record.facts);
        unsigned long long now=utc_now(); anchor_tick=GetTickCount64();
        remaining_ms=record.offline_allowed_until>now?(record.offline_allowed_until-now)*1000:0;
        expire_locked();
    }
    if (cert_changed) {
        fm_authority[0]=0;fm_authority_deadline=0;
        if (certificate) CertFreeCertificateContext(certificate);
        certificate=details?CertDuplicateCertificateContext(details->pCertContext):NULL;
        ++identity_generation;
        if (wake_event) SetEvent(wake_event);
    }
    expire_locked();
    ReleaseSRWLockExclusive(&lock);
}
bool policy_probe_media_allowed(const char* sn) {
    /* Standalone diagnostic only: read admission, without polling or updating it. */
    if (!init_storage()) return false;
    PolicyRecord cached={0}; restore(sn,&cached);
    bool allowed=policy_record_allowed(&cached,utc_now());
    if (storage_sd) { LocalFree(storage_sd); storage_sd=NULL; storage_sa.lpSecurityDescriptor=NULL; }
    return allowed;
}
bool policy_probe_bootstrap(const ProxyConfig* config, const CertDetails* cert) {
    if (!config->policy_bootstrap_ip[0]) return false;
    settings=*config; bootstrap_probe=true;
    char body[16385],facts_text[1024]; DWORD length=0; bool media=false,https=false;
    bool ok=fetch(cert->pCertContext,body,sizeof(body),&length) &&
        policy_response_parse(body,length,cert->sn,&media,&https,facts_text,sizeof(facts_text));
    bootstrap_probe=false;
    if (ok) printf("{\"v\":1,\"policy\":\"valid\",\"source\":\"bootstrap_ip\",\"strict\":true,\"mqtt_allowed\":%s,\"https_allowed\":%s}\n",media?"true":"false",https?"true":"false");
    return ok;
}
void policy_stop(void) {
    AcquireSRWLockExclusive(&lock); stopping=true;
    for (PolicySocket* n=sockets;n;n=n->next) shutdown(n->socket,SD_BOTH);
    ReleaseSRWLockExclusive(&lock);
    if (stop_event) SetEvent(stop_event);
    if (worker) { WaitForSingleObject(worker,INFINITE); CloseHandle(worker); worker=NULL; }
    if (record.sn[0] && dirty && !persist(&record))
        fprintf(stderr,"[POLICY] Could not save both state copies during shutdown\n");
    if (certificate) { CertFreeCertificateContext(certificate); certificate=NULL; }
    if (stop_event) CloseHandle(stop_event);
    if (wake_event) CloseHandle(wake_event);
    if (storage_sd) LocalFree(storage_sd);
    endpoints_shutdown();
}
