#include "policy.h"
#include "policy_json.h"
#include <winhttp.h>
#include <sddl.h>
#include <shlobj.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>

static SRWLOCK lock=SRWLOCK_INIT;
static PolicyRecord record;
static bool media_allowed=true, https_allowed=true, stopping=false, dirty=false;
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
static void set_media_locked(bool allowed) {
    if (media_allowed==allowed) return;
    media_allowed=allowed;
    if (!allowed) for (PolicySocket* n=sockets;n;n=n->next) shutdown(n->socket,SD_BOTH);
    printf("[POLICY] MQTT/RTP %s; facts=%s\n",allowed?"allowed":"denied",
           allowed?"":(record.known && !record.allowed?(facts[0]?facts:"server_denied"):"policy_unavailable"));
}
static void expire_locked(void) {
    if (record.sn[0]) {
        if (utc_now()>=record.offline_allowed_until) remaining_ms=0;
        set_media_locked(policy_record_allowed(&record,utc_now()) &&
            GetTickCount64()-anchor_tick<remaining_ms);
    }
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
bool policy_https_path_allowed(const char* path) {
    AcquireSRWLockShared(&lock); bool allowed=https_allowed; ReleaseSRWLockShared(&lock);
    if (allowed) return true;
    const char* query=strchr(path,'?'); size_t len=query?(size_t)(query-path):strlen(path);
    if (len==strlen("/api/leo4proxy/policy") && !memcmp(path,"/api/leo4proxy/policy",len)) return true;
    if (len && path[len-1]=='/') --len;
    const char* exceptions[]={"/api/certificates","/api/licensebilling","/licensebilling"};
    for (int k=0;k<3;k++) if (len==strlen(exceptions[k]) && !memcmp(path,exceptions[k],len)) return true;
    return false;
}
void policy_diagnostics(char* out,size_t size) {
    AcquireSRWLockExclusive(&lock); expire_locked();
    snprintf(out,size,"{\"mqtt_rtp_allowed\":%s,\"outgoing_https_allowed\":%s,"
        "\"offline_allowed_until\":%llu,\"last_success_at\":%llu,\"generation\":%llu,"
        "\"stop_facts\":\"%s\",\"last_error\":\"%s\",\"storage_pending\":%s}",
        media_allowed?"true":"false",https_allowed?"true":"false",record.offline_allowed_until,
        record.last_success_at,record.generation,media_allowed?"":
        (record.known && !record.allowed?(facts[0]?facts:"server_denied"):"policy_unavailable"),
        last_error,dirty?"true":"false");
    ReleaseSRWLockExclusive(&lock);
}
static void init_storage(void) {
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
    wchar_t base[MAX_PATH];
    int folders[]={CSIDL_COMMON_APPDATA,CSIDL_LOCAL_APPDATA};
    for (int k=0;k<2;k++) if (SUCCEEDED(SHGetFolderPathW(NULL,folders[k],NULL,SHGFP_TYPE_CURRENT,base)))
        swprintf_s(json_paths[k],MAX_PATH,L"%s\\Leo4Proxy\\policy.json",base);
    if (GetModuleFileNameW(NULL,base,MAX_PATH)) {
        wchar_t* slash=wcsrchr(base,L'\\');
        if (slash) { *slash=0; swprintf_s(json_paths[2],MAX_PATH,L"%s\\policy-data\\policy.json",base); }
    }
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
static bool fetch(PCCERT_CONTEXT cert,char* text,DWORD capacity,DWORD* length) {
    wchar_t host[MAX_HOST_LEN]; MultiByteToWideChar(CP_UTF8,0,settings.http_remote_host,-1,host,MAX_HOST_LEN);
    HINTERNET session=WinHttpOpen(L"Leo4Proxy policy/1",WINHTTP_ACCESS_TYPE_NO_PROXY,NULL,NULL,0);
    if (!session) return false;
    WinHttpSetTimeouts(session,5000,5000,5000,5000);
    DWORD protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof(protocols));
    HINTERNET connection=WinHttpConnect(session,host,(INTERNET_PORT)settings.http_remote_port,0);
    HINTERNET request=connection?WinHttpOpenRequest(connection,L"GET",L"/api/leo4proxy/policy",NULL,
        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):NULL;
    bool ok=false; *length=0;
    if (request) {
        DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects));
        if (settings.insecure_server_cert) {
            DWORD flags=SECURITY_FLAG_IGNORE_UNKNOWN_CA|SECURITY_FLAG_IGNORE_CERT_CN_INVALID|
                        SECURITY_FLAG_IGNORE_CERT_DATE_INVALID|SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            WinHttpSetOption(request,WINHTTP_OPTION_SECURITY_FLAGS,&flags,sizeof(flags));
        }
        DWORD status=0,bytes=sizeof(status);
        ok=WinHttpSetOption(request,WINHTTP_OPTION_CLIENT_CERT_CONTEXT,(void*)cert,sizeof(CERT_CONTEXT)) &&
            WinHttpSendRequest(request,L"Accept: application/json\r\nCache-Control: no-cache\r\n",(DWORD)-1L,
                WINHTTP_NO_REQUEST_DATA,0,0,0) && WinHttpReceiveResponse(request,NULL) &&
            WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,NULL,&status,&bytes,NULL) && status==200;
        wchar_t content_type[128]; bytes=sizeof(content_type);
        if (ok) ok=WinHttpQueryHeaders(request,WINHTTP_QUERY_CONTENT_TYPE,NULL,content_type,&bytes,NULL) &&
            !_wcsnicmp(content_type,L"application/json",16) && (content_type[16]==0 || content_type[16]==L';');
        while (ok) {
            DWORD available=0,read=0;
            if (!WinHttpQueryDataAvailable(request,&available)) { ok=false; break; }
            if (!available) break;
            if (available>=capacity-*length || !WinHttpReadData(request,text+*length,available,&read) || !read) { ok=false; break; }
            *length+=read;
        }
        WinHttpCloseHandle(request);
    }
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session); text[*length]=0; return ok;
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
    settings=*config; init_storage();
    stop_event=CreateEventW(NULL,TRUE,FALSE,NULL); wake_event=CreateEventW(NULL,FALSE,FALSE,NULL);
    if (stop_event && wake_event) worker=(HANDLE)_beginthreadex(NULL,0,run,NULL,0,NULL);
    if (!worker) fprintf(stderr,"[POLICY] Cannot start polling worker; retry requires service restart\n");
}
void policy_identity(const CertDetails* details) {
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
        if (certificate) CertFreeCertificateContext(certificate);
        certificate=details?CertDuplicateCertificateContext(details->pCertContext):NULL;
        ++identity_generation;
        if (wake_event) SetEvent(wake_event);
    }
    ReleaseSRWLockExclusive(&lock);
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
}
