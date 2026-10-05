#define fm_start fm_child_start
#define fm_shutdown fm_child_shutdown
#define fm_enqueue fm_child_enqueue
#define fm_busy fm_child_busy
#define fm_connection fm_child_connection
#define fm_tick fm_child_tick
#define fm_set_navigation_result fm_child_set_navigation_result
#define fm_navigation fm_child_navigation
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "file_manager.h"
#include "config.h"
#include "mqtt_protocol.h"
#include "../../leo4proxy/src/policy_json.h"
#include "../../l4pin/src/cert_discovery.h"
#include <winhttp.h>
#include <objbase.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <wchar.h>
#include <time.h>
#include "fm_user.h"
#include <aclapi.h>

#define FM_RESPONSE_LIMIT 65536
static struct {
    CRITICAL_SECTION lock;
    HANDLE thread,wake,stop;
    bool initialized,connected,pending,processing;
    RpcCommand command;
    char sn[128],api[768],instance[40],lease[40];
    wchar_t root[1024],journal[1024];
    int proxy_port;
    ULONGLONG deadline;
    FmResult result;
    FmNavigationResult navigation_result;
    unsigned generation;
    HANDLE user_token;
    DWORD user_session;
    LUID user_authentication;
    wchar_t read_roots[32][1024];
    unsigned read_root_count;
    bool local_drives;
    void* context;
} fm;

static bool user_current(void) {
    HANDLE previous=NULL;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY|TOKEN_IMPERSONATE,TRUE,&previous) && !RevertToSelf()) {CloseHandle(previous);return false;}
    DWORD session;LUID authentication;HANDLE token=fm_desktop_token(&session,&authentication);
    bool ok=token && fm.user_token && session==fm.user_session &&
        authentication.HighPart==fm.user_authentication.HighPart && authentication.LowPart==fm.user_authentication.LowPart;
    if(token)CloseHandle(token);
    if(previous) {ok=ImpersonateLoggedOnUser(previous) && ok;CloseHandle(previous);}
    return ok;
}
static bool user_enter(void) {return user_current() && ImpersonateLoggedOnUser(fm.user_token);}

/* Loopback Leo4Proxy only: it owns remote PB routing, mTLS and common policy. */
static bool api(const char* suffix,const char* body,char** output) {
    *output=NULL;
    char combined[1024];wchar_t url[1024],host[256],path[768],headers[256],instance[40];
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,fm.instance,-1,instance,40)) return false;
    swprintf_s(headers,256,L"Content-Type: application/json\r\nX-FM-Agent-Instance-Id: %s\r\n",instance);
    if (snprintf(combined,sizeof(combined),"%s%s",fm.api,suffix)>=(int)sizeof(combined) ||
        !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,combined,-1,url,1024)) return false;
    URL_COMPONENTS parts={sizeof(parts)};
    parts.dwExtraInfoLength=parts.dwUserNameLength=parts.dwPasswordLength=(DWORD)-1;
    parts.lpszHostName=host;parts.dwHostNameLength=256;parts.lpszUrlPath=path;parts.dwUrlPathLength=768;
    if (!WinHttpCrackUrl(url,0,0,&parts) || parts.nScheme!=INTERNET_SCHEME_HTTP || wcscmp(host,L"127.0.0.1") ||
        parts.nPort!=fm.proxy_port || parts.dwExtraInfoLength || parts.dwUserNameLength || parts.dwPasswordLength ||
        wcsncmp(path,L"/api/file-manager/v1/agent/",27)) return false;
    HINTERNET session=WinHttpOpen(L"l4con FM",WINHTTP_ACCESS_TYPE_NO_PROXY,NULL,NULL,0);
    HINTERNET connection=session?WinHttpConnect(session,host,parts.nPort,0):NULL;
    HINTERNET request=connection?WinHttpOpenRequest(connection,body?L"POST":L"GET",path,NULL,NULL,NULL,0):NULL;
    bool ok=false;char* data=NULL;DWORD used=0;
    ULONGLONG deadline=GetTickCount64()+5000;
    if (request) {
        DWORD disabled=WINHTTP_DISABLE_REDIRECTS;
        ok=WinHttpSetTimeouts(request,2000,2000,2000,2000) &&
           WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)) &&
           WinHttpSendRequest(request,headers,(DWORD)-1L,(void*)body,body?(DWORD)strlen(body):0,body?(DWORD)strlen(body):0,0) &&
           WinHttpReceiveResponse(request,NULL);
        DWORD status=0,size=sizeof(status);
        ok=ok && WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,NULL,&status,&size,NULL) && status==200;
        data=(char*)malloc(FM_RESPONSE_LIMIT+1);ok=ok && data!=NULL;
        while (ok) {
            DWORD available=0,read=0;
            if (GetTickCount64()>=deadline || !WinHttpQueryDataAvailable(request,&available)) {ok=false;break;}
            if (!available) break;
            if (available>FM_RESPONSE_LIMIT-used || !WinHttpReadData(request,data+used,available,&read) || !read) {ok=false;break;}
            used+=read;
        }
        if (ok) {data[used]=0;*output=data;data=NULL;}
    }
    free(data);if (request) WinHttpCloseHandle(request);if (connection) WinHttpCloseHandle(connection);if (session) WinHttpCloseHandle(session);
    return ok;
}

static bool alive(void) {
    bool active;EnterCriticalSection(&fm.lock);
    active=fm.connected && fm.lease[0] && GetTickCount64()<fm.deadline && WaitForSingleObject(fm.stop,0)!=WAIT_OBJECT_0;
    if (!active) {fm.lease[0]=0;fm.deadline=0;}
    LeaveCriticalSection(&fm.lock);return active;
}
bool fm_busy(void) {
    if (!fm.initialized) return false;
    bool busy;EnterCriticalSection(&fm.lock);busy=fm.pending || fm.processing;LeaveCriticalSection(&fm.lock);
    return busy || alive();
}
void fm_connection(bool connected) {
    if (!fm.initialized) return;
    EnterCriticalSection(&fm.lock);fm.connected=connected;
    if (!connected) {fm.lease[0]=0;fm.deadline=0;fm.pending=false;fm.generation++;}
    LeaveCriticalSection(&fm.lock);
    if (!connected && fm.thread) CancelSynchronousIo(fm.thread);
}
void fm_tick(void) {
    if (!fm.initialized) return;
    bool expired;EnterCriticalSection(&fm.lock);
    expired=fm.lease[0] && GetTickCount64()>=fm.deadline;
    if (expired) {fm.lease[0]=0;fm.deadline=0;fm.pending=false;fm.generation++;}
    LeaveCriticalSection(&fm.lock);
    if (expired && fm.thread) CancelSynchronousIo(fm.thread);
}

/* Reject Win32 aliases, ADS, device/UNC paths and ambiguous normalization. */
static bool valid_path(const wchar_t* path) {
    if (!path || !iswalpha(path[0]) || path[1]!=L':' || path[2]!=L'\\' || wcslen(path)>1000) return false;
    if(wcslen(path)==3)return true;
    const wchar_t* part=path+3;
    for (const wchar_t* p=part;;p++) {
        if (*p && (*p<L' ' || wcschr(L"/:*?\"<>|",*p))) return false;
        if (*p==L'\\' || !*p) {
            size_t length=(size_t)(p-part);
            if (!length || part[length-1]==L'.' || part[length-1]==L' ') return false;
            wchar_t name[1024];wcsncpy_s(name,1024,part,length);
            if (!_wcsicmp(name,L".ssh") || !_wcsicmp(name,L".git") || !_wcsicmp(name,L"Crypto") || !_wcsicmp(name,L"Protect")) return false;
            wchar_t* dot=wcschr(name,L'.');if (dot) *dot=0;
            if (!_wcsicmp(name,L"CON") || !_wcsicmp(name,L"PRN") || !_wcsicmp(name,L"AUX") || !_wcsicmp(name,L"NUL") ||
                (wcslen(name)==4 && (!_wcsnicmp(name,L"COM",3) || !_wcsnicmp(name,L"LPT",3)) && name[3]>=L'1' && name[3]<=L'9')) return false;
            if (!*p) break;part=p+1;
        }
    }
    return true;
}
/* Hold every ancestor without FILE_SHARE_DELETE: reparse/rename races fail closed. */
static bool checked_directory(const wchar_t* path,HANDLE handles[128],unsigned* count,bool enforce_policy) {
    *count=0;
    if (!valid_path(path) || !valid_path(fm.root)) return false;
    bool permitted=false;
    for(unsigned r=0;r<fm.read_root_count;r++) {
        size_t n=wcslen(fm.read_roots[r]);
        if(!_wcsnicmp(path,fm.read_roots[r],n) && (n==3 || !path[n] || path[n]==L'\\'))permitted=true;
    }
    if(enforce_policy && !permitted)return false;
    wchar_t prefix[1024];wcscpy_s(prefix,1024,path);
    size_t length=wcslen(path);
    for (size_t i=3;i<=length;i++) {
        if (path[i] && path[i]!=L'\\') continue;
        wchar_t saved=prefix[i];if(i!=3)prefix[i]=0;
        HANDLE handle=CreateFileW(prefix,FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        prefix[i]=saved;
        if (handle==INVALID_HANDLE_VALUE || *count>=128) {if(handle!=INVALID_HANDLE_VALUE) CloseHandle(handle);return false;}
        handles[(*count)++]=handle;
        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(handle,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        wchar_t resolved[1100],expected[1100];
        if(i!=3)prefix[i]=0;swprintf_s(expected,1100,L"\\\\?\\%s",prefix);prefix[i]=saved;
        DWORD got=GetFinalPathNameByHandleW(handle,resolved,1100,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if (!got || got>=1100 || _wcsicmp(resolved,expected)) return false;
    }
    return true;
}
static bool open_directory(const wchar_t* path,HANDLE handles[128],unsigned* count) {
    return checked_directory(path,handles,count,true);
}
static bool quote(const wchar_t* wide,char* target,size_t capacity) {
    char utf8[4096];if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,-1,utf8,sizeof(utf8),NULL,NULL)) return false;
    size_t used=0;if (capacity<3) return false;target[used++]='"';
    for (const unsigned char* p=(const unsigned char*)utf8;*p;p++) {
        if (*p<32 || used+3>=capacity) return false;
        if (*p=='"' || *p=='\\') target[used++]='\\';target[used++]=(char)*p;
    }
    target[used++]='"';target[used]=0;return true;
}

static bool private_name(const wchar_t* name);
static bool transfer_alive(void);

static bool list_directory(const char* path,unsigned offset,char* result,size_t capacity) {
    wchar_t wide[1024],pattern[1030];HANDLE handles[128];unsigned count=0;
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,1024)) return false;
    bool impersonated=user_enter();
    bool ok=impersonated && open_directory(wide,handles,&count);HANDLE find=INVALID_HANDLE_VALUE;
    size_t used=0;unsigned entries=0,visible=0;bool more=false;
    if (ok) {
        swprintf_s(pattern,1030,L"%s\\*",wide);WIN32_FIND_DATAW entry;
        find=FindFirstFileW(pattern,&entry);
        if (find==INVALID_HANDLE_VALUE && GetLastError()!=ERROR_FILE_NOT_FOUND) ok=false;
        used=(size_t)snprintf(result,capacity,"{\"state\":\"completed\",\"entries\":[");
        if (find!=INVALID_HANDLE_VALUE) do {
            if (!wcscmp(entry.cFileName,L".") || !wcscmp(entry.cFileName,L"..")) continue;
            if (!transfer_alive()) {ok=false;break;}
            /* Hide reparse entries and private credential material. */
            const wchar_t* extension=wcsrchr(entry.cFileName,L'.');
            if (private_name(entry.cFileName) || entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT ||
                (extension && (!_wcsicmp(extension,L".key") || !_wcsicmp(extension,L".pfx") || !_wcsicmp(extension,L".p12") || !_wcsicmp(extension,L".pem"))) || !_wcsicmp(entry.cFileName,L".env")) continue;
            if(++visible<=offset)continue;
            if(entries==64) {more=true;break;}entries++;
            char name[8192];if (!quote(entry.cFileName,name,sizeof(name))) {ok=false;break;}
            unsigned long long size=((unsigned long long)entry.nFileSizeHigh<<32)|entry.nFileSizeLow;
            int added=snprintf(result+used,capacity-used,"%s{\"name\":%s,\"directory\":%s,\"size_bytes\":%llu}",result[used-1]=='['?"":",",name,entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY?"true":"false",size);
            if (added<0 || (size_t)added>=capacity-used) {ok=false;break;}used+=(size_t)added;
        } while (FindNextFileW(find,&entry));
        if (find!=INVALID_HANDLE_VALUE && ok && !more && GetLastError()!=ERROR_NO_MORE_FILES) ok=false;
        if (ok && used+22<capacity) {snprintf(result+used,capacity-used,"],\"has_more\":%s}",more?"true":"false");}else ok=false;
    }
    if (find!=INVALID_HANDLE_VALUE) FindClose(find);
    for (unsigned i=0;i<count;i++) CloseHandle(handles[i]);
    if(impersonated)RevertToSelf();return ok;
}

static bool permitted_root(const PolicyJson* json) {
    bool drives=false;
    policy_json_bool(json,policy_json_field(json,0,"local_drives"),&drives);
    if(drives) {
        wchar_t all[256];DWORD n=GetLogicalDriveStringsW(256,all);
        if(!n || n>=256)return false;
        fm.read_root_count=0;fm.local_drives=true;
        for(wchar_t* p=all;*p && fm.read_root_count<32;p+=wcslen(p)+1) {
            UINT type=GetDriveTypeW(p);
            if(type==DRIVE_FIXED || type==DRIVE_REMOVABLE)wcscpy_s(fm.read_roots[fm.read_root_count++],1024,p);
        }
        return fm.read_root_count>0;
    }
    fm.local_drives=false;fm.read_root_count=0;
    int roots=policy_json_field(json,0,"read_roots");
    if (roots<0 || json->tokens[roots].type!='[') return false;
    for (int i=roots+1;i<json->tokens[roots].next;i=json->tokens[i].next) {
        char path[4096];wchar_t wide[1024];
        if (policy_json_string(json,i,path,sizeof(path)) && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,1024) && valid_path(wide) && fm.read_root_count<32)
            wcscpy_s(fm.read_roots[fm.read_root_count++],1024,wide);
    }
    return fm.read_root_count>0;
}

static void execute(const RpcCommand* command);
static bool transfer_alive(void) {
    RpcCommand command;bool renew=false;
    EnterCriticalSection(&fm.lock);
    if (fm.pending && fm.command.method==7023 && !strcmp(fm.command.fm_action,"renew")) {
        command=fm.command;fm.pending=false;renew=true;
    }
    LeaveCriticalSection(&fm.lock);
    if (renew) execute(&command);
    return alive();
}

static bool hash_file(HANDLE file,char hex[65],unsigned long long* length) {
    BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE hash=NULL;BYTE digest[32],buffer[65536];DWORD read=0;
    LARGE_INTEGER zero={0},size;
    bool ok=GetFileSizeEx(file,&size) && size.QuadPart>=0 && size.QuadPart<=67108864 &&
        SetFilePointerEx(file,zero,NULL,FILE_BEGIN) &&
        BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0 &&
        BCryptCreateHash(algorithm,&hash,NULL,0,NULL,0,0)>=0;
    *length=0;
    while (ok) {
        if (!transfer_alive() || !ReadFile(file,buffer,sizeof(buffer),&read,NULL)) {ok=false;break;}
        if (!read) break;*length+=read;
        if (*length>67108864 || BCryptHashData(hash,buffer,read,0)<0) {ok=false;break;}
    }
    ok=ok && *length==(unsigned long long)size.QuadPart && BCryptFinishHash(hash,digest,32,0)>=0 && SetFilePointerEx(file,zero,NULL,FILE_BEGIN);
    if (ok) {for (unsigned i=0;i<32;i++) sprintf_s(hex+i*2,65-i*2,"%02x",digest[i]);}
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}

/* S3 HTTPS through Leo4Proxy's policy-bound CONNECT; no client certificate or bypass. */
static bool storage_io(const PolicyJson* grant,HANDLE file,bool upload,unsigned long long expected) {
    char address[8192],digest[128]={0};wchar_t url[8192],host[256],path[8192],extra[8192],headers[256]={0};
    if (!policy_json_string(grant,policy_json_field(grant,0,"url"),address,sizeof(address)) ||
        !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,address,-1,url,8192)) return false;
    URL_COMPONENTS parts={sizeof(parts)};
    parts.lpszHostName=host;parts.dwHostNameLength=256;parts.lpszUrlPath=path;parts.dwUrlPathLength=8192;
    parts.lpszExtraInfo=extra;parts.dwExtraInfoLength=8192;parts.dwUserNameLength=parts.dwPasswordLength=(DWORD)-1;
    if (!WinHttpCrackUrl(url,0,0,&parts) || parts.nScheme!=INTERNET_SCHEME_HTTPS || parts.dwUserNameLength || parts.dwPasswordLength || wcschr(extra,L'#') || wcscat_s(path,8192,extra)) return false;
    if (upload) {
        int object=policy_json_field(grant,0,"headers");
        if (!policy_json_string(grant,policy_json_field(grant,object,"x-amz-checksum-sha256"),digest,sizeof(digest))) return false;
        wchar_t checksum[128];if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,digest,-1,checksum,128)) return false;
        swprintf_s(headers,256,L"Content-Type: application/octet-stream\r\nx-amz-checksum-sha256: %s\r\n",checksum);
    }
    wchar_t proxy[64];swprintf_s(proxy,64,L"127.0.0.1:%d",fm.proxy_port);
    HINTERNET session=WinHttpOpen(L"l4con FM storage",WINHTTP_ACCESS_TYPE_NAMED_PROXY,proxy,WINHTTP_NO_PROXY_BYPASS,0);
    HINTERNET connection=session?WinHttpConnect(session,host,parts.nPort,0):NULL;
    HINTERNET request=connection?WinHttpOpenRequest(connection,upload?L"PUT":L"GET",path,NULL,NULL,NULL,WINHTTP_FLAG_SECURE):NULL;
    bool ok=false;ULONGLONG deadline=GetTickCount64()+45000;unsigned long long total=0;BYTE buffer[65536];
    if (request) {
        DWORD disabled=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_AUTHENTICATION;
        ok=WinHttpSetTimeouts(request,3000,3000,3000,3000) && WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)) &&
            transfer_alive() && WinHttpSendRequest(request,upload?headers:NULL,upload?(DWORD)-1L:0,NULL,0,upload?(DWORD)expected:0,0);
        if (upload) while (ok && total<expected) {
            DWORD read=0,written=0;
            if (GetTickCount64()>=deadline || !transfer_alive() || !ReadFile(file,buffer,(DWORD)((expected-total)<sizeof(buffer)?expected-total:sizeof(buffer)),&read,NULL) || !read ||
                !WinHttpWriteData(request,buffer,read,&written) || written!=read) {ok=false;break;}
            total+=read;
        }
        ok=ok && transfer_alive() && WinHttpReceiveResponse(request,NULL);
        DWORD status=0,size=sizeof(status);
        ok=ok && WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,NULL,&status,&size,NULL) && status==200;
        if (!upload) while (ok) {
            DWORD read=0,written=0;
            if (GetTickCount64()>=deadline || !transfer_alive() || !WinHttpReadData(request,buffer,sizeof(buffer),&read)) {ok=false;break;}
            if (!read) break;
            if (total+read>expected || !WriteFile(file,buffer,read,&written,NULL) || written!=read) {ok=false;break;}total+=read;
        }
        ok=ok && total==expected;
    }
    if(request)WinHttpCloseHandle(request);if(connection)WinHttpCloseHandle(connection);if(session)WinHttpCloseHandle(session);return ok;
}

static bool private_name(const wchar_t* name) {
    const wchar_t* extension=wcsrchr(name,L'.');
    return !_wcsicmp(name,L"fm-state") || !_wcsicmp(name,L"Crypto") || !_wcsicmp(name,L"Protect") || !_wcsicmp(name,L".ssh") || !_wcsicmp(name,L".git") || !_wcsicmp(name,L".env") || !_wcsnicmp(name,L".l4fm-",6) ||
        (extension && (!_wcsicmp(extension,L".key") || !_wcsicmp(extension,L".pem") || !_wcsicmp(extension,L".pfx") || !_wcsicmp(extension,L".p12")));
}

typedef struct {
    DWORD magic,volume,index_high,index_low,outcome;
    unsigned long long length;
    char id[40],sha[65];
    wchar_t path[1024];
} CommitReceipt;

static bool receipt_outcome(const wchar_t* path,DWORD outcome) {
    HANDLE handle=CreateFileW(path,GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,NULL);
    if(handle==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER offset;offset.QuadPart=offsetof(CommitReceipt,outcome);DWORD written=0;
    bool ok=SetFilePointerEx(handle,offset,NULL,FILE_BEGIN) && WriteFile(handle,&outcome,sizeof(outcome),&written,NULL) && written==sizeof(outcome) && FlushFileBuffers(handle);
    CloseHandle(handle);return ok;
}

static bool save_receipt(HANDLE file,const wchar_t* path,const RpcCommand* command,const char* sha,unsigned long long length,wchar_t receipt_path[1100]) {
    BY_HANDLE_FILE_INFORMATION info;CommitReceipt receipt={0};
    if (!GetFileInformationByHandle(file,&info)) return false;
    receipt.magic=0x464D0001;receipt.volume=info.dwVolumeSerialNumber;receipt.index_high=info.nFileIndexHigh;receipt.index_low=info.nFileIndexLow;receipt.length=length;
    strcpy_s(receipt.id,40,command->fm_operation_id);strcpy_s(receipt.sha,65,sha);wcscpy_s(receipt.path,1024,path);
    wchar_t id[40];if (!MultiByteToWideChar(CP_UTF8,0,receipt.id,-1,id,40)) return false;
    swprintf_s(receipt_path,1100,L"%s\\.l4fm-%s.receipt",fm.journal,id);
    PSECURITY_DESCRIPTOR descriptor=NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&descriptor,NULL)) return false;
    SECURITY_ATTRIBUTES attributes={sizeof(attributes),descriptor,FALSE};
    HANDLE handle=CreateFileW(receipt_path,GENERIC_WRITE,0,&attributes,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_WRITE_THROUGH,NULL);
    LocalFree(descriptor);if(handle==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=WriteFile(handle,&receipt,sizeof(receipt),&written,NULL) && written==sizeof(receipt) && FlushFileBuffers(handle);
    CloseHandle(handle);return ok;
}

static bool trusted_receipt(HANDLE handle) {
    PSID owner=NULL;PACL acl=NULL;PSECURITY_DESCRIPTOR descriptor=NULL;
    if(GetSecurityInfo(handle,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,NULL,&acl,NULL,&descriptor)!=ERROR_SUCCESS)return false;
    SECURITY_DESCRIPTOR_CONTROL control=0;DWORD revision=0;
    bool ok=owner && (IsWellKnownSid(owner,WinLocalSystemSid) || IsWellKnownSid(owner,WinBuiltinAdministratorsSid)) &&
        GetSecurityDescriptorControl(descriptor,&control,&revision) && (control&SE_DACL_PROTECTED) && acl && acl->AceCount>0;
    for(WORD i=0;ok && i<acl->AceCount;i++) {
        ACCESS_ALLOWED_ACE* ace=NULL;
        ok=GetAce(acl,i,(void**)&ace) && ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE &&
            (IsWellKnownSid(&ace->SidStart,WinLocalSystemSid) || IsWellKnownSid(&ace->SidStart,WinBuiltinAdministratorsSid));
    }
    LocalFree(descriptor);return ok;
}
static void reconcile_receipts_at(const wchar_t* directory) {
    wchar_t pattern[1100];swprintf_s(pattern,1100,L"%s\\.l4fm-*.receipt",directory);
    HANDLE roots[128];unsigned count=0;
    if(!checked_directory(directory,roots,&count,false)) {for(unsigned i=0;i<count;i++)CloseHandle(roots[i]);return;}
    WIN32_FIND_DATAW entry;HANDLE find=FindFirstFileW(pattern,&entry);unsigned scanned=0;
    if(find!=INVALID_HANDLE_VALUE) do {
        if (WaitForSingleObject(fm.stop,0)==WAIT_OBJECT_0) break;
        if(++scanned>1 || entry.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) continue;
        wchar_t receipt_path[1100];swprintf_s(receipt_path,1100,L"%s\\%s",directory,entry.cFileName);
        HANDLE handle=CreateFileW(receipt_path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        CommitReceipt receipt={0};DWORD read=0;LARGE_INTEGER size;
        bool valid=handle!=INVALID_HANDLE_VALUE && trusted_receipt(handle) && GetFileSizeEx(handle,&size) && size.QuadPart==sizeof(receipt) && ReadFile(handle,&receipt,sizeof(receipt),&read,NULL) && read==sizeof(receipt);
        if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
        if(!valid || receipt.magic!=0x464D0001 || !memchr(receipt.id,0,40) || !memchr(receipt.sha,0,65) || !wmemchr(receipt.path,0,1024) || !rpc_uuid(receipt.id) || !valid_path(receipt.path))continue;
        wchar_t parent[1024];wcscpy_s(parent,1024,receipt.path);wchar_t* separator=wcsrchr(parent,L'\\');if(!separator)continue;separator[separator==parent+2?1:0]=0;
        HANDLE ancestors[128];unsigned ancestors_count=0;valid=checked_directory(parent,ancestors,&ancestors_count,false);
        HANDLE target=valid?CreateFileW(receipt.path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL):INVALID_HANDLE_VALUE;
        BY_HANDLE_FILE_INFORMATION info;
        valid=receipt.outcome==2 || (target!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(target,&info) && info.dwVolumeSerialNumber==receipt.volume && info.nFileIndexHigh==receipt.index_high && info.nFileIndexLow==receipt.index_low && info.nNumberOfLinks==1 && !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)));
        if(target!=INVALID_HANDLE_VALUE)CloseHandle(target);
        for(unsigned i=0;i<ancestors_count;i++)CloseHandle(ancestors[i]);
        if(valid) {
            char suffix[160],body[256],*response=NULL;
            snprintf(suffix,sizeof(suffix),"/operations/%s/reconcile",receipt.id);
            snprintf(body,sizeof(body),"{\"size_bytes\":%llu,\"sha256\":\"%s\",\"state\":\"%s\"}",receipt.length,receipt.sha,receipt.outcome==2?"failed":"completed");
            if(api(suffix,body,&response))DeleteFileW(receipt_path);
            free(response);
        }
    } while(scanned<1 && FindNextFileW(find,&entry));
    if(find!=INVALID_HANDLE_VALUE)FindClose(find);
    for(unsigned i=0;i<count;i++)CloseHandle(roots[i]);
}

/* Return 2 after terminal->S3 verification, 1 after atomic terminal publish. */
static int transfer(const RpcCommand* command,const PolicyJson* ticket,const char* kind) {
    char path[4096],suffix[160],*response=NULL;wchar_t wide[1024],parent[1024],partial[1100];
    if (!policy_json_string(ticket,policy_json_field(ticket,0,"path"),path,sizeof(path)) ||
        !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,1024) || !valid_path(wide)) return 0;
    wchar_t* separator=wcsrchr(wide,L'\\');if (!separator || private_name(separator+1)) return 0;
    wcscpy_s(parent,1024,wide);parent[separator-wide+(separator==wide+2?1:0)]=0;
    HANDLE handles[128];unsigned count=0;HANDLE file=INVALID_HANDLE_VALUE;bool upload=!strcmp(kind,"upload");
    bool impersonated=user_enter();
    bool ok=impersonated && open_directory(parent,handles,&count);int outcome=0;char sha[65]={0};unsigned long long length=0;
    bool privileged_read=false;
    policy_json_bool(ticket,policy_json_field(ticket,0,"privileged_read"),&privileged_read);
    if(!ok && impersonated && !upload && privileged_read && GetLastError()==ERROR_ACCESS_DENIED) {
        for(unsigned i=0;i<count;i++)CloseHandle(handles[i]);count=0;
        RevertToSelf();impersonated=false;
        ok=user_current() && open_directory(parent,handles,&count);
    }
    PSECURITY_DESCRIPTOR descriptor=NULL;partial[0]=0;wchar_t receipt_path[1100]={0};
    if (ok && upload) {
        int roots=policy_json_field(ticket,0,"write_roots");bool allowed=fm.local_drives;
        if (roots>=0 && ticket->tokens[roots].type=='[') for (int i=roots+1;i<ticket->tokens[roots].next;i=ticket->tokens[i].next) {
            char root[4096];wchar_t value[1024];if (policy_json_string(ticket,i,root,sizeof(root)) && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,root,-1,value,1024)) {
                size_t n=wcslen(value);
                if(n>=3 && !_wcsnicmp(parent,value,n) && (n==3 || !parent[n] || parent[n]==L'\\'))allowed=true;
            }
        }
        ok=allowed && GetFileAttributesW(wide)==INVALID_FILE_ATTRIBUTES && GetLastError()==ERROR_FILE_NOT_FOUND;
        wchar_t id[40];MultiByteToWideChar(CP_UTF8,0,command->fm_operation_id,-1,id,40);
        swprintf_s(partial,1100,L"%s\\.l4fm-%s.partial",parent,id);
        /* Destination and staging are both created using the same ordinary user token.
         * Inherit the destination DACL and publish ordinary file attributes. */
        if (ok) file=CreateFileW(partial,GENERIC_READ|GENERIC_WRITE|DELETE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        ok=ok && file!=INVALID_HANDLE_VALUE;
        snprintf(suffix,sizeof(suffix),"/operations/%s/download",command->fm_operation_id);
        PolicyJson grant;char expected_sha[65];
        ok=ok && api(suffix,NULL,&response) && policy_json_parse(&grant,response,strlen(response)) &&
            policy_json_uint(&grant,policy_json_field(&grant,0,"size_bytes"),&length) && length<=67108864 &&
            policy_json_string(&grant,policy_json_field(&grant,0,"sha256"),expected_sha,sizeof(expected_sha)) && storage_io(&grant,file,false,length);
        free(response);response=NULL;
        unsigned long long actual=0;ok=ok && hash_file(file,sha,&actual) && actual==length && !strcmp(sha,expected_sha) && FlushFileBuffers(file);
        if(impersonated) {RevertToSelf();impersonated=false;}
        ok=ok && save_receipt(file,wide,command,sha,length,receipt_path);
        impersonated=user_enter();ok=ok && impersonated;
        snprintf(suffix,sizeof(suffix),"/operations/%s/commit",command->fm_operation_id);
        ok=ok && transfer_alive() && api(suffix,"{}",&response) && alive();free(response);response=NULL;
        if (ok) {
            size_t name_length=wcslen(wide)*sizeof(wchar_t);
            FILE_RENAME_INFO* rename=(FILE_RENAME_INFO*)calloc(1,sizeof(FILE_RENAME_INFO)+name_length);
            if (!rename) ok=false;
            else {
                rename->ReplaceIfExists=FALSE;rename->RootDirectory=NULL;rename->FileNameLength=(DWORD)name_length;memcpy(rename->FileName,wide,name_length);
                ok=alive() && user_current() && SetFileInformationByHandle(file,FileRenameInfo,rename,(DWORD)(sizeof(FILE_RENAME_INFO)+name_length));free(rename);
            }
        }
        if (ok) {partial[0]=0;outcome=1;}
    } else if (ok && !strcmp(kind,"download")) {
        file=CreateFileW(wide,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,NULL);
        if(file==INVALID_HANDLE_VALUE && GetLastError()==ERROR_ACCESS_DENIED && privileged_read && impersonated) {
            RevertToSelf();impersonated=false;
            if(user_current())file=CreateFileW(wide,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,NULL);
        }
        BY_HANDLE_FILE_INFORMATION info;
        ok=file!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(file,&info) && info.nNumberOfLinks==1 && !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) && hash_file(file,sha,&length);
        snprintf(suffix,sizeof(suffix),"/operations/%s/manifest",command->fm_operation_id);char manifest[256];
        snprintf(manifest,sizeof(manifest),"{\"size_bytes\":%llu,\"sha256\":\"%s\"}",length,sha);
        PolicyJson grant;
        ok=ok && api(suffix,manifest,&response) && policy_json_parse(&grant,response,strlen(response)) && storage_io(&grant,file,true,length);
        free(response);response=NULL;
        snprintf(suffix,sizeof(suffix),"/operations/%s/source-complete",command->fm_operation_id);
        ok=ok && transfer_alive() && api(suffix,"{}",&response);free(response);response=NULL;
        if (ok) outcome=2;
    }
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(partial[0] && impersonated)DeleteFileW(partial);
    if(impersonated)RevertToSelf();
    if(receipt_path[0])receipt_outcome(receipt_path,outcome?1:2);
    if(descriptor)LocalFree(descriptor);
    for(unsigned i=0;i<count;i++)CloseHandle(handles[i]);return outcome;
}

static void execute(const RpcCommand* command) {
    bool control=command->method==7023;
    EnterCriticalSection(&fm.lock);
    bool matches=!strcmp(command->session_id,fm.lease);
    unsigned generation=fm.generation;
    LeaveCriticalSection(&fm.lock);
    if (command->fm_navigation) {
        char page[23000],response[24576];
        bool ok=matches && alive() && (unsigned long long)time(NULL)<command->fm_expires_at &&
            list_directory(command->fm_path,command->fm_offset,page,sizeof(page));
        EnterCriticalSection(&fm.lock);
        ok=ok && generation==fm.generation;
        LeaveCriticalSection(&fm.lock);
        if (!ok) strcpy_s(page,sizeof(page),"{\"state\":\"failed\",\"error_code\":\"fm_path_or_session_failed\"}");
        snprintf(response,sizeof(response),"{\"v\":2,\"command_id\":\"%s\",\"lease_id\":\"%s\",%s",command->task_id,command->session_id,page+1);
        if(fm.navigation_result)fm.navigation_result(fm.context,command->task_id,response);
        return;
    }
    if (!matches && !(control && !strcmp(command->fm_action,"start"))) {
        fm.result(fm.context,command->task_id,409,"fm_stale_session");return;
    }
    if (control && strcmp(command->fm_action,"start") && !matches) {fm.result(fm.context,command->task_id,409,"fm_session_not_found");return;}
    char suffix[160],*ticket=NULL,*response=NULL;
    const char* identifier=control?command->session_id:command->fm_operation_id;
    snprintf(suffix,sizeof(suffix),"/operations/%s/ticket",identifier);
    ULONGLONG requested_at=GetTickCount64();
    bool ok=api(suffix,NULL,&ticket);PolicyJson json;unsigned long long seconds=0;
    char lease[40]={0},kind[16]={0},grant_id[40]={0};
    ok=ok && policy_json_parse(&json,ticket,strlen(ticket)) &&
        policy_json_string(&json,policy_json_field(&json,0,"lease_id"),lease,sizeof(lease)) && !strcmp(lease,command->session_id) &&
        policy_json_string(&json,policy_json_field(&json,0,"kind"),kind,sizeof(kind)) &&
        policy_json_uint(&json,policy_json_field(&json,0,"remaining_sec"),&seconds) && seconds>5 && seconds<=90 && permitted_root(&json);
    if (ok && control) {
        ok=!strcmp(kind,"session") && policy_json_string(&json,policy_json_field(&json,0,"grant_id"),grant_id,sizeof(grant_id)) && rpc_uuid(grant_id);
        if(ok && !strcmp(command->fm_action,"start")) {
            if(fm.user_token)CloseHandle(fm.user_token);
            fm.user_token=fm_desktop_token(&fm.user_session,&fm.user_authentication);
            ok=fm.user_token!=NULL;
        } else if(ok)ok=user_current();
        if (ok) {
            EnterCriticalSection(&fm.lock);
            ok=fm.connected && generation==fm.generation && requested_at+(seconds-2)*1000>GetTickCount64();
            if(ok) {strcpy_s(fm.lease,40,lease);fm.deadline=requested_at+(seconds-2)*1000;}
            LeaveCriticalSection(&fm.lock);
        }
    }
    char* body=(char*)malloc(FM_RESPONSE_LIMIT);if (!body) ok=false;
    if (ok && control) {
        ok=alive();
        if(ok) {
            size_t used=(size_t)snprintf(body,FM_RESPONSE_LIMIT,"{\"state\":\"active\",\"grant_id\":\"%s\",\"roots\":[",grant_id);
            for(unsigned r=0;r<fm.read_root_count && ok;r++) {
                char root[8192];ok=quote(fm.read_roots[r],root,sizeof(root));
                if(ok) {int added=snprintf(body+used,FM_RESPONSE_LIMIT-used,"%s%s",r?",":"",root);if(added<0 || (size_t)added>=FM_RESPONSE_LIMIT-used)ok=false;else used+=(size_t)added;}
            }
            if(ok && used+3<FM_RESPONSE_LIMIT)strcpy_s(body+used,FM_RESPONSE_LIMIT-used,"]}");else ok=false;
        }
    }
    else if (ok && command->method==7021) {
        int outcome=matches && alive()?transfer(command,&json,kind):0;
        if (outcome==2) {fm.result(fm.context,command->task_id,200,"fm_source_verified");free(body);free(ticket);return;}
        ok=outcome==1;
        if (ok) strcpy_s(body,FM_RESPONSE_LIMIT,"{\"state\":\"completed\"}");
    } else ok=false;
    if (!ok && body) strcpy_s(body,FM_RESPONSE_LIMIT,"{\"state\":\"failed\",\"error_code\":\"fm_control_or_path_failed\"}");
    snprintf(suffix,sizeof(suffix),"/operations/%s/result",identifier);
    bool ack=body && api(suffix,body,&response);
    if (!ok || !ack) {EnterCriticalSection(&fm.lock);fm.lease[0]=0;fm.deadline=0;LeaveCriticalSection(&fm.lock);}
    fm.result(fm.context,command->task_id,ok&&ack?200:409,ok&&ack?"fm_ack":"fm_failed");
    free(body);free(response);free(ticket);
}

static DWORD WINAPI worker(void* unused) {
    (void)unused;ULONGLONG last_hello=0;
    while (WaitForSingleObject(fm.stop,0)!=WAIT_OBJECT_0) {
        ULONGLONG now=GetTickCount64();bool connected;
        EnterCriticalSection(&fm.lock);connected=fm.connected;LeaveCriticalSection(&fm.lock);
        if (connected && now-last_hello>=15000) {
            last_hello=now;char hello[512],*response=NULL;
            /* Resolve from the existing local transport profile; retry while unavailable.
             * Keep the selected origin fixed for this process, including active transfers. */
            if (!fm.api[0] && config_query_fm_api_from_proxy(fm.proxy_port,fm.sn,fm.api,sizeof(fm.api))) continue;
            snprintf(hello,sizeof(hello),"{\"agent_instance_id\":\"%s\",\"agent_version\":\"%s\",\"protocol_version\":2,\"capabilities\":[\"fs.session\",\"fs.list\",\"fs.read\",\"fs.write\",\"fs.cancel\",\"fs.proxy\",\"fs.mqtt_navigation\",\"fs.write_user\",\"fs.drives\"],\"filesystem_ready\":true}",fm.instance,L4CON_APP_VERSION);
            HANDLE handles[128];unsigned count=0;DWORD desktop_session;LUID desktop_auth;
            HANDLE desktop=fm_desktop_token(&desktop_session,&desktop_auth);
            bool ready=desktop!=NULL;if(desktop)CloseHandle(desktop);
            for(unsigned i=0;i<count;i++)CloseHandle(handles[i]);
            if (!ready) {char* value=strstr(hello,"true}");if(value)strcpy_s(value,(size_t)(hello+sizeof(hello)-value),"false}");}
            api("/hello",hello,&response);free(response);
            if(!fm_busy())reconcile_receipts_at(fm.journal);
            if(!fm_busy())reconcile_receipts_at(fm.root);
        }
        RpcCommand command;bool pending;
        EnterCriticalSection(&fm.lock);pending=fm.pending;if (pending) {command=fm.command;fm.pending=false;fm.processing=true;}LeaveCriticalSection(&fm.lock);
        if (pending) {execute(&command);EnterCriticalSection(&fm.lock);fm.processing=false;LeaveCriticalSection(&fm.lock);}
        alive();WaitForSingleObject(fm.wake,250);
    }
    return 0;
}

bool fm_start(const char* sn,int proxy_port,HANDLE stop,FmResult result,void* context) {
    ZeroMemory(&fm,sizeof(fm));
    DWORD root_length=GetEnvironmentVariableW(L"L4FM_ROOT",fm.root,1024);
    if (root_length>=1024) return false;
    if (!root_length) wcscpy_s(fm.root,1024,L"C:\\l4tools\\fm");
    if (!valid_path(fm.root) || proxy_port<1 || proxy_port>65535) return false;
    if(!GetModuleFileNameW(NULL,fm.journal,1024))return false;
    wchar_t* leaf=wcsrchr(fm.journal,L'\\');if(!leaf)return false;*leaf=0;
    /* Stable suite metadata directory, outside subdirectories swapped on upgrade. */
    leaf=wcsrchr(fm.journal,L'\\');if(!leaf)return false;*leaf=0;
    HANDLE ancestors[128];unsigned ancestor_count=0;
    bool journal_ok=checked_directory(fm.journal,ancestors,&ancestor_count,false);
    if(journal_ok) {
        wcscat_s(fm.journal,1024,L"\\fm-state");
        PSECURITY_DESCRIPTOR sd=NULL;
        journal_ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",SDDL_REVISION_1,&sd,NULL)!=0;
        SECURITY_ATTRIBUTES attributes={sizeof(attributes),sd,FALSE};
        if(journal_ok && !CreateDirectoryW(fm.journal,&attributes) && GetLastError()!=ERROR_ALREADY_EXISTS)journal_ok=false;
        if(sd)LocalFree(sd);
        HANDLE directory=journal_ok?CreateFileW(fm.journal,READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL):INVALID_HANDLE_VALUE;
        BY_HANDLE_FILE_INFORMATION info;
        journal_ok=directory!=INVALID_HANDLE_VALUE && trusted_receipt(directory) && GetFileInformationByHandle(directory,&info) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
        if(directory!=INVALID_HANDLE_VALUE)CloseHandle(directory);
    }
    for(unsigned i=0;i<ancestor_count;i++)CloseHandle(ancestors[i]);
    if(!journal_ok)return false;
    fm.proxy_port=proxy_port;
    size_t length=strlen(fm.api);while (length && fm.api[length-1]=='/') fm.api[--length]=0;
    GUID guid;if (FAILED(CoCreateGuid(&guid))) return false;
    snprintf(fm.instance,40,"%08lX-%04hX-%04hX-%02X%02X-%02X%02X%02X%02X%02X%02X",guid.Data1,guid.Data2,guid.Data3,guid.Data4[0],guid.Data4[1],guid.Data4[2],guid.Data4[3],guid.Data4[4],guid.Data4[5],guid.Data4[6],guid.Data4[7]);
    InitializeCriticalSection(&fm.lock);fm.initialized=true;fm.stop=stop;fm.result=result;fm.context=context;strcpy_s(fm.sn,128,sn);
    fm.wake=CreateEventW(NULL,FALSE,FALSE,NULL);
    fm.thread=fm.wake?CreateThread(NULL,0,worker,NULL,0,NULL):NULL;
    if (!fm.thread) {if(fm.wake)CloseHandle(fm.wake);DeleteCriticalSection(&fm.lock);fm.initialized=false;return false;}
    return true;
}
bool fm_enqueue(const RpcCommand* command,bool console_busy) {
    if (!fm.initialized || console_busy) return false;
    EnterCriticalSection(&fm.lock);bool ok=!fm.pending && fm.connected;
    if (command->method==7023 && !strcmp(command->fm_action,"start") && fm.lease[0] && strcmp(command->session_id,fm.lease)) ok=false;
    if (ok) {fm.command=*command;fm.pending=true;SetEvent(fm.wake);}LeaveCriticalSection(&fm.lock);return ok;
}
void fm_set_navigation_result(FmNavigationResult result) {fm.navigation_result=result;}
bool fm_navigation(const char* payload,size_t length,bool console_busy) {
    if(!fm.initialized || length>8192) return false;
    PolicyJson json;RpcCommand command;ZeroMemory(&command,sizeof(command));
    unsigned long long version=0,offset=0;char action[16];
    if(!policy_json_parse(&json,payload,length) ||
       !policy_json_uint(&json,policy_json_field(&json,0,"v"),&version) || version!=2 ||
       !policy_json_string(&json,policy_json_field(&json,0,"action"),action,sizeof(action)) || strcmp(action,"list") ||
       !policy_json_string(&json,policy_json_field(&json,0,"command_id"),command.task_id,sizeof(command.task_id)) || !rpc_uuid(command.task_id) ||
       !policy_json_string(&json,policy_json_field(&json,0,"lease_id"),command.session_id,sizeof(command.session_id)) || !rpc_uuid(command.session_id) ||
       !policy_json_string(&json,policy_json_field(&json,0,"path"),command.fm_path,sizeof(command.fm_path)) ||
       !policy_json_uint(&json,policy_json_field(&json,0,"offset"),&offset) || offset>1000000 ||
       !policy_json_uint(&json,policy_json_field(&json,0,"expires_at"),&command.fm_expires_at) ||
       command.fm_expires_at<=(unsigned long long)time(NULL) || command.fm_expires_at>(unsigned long long)time(NULL)+10) return false;
    command.method=7020;command.fm_navigation=true;command.fm_offset=(unsigned)offset;
    return fm_enqueue(&command,console_busy);
}
void fm_shutdown(void) {
    if (!fm.initialized) return;
    fm_connection(false);SetEvent(fm.wake);
    if(WaitForSingleObject(fm.thread,5000)!=WAIT_OBJECT_0)return; /* Owned child exits immediately. */
    CloseHandle(fm.thread);CloseHandle(fm.wake);DeleteCriticalSection(&fm.lock);fm.initialized=false;
}
