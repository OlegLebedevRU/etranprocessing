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

#define FM_RESPONSE_LIMIT 65536
static struct {
    CRITICAL_SECTION lock;
    HANDLE thread,wake,stop;
    bool initialized,connected,pending,processing;
    RpcCommand command;
    char sn[128],api[768],instance[40],lease[40];
    wchar_t root[1024];
    ULONGLONG deadline;
    FmResult result;
    void* context;
} fm;

static PCCERT_CONTEXT certificate(void) {
    cert_info info={0};wchar_t sn[128];
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,fm.sn,-1,sn,128)) return NULL;
    cert_state state=cert_discover(sn,&info);
    if (state!=CERT_VALID && state!=CERT_EXPIRING) return NULL;
    BYTE hash[20];DWORD size=sizeof(hash);
    if (!CryptStringToBinaryA(info.thumbprint_hex,0,CRYPT_STRING_HEX,hash,&size,NULL,NULL)) return NULL;
    CRYPT_HASH_BLOB blob={size,hash};
    HCERTSTORE store=CertOpenStore(CERT_STORE_PROV_SYSTEM_W,0,0,CERT_SYSTEM_STORE_LOCAL_MACHINE|CERT_STORE_READONLY_FLAG,L"MY");
    if (!store) return NULL;
    PCCERT_CONTEXT cert=CertFindCertificateInStore(store,X509_ASN_ENCODING,0,CERT_FIND_SHA1_HASH,&blob,NULL);
    CertCloseStore(store,0);
    if (cert && (!cert_is_leo4_issuer(cert) || CertVerifyTimeValidity(NULL,cert->pCertInfo))) {
        CertFreeCertificateContext(cert);return NULL;
    }
    return cert;
}

/* Strict HTTPS, no redirects, bounded metadata, no terminal key on storage calls. */
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
    if (!WinHttpCrackUrl(url,0,0,&parts) || parts.nScheme!=INTERNET_SCHEME_HTTPS || parts.dwExtraInfoLength || parts.dwUserNameLength || parts.dwPasswordLength) return false;
    PCCERT_CONTEXT cert=certificate();if (!cert) return false;
    HINTERNET session=WinHttpOpen(L"l4con FM",WINHTTP_ACCESS_TYPE_NO_PROXY,NULL,NULL,0);
    HINTERNET connection=session?WinHttpConnect(session,host,parts.nPort,0):NULL;
    HINTERNET request=connection?WinHttpOpenRequest(connection,body?L"POST":L"GET",path,NULL,NULL,NULL,WINHTTP_FLAG_SECURE):NULL;
    bool ok=false;char* data=NULL;DWORD used=0;
    ULONGLONG deadline=GetTickCount64()+5000;
    if (request) {
        DWORD disabled=WINHTTP_DISABLE_REDIRECTS;
        ok=WinHttpSetTimeouts(request,2000,2000,2000,2000) &&
           WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)) &&
           WinHttpSetOption(request,WINHTTP_OPTION_CLIENT_CERT_CONTEXT,(void*)cert,sizeof(CERT_CONTEXT)) &&
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
    CertFreeCertificateContext(cert);return ok;
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
    if (!connected) {fm.lease[0]=0;fm.deadline=0;}
    LeaveCriticalSection(&fm.lock);
    if (!connected && fm.thread) CancelSynchronousIo(fm.thread);
}
void fm_tick(void) {
    if (!fm.initialized) return;
    bool expired;EnterCriticalSection(&fm.lock);
    expired=fm.lease[0] && GetTickCount64()>=fm.deadline;
    if (expired) {fm.lease[0]=0;fm.deadline=0;}
    LeaveCriticalSection(&fm.lock);
    if (expired && fm.thread) CancelSynchronousIo(fm.thread);
}

/* Reject Win32 aliases, ADS, device/UNC paths and ambiguous normalization. */
static bool valid_path(const wchar_t* path) {
    if (!path || !iswalpha(path[0]) || path[1]!=L':' || path[2]!=L'\\' || wcslen(path)>1000) return false;
    const wchar_t* part=path+3;
    for (const wchar_t* p=part;;p++) {
        if (*p && (*p<L' ' || wcschr(L"/:*?\"<>|",*p))) return false;
        if (*p==L'\\' || !*p) {
            size_t length=(size_t)(p-part);
            if (!length || part[length-1]==L'.' || part[length-1]==L' ') return false;
            wchar_t name[1024];wcsncpy_s(name,1024,part,length);
            if (!_wcsicmp(name,L"Windows") || !_wcsicmp(name,L".ssh") || !_wcsicmp(name,L".git") || !_wcsicmp(name,L"Crypto") || !_wcsicmp(name,L"Protect")) return false;
            wchar_t* dot=wcschr(name,L'.');if (dot) *dot=0;
            if (!_wcsicmp(name,L"CON") || !_wcsicmp(name,L"PRN") || !_wcsicmp(name,L"AUX") || !_wcsicmp(name,L"NUL") ||
                (wcslen(name)==4 && (!_wcsnicmp(name,L"COM",3) || !_wcsnicmp(name,L"LPT",3)) && name[3]>=L'1' && name[3]<=L'9')) return false;
            if (!*p) break;part=p+1;
        }
    }
    return true;
}
/* Hold every ancestor without FILE_SHARE_DELETE: reparse/rename races fail closed. */
static bool open_directory(const wchar_t* path,HANDLE handles[128],unsigned* count) {
    *count=0;
    if (!valid_path(path) || !valid_path(fm.root)) return false;
    size_t root_length=wcslen(fm.root);
    if (_wcsnicmp(path,fm.root,root_length) || (path[root_length] && path[root_length]!=L'\\')) return false;
    wchar_t prefix[1024];wcscpy_s(prefix,1024,path);
    size_t length=wcslen(path);
    for (size_t i=3;i<=length;i++) {
        if (path[i] && path[i]!=L'\\') continue;
        wchar_t saved=prefix[i];prefix[i]=0;
        HANDLE handle=CreateFileW(prefix,FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        prefix[i]=saved;
        if (handle==INVALID_HANDLE_VALUE || *count>=128) {if(handle!=INVALID_HANDLE_VALUE) CloseHandle(handle);return false;}
        handles[(*count)++]=handle;
        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(handle,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        wchar_t resolved[1100],expected[1100];
        prefix[i]=0;swprintf_s(expected,1100,L"\\\\?\\%s",prefix);prefix[i]=saved;
        DWORD got=GetFinalPathNameByHandleW(handle,resolved,1100,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if (!got || got>=1100 || _wcsicmp(resolved,expected)) return false;
    }
    return true;
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

static bool list_directory(const char* path,unsigned offset,char* result,size_t capacity) {
    wchar_t wide[1024],pattern[1030];HANDLE handles[128];unsigned count=0;
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,1024)) return false;
    bool ok=open_directory(wide,handles,&count);HANDLE find=INVALID_HANDLE_VALUE;
    size_t used=0;unsigned entries=0,visible=0;bool more=false;
    if (ok) {
        swprintf_s(pattern,1030,L"%s\\*",wide);WIN32_FIND_DATAW entry;
        find=FindFirstFileW(pattern,&entry);
        if (find==INVALID_HANDLE_VALUE && GetLastError()!=ERROR_FILE_NOT_FOUND) ok=false;
        used=(size_t)snprintf(result,capacity,"{\"state\":\"completed\",\"entries\":[");
        if (find!=INVALID_HANDLE_VALUE) do {
            if (!wcscmp(entry.cFileName,L".") || !wcscmp(entry.cFileName,L"..")) continue;
            if (!alive()) {ok=false;break;}
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
    for (unsigned i=0;i<count;i++) CloseHandle(handles[i]);return ok;
}

static bool permitted_root(const PolicyJson* json) {
    int roots=policy_json_field(json,0,"read_roots");
    if (roots<0 || json->tokens[roots].type!='[') return false;
    for (int i=roots+1;i<json->tokens[roots].next;i=json->tokens[i].next) {
        char path[4096];wchar_t wide[1024];
        if (policy_json_string(json,i,path,sizeof(path)) && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,1024) && !_wcsicmp(wide,fm.root)) return true;
    }
    return false;
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

/* Direct S3 stream. No client certificate/cookie/auth header/redirect is sent. */
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
    HINTERNET session=WinHttpOpen(L"l4con FM storage",WINHTTP_ACCESS_TYPE_NO_PROXY,NULL,NULL,0);
    HINTERNET connection=session?WinHttpConnect(session,host,parts.nPort,0):NULL;
    HINTERNET request=connection?WinHttpOpenRequest(connection,upload?L"PUT":L"GET",path,NULL,NULL,NULL,WINHTTP_FLAG_SECURE):NULL;
    bool ok=false;ULONGLONG deadline=GetTickCount64()+45000;unsigned long long total=0;BYTE buffer[65536];
    if (request) {
        DWORD disabled=WINHTTP_DISABLE_REDIRECTS;
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
    return !_wcsicmp(name,L"Windows") || !_wcsicmp(name,L"Crypto") || !_wcsicmp(name,L"Protect") || !_wcsicmp(name,L".ssh") || !_wcsicmp(name,L".git") || !_wcsicmp(name,L".env") || !_wcsnicmp(name,L".l4fm-",6) ||
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
    swprintf_s(receipt_path,1100,L"%s\\.l4fm-%s.receipt",fm.root,id);
    PSECURITY_DESCRIPTOR descriptor=NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&descriptor,NULL)) return false;
    SECURITY_ATTRIBUTES attributes={sizeof(attributes),descriptor,FALSE};
    HANDLE handle=CreateFileW(receipt_path,GENERIC_WRITE,0,&attributes,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_WRITE_THROUGH,NULL);
    LocalFree(descriptor);if(handle==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=WriteFile(handle,&receipt,sizeof(receipt),&written,NULL) && written==sizeof(receipt) && FlushFileBuffers(handle);
    CloseHandle(handle);return ok;
}

static void reconcile_receipts(void) {
    wchar_t pattern[1100];swprintf_s(pattern,1100,L"%s\\.l4fm-*.receipt",fm.root);
    HANDLE roots[128];unsigned count=0;
    if(!open_directory(fm.root,roots,&count)) {for(unsigned i=0;i<count;i++)CloseHandle(roots[i]);return;}
    WIN32_FIND_DATAW entry;HANDLE find=FindFirstFileW(pattern,&entry);unsigned scanned=0;
    if(find!=INVALID_HANDLE_VALUE) do {
        if (WaitForSingleObject(fm.stop,0)==WAIT_OBJECT_0) break;
        if(++scanned>64 || entry.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) continue;
        wchar_t receipt_path[1100];swprintf_s(receipt_path,1100,L"%s\\%s",fm.root,entry.cFileName);
        HANDLE handle=CreateFileW(receipt_path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        CommitReceipt receipt={0};DWORD read=0;LARGE_INTEGER size;
        bool valid=handle!=INVALID_HANDLE_VALUE && GetFileSizeEx(handle,&size) && size.QuadPart==sizeof(receipt) && ReadFile(handle,&receipt,sizeof(receipt),&read,NULL) && read==sizeof(receipt);
        if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
        if(!valid || receipt.magic!=0x464D0001 || !memchr(receipt.id,0,40) || !memchr(receipt.sha,0,65) || !wmemchr(receipt.path,0,1024) || !rpc_uuid(receipt.id) || !valid_path(receipt.path))continue;
        wchar_t parent[1024];wcscpy_s(parent,1024,receipt.path);wchar_t* separator=wcsrchr(parent,L'\\');if(!separator)continue;*separator=0;
        HANDLE ancestors[128];unsigned ancestors_count=0;valid=open_directory(parent,ancestors,&ancestors_count);
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
    } while(scanned<64 && FindNextFileW(find,&entry));
    if(find!=INVALID_HANDLE_VALUE)FindClose(find);
    for(unsigned i=0;i<count;i++)CloseHandle(roots[i]);
}

/* Return 2 after terminal->S3 verification, 1 after atomic terminal publish. */
static int transfer(const RpcCommand* command,const PolicyJson* ticket,const char* kind) {
    char path[4096],suffix[160],*response=NULL;wchar_t wide[1024],parent[1024],partial[1100];
    if (!policy_json_string(ticket,policy_json_field(ticket,0,"path"),path,sizeof(path)) ||
        !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,1024) || !valid_path(wide)) return 0;
    wchar_t* separator=wcsrchr(wide,L'\\');if (!separator || private_name(separator+1)) return 0;
    wcscpy_s(parent,1024,wide);parent[separator-wide]=0;
    HANDLE handles[128];unsigned count=0;HANDLE file=INVALID_HANDLE_VALUE;bool upload=!strcmp(kind,"upload");
    bool ok=open_directory(parent,handles,&count);int outcome=0;char sha[65]={0};unsigned long long length=0;
    PSECURITY_DESCRIPTOR descriptor=NULL;partial[0]=0;wchar_t receipt_path[1100]={0};
    if (ok && upload) {
        int roots=policy_json_field(ticket,0,"write_roots");bool allowed=false;
        if (roots>=0 && ticket->tokens[roots].type=='[') for (int i=roots+1;i<ticket->tokens[roots].next;i=ticket->tokens[i].next) {
            char root[4096];wchar_t value[1024];if (policy_json_string(ticket,i,root,sizeof(root)) && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,root,-1,value,1024) && !_wcsicmp(value,fm.root)) allowed=true;
        }
        ok=allowed && GetFileAttributesW(wide)==INVALID_FILE_ATTRIBUTES && GetLastError()==ERROR_FILE_NOT_FOUND;
        wchar_t id[40];MultiByteToWideChar(CP_UTF8,0,command->fm_operation_id,-1,id,40);
        swprintf_s(partial,1100,L"%s\\.l4fm-%s.partial",parent,id);
        ok=ok && ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&descriptor,NULL);
        SECURITY_ATTRIBUTES attributes={sizeof(attributes),descriptor,FALSE};
        if (ok) file=CreateFileW(partial,GENERIC_READ|GENERIC_WRITE|DELETE,0,&attributes,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_WRITE_THROUGH|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        ok=ok && file!=INVALID_HANDLE_VALUE;
        snprintf(suffix,sizeof(suffix),"/operations/%s/download",command->fm_operation_id);
        PolicyJson grant;char expected_sha[65];
        ok=ok && api(suffix,NULL,&response) && policy_json_parse(&grant,response,strlen(response)) &&
            policy_json_uint(&grant,policy_json_field(&grant,0,"size_bytes"),&length) && length<=67108864 &&
            policy_json_string(&grant,policy_json_field(&grant,0,"sha256"),expected_sha,sizeof(expected_sha)) && storage_io(&grant,file,false,length);
        free(response);response=NULL;
        unsigned long long actual=0;ok=ok && hash_file(file,sha,&actual) && actual==length && !strcmp(sha,expected_sha) && FlushFileBuffers(file);
        ok=ok && save_receipt(file,wide,command,sha,length,receipt_path);
        snprintf(suffix,sizeof(suffix),"/operations/%s/commit",command->fm_operation_id);
        ok=ok && transfer_alive() && api(suffix,"{}",&response) && alive();free(response);response=NULL;
        if (ok) {
            size_t name_length=wcslen(wide)*sizeof(wchar_t);
            FILE_RENAME_INFO* rename=(FILE_RENAME_INFO*)calloc(1,sizeof(FILE_RENAME_INFO)+name_length);
            if (!rename) ok=false;
            else {
                rename->ReplaceIfExists=FALSE;rename->RootDirectory=NULL;rename->FileNameLength=(DWORD)name_length;memcpy(rename->FileName,wide,name_length);
                ok=alive() && SetFileInformationByHandle(file,FileRenameInfo,rename,(DWORD)(sizeof(FILE_RENAME_INFO)+name_length));free(rename);
            }
        }
        if (ok) {partial[0]=0;outcome=1;}
    } else if (ok && !strcmp(kind,"download")) {
        file=CreateFileW(wide,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,NULL);
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
    if(receipt_path[0])receipt_outcome(receipt_path,outcome?1:2);
    if(partial[0])DeleteFileW(partial);
    if(descriptor)LocalFree(descriptor);
    for(unsigned i=0;i<count;i++)CloseHandle(handles[i]);return outcome;
}

static void execute(const RpcCommand* command) {
    bool control=command->method==7023;
    bool stop=control && !strcmp(command->fm_action,"stop");
    EnterCriticalSection(&fm.lock);
    bool matches=!strcmp(command->session_id,fm.lease);
    LeaveCriticalSection(&fm.lock);
    if (!matches && !(control && !strcmp(command->fm_action,"start"))) {
        fm.result(fm.context,command->task_id,409,"fm_stale_session");return;
    }
    if (stop || command->method==7022) {
        if (matches) {EnterCriticalSection(&fm.lock);fm.lease[0]=0;fm.deadline=0;LeaveCriticalSection(&fm.lock);}
        fm.result(fm.context,command->task_id,matches?200:404,matches?"fm_stopped":"fm_session_not_found");return;
    }
    if (control && strcmp(command->fm_action,"start") && !matches) {fm.result(fm.context,command->task_id,409,"fm_session_not_found");return;}
    char suffix[160],*ticket=NULL,*response=NULL;
    const char* identifier=control?command->session_id:command->fm_operation_id;
    snprintf(suffix,sizeof(suffix),"/operations/%s/ticket",identifier);
    bool ok=api(suffix,NULL,&ticket);PolicyJson json;unsigned long long seconds=0;
    char lease[40]={0},kind[16]={0},path[4096]={0},grant_id[40]={0};
    ok=ok && policy_json_parse(&json,ticket,strlen(ticket)) &&
        policy_json_string(&json,policy_json_field(&json,0,"lease_id"),lease,sizeof(lease)) && !strcmp(lease,command->session_id) &&
        policy_json_string(&json,policy_json_field(&json,0,"kind"),kind,sizeof(kind)) &&
        policy_json_uint(&json,policy_json_field(&json,0,"remaining_sec"),&seconds) && seconds>5 && seconds<=90 && permitted_root(&json);
    if (ok && control) {
        ok=!strcmp(kind,"session") && policy_json_string(&json,policy_json_field(&json,0,"grant_id"),grant_id,sizeof(grant_id)) && rpc_uuid(grant_id);
        if (ok) {EnterCriticalSection(&fm.lock);strcpy_s(fm.lease,40,lease);fm.deadline=GetTickCount64()+(seconds-2)*1000;LeaveCriticalSection(&fm.lock);}
    }
    char* body=(char*)malloc(FM_RESPONSE_LIMIT);if (!body) ok=false;
    if (ok && control) {ok=alive();if(ok)snprintf(body,FM_RESPONSE_LIMIT,"{\"state\":\"active\",\"grant_id\":\"%s\"}",grant_id);}
    else if (ok && command->method==7021) {
        int outcome=matches && alive()?transfer(command,&json,kind):0;
        if (outcome==2) {fm.result(fm.context,command->task_id,200,"fm_source_verified");free(body);free(ticket);return;}
        ok=outcome==1;
        if (ok) strcpy_s(body,FM_RESPONSE_LIMIT,"{\"state\":\"completed\"}");
    } else if (ok) {
        unsigned long long offset=0;
        ok=matches && alive() && !strcmp(kind,"list") && command->method==7020 &&
           policy_json_uint(&json,policy_json_field(&json,0,"offset"),&offset) && offset<=1000000 &&
           policy_json_string(&json,policy_json_field(&json,0,"path"),path,sizeof(path)) && list_directory(path,(unsigned)offset,body,FM_RESPONSE_LIMIT);
    }
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
            snprintf(hello,sizeof(hello),"{\"agent_instance_id\":\"%s\",\"agent_version\":\"%s\",\"protocol_version\":1,\"capabilities\":[\"fs.session\",\"fs.list\",\"fs.read\",\"fs.write\",\"fs.cancel\"],\"filesystem_ready\":true}",fm.instance,L4CON_APP_VERSION);
            HANDLE handles[128];unsigned count=0;bool ready=open_directory(fm.root,handles,&count);
            for(unsigned i=0;i<count;i++)CloseHandle(handles[i]);
            if (!ready) {char* value=strstr(hello,"true}");if(value)strcpy_s(value,(size_t)(hello+sizeof(hello)-value),"false}");}
            api("/hello",hello,&response);free(response);reconcile_receipts();
        }
        RpcCommand command;bool pending;
        EnterCriticalSection(&fm.lock);pending=fm.pending;if (pending) {command=fm.command;fm.pending=false;fm.processing=true;}LeaveCriticalSection(&fm.lock);
        if (pending) {execute(&command);EnterCriticalSection(&fm.lock);fm.processing=false;LeaveCriticalSection(&fm.lock);}
        alive();WaitForSingleObject(fm.wake,250);
    }
    return 0;
}

bool fm_start(const char* sn,HANDLE stop,FmResult result,void* context) {
    ZeroMemory(&fm,sizeof(fm));
    DWORD api_length=GetEnvironmentVariableA("L4FM_API_URL",fm.api,sizeof(fm.api));
    DWORD root_length=GetEnvironmentVariableW(L"L4FM_ROOT",fm.root,1024);
    if (!api_length || api_length>=sizeof(fm.api) || !root_length || root_length>=1024 || !valid_path(fm.root)) return false;
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
    if ((command->method==7022 || (command->method==7023 && !strcmp(command->fm_action,"stop"))) && !strcmp(command->session_id,fm.lease)) {
        fm.lease[0]=0;fm.deadline=0;
        LeaveCriticalSection(&fm.lock);CancelSynchronousIo(fm.thread);fm.result(fm.context,command->task_id,200,"fm_stop_requested");return true;
    }
    if (command->method==7023 && !strcmp(command->fm_action,"start") && fm.lease[0] && strcmp(command->session_id,fm.lease)) ok=false;
    if (ok) {fm.command=*command;fm.pending=true;SetEvent(fm.wake);}LeaveCriticalSection(&fm.lock);return ok;
}
void fm_shutdown(void) {
    if (!fm.initialized) return;
    fm_connection(false);SetEvent(fm.wake);WaitForSingleObject(fm.thread,INFINITE);
    CloseHandle(fm.thread);CloseHandle(fm.wake);DeleteCriticalSection(&fm.lock);fm.initialized=false;
}
