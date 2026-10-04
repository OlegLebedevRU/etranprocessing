/* Authenticated renewal: no credential in URL/argv/output, no anonymous TLS retry. */
#include "renewal.h"
#include "http_client.h"
#include "xml_utils.h"
#include "cng_crypto.h"
#include "cert_store.h"
#include "cert_discovery.h"
#include "../../l4con/src/event_ipc.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define PENDING_VERSION 1
#define RENEW_BUDGET 90000

typedef struct {
    DWORD version;
    BYTE pin_hash[32];
    WCHAR key[128];
    char csr[32769];
    char sign[128];
    char email[256];
    char pkcs7[65537];
} PendingRenewal;

HANDLE enrollment_lock(void) {
    PSECURITY_DESCRIPTOR sd=NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",SDDL_REVISION_1,&sd,NULL)) return NULL;
    SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};
    HANDLE lock=CreateMutexW(&sa,FALSE,L"Global\\Leo4_Certificate_Enrollment");
    LocalFree(sd);
    if (!lock) return NULL;
    DWORD result=WaitForSingleObject(lock,0);
    if (result!=WAIT_OBJECT_0 && result!=WAIT_ABANDONED) { CloseHandle(lock); return NULL; }
    return lock;
}

static bool pipe_io(HANDLE pipe, bool writing, void* buffer, DWORD size) {
    OVERLAPPED op={0}; op.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);
    if (!op.hEvent) return false;
    DWORD bytes=0;
    BOOL ok=writing?WriteFile(pipe,buffer,size,&bytes,&op):ReadFile(pipe,buffer,size,&bytes,&op);
    if (!ok && GetLastError()==ERROR_IO_PENDING) {
        if (WaitForSingleObject(op.hEvent,1000)!=WAIT_OBJECT_0) {
            CancelIoEx(pipe,&op); GetOverlappedResult(pipe,&op,&bytes,TRUE); ok=FALSE;
        } else ok=GetOverlappedResult(pipe,&op,&bytes,FALSE);
    }
    CloseHandle(op.hEvent); return ok && bytes==size;
}
static bool authorize(void) {
    if (!WaitNamedPipeW(EVENT_PIPE_NAME,500)) return false;
    HANDLE pipe=CreateFileW(EVENT_PIPE_NAME,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
    if (pipe==INVALID_HANDLE_VALUE) return false;
    ULONG pid=0; bool ok=false;
    HANDLE process=NULL;
    wchar_t expected[MAX_PATH],actual[MAX_PATH]; DWORD size=MAX_PATH;
    DWORD length=GetModuleFileNameW(NULL,expected,MAX_PATH);
    wchar_t* slash=wcsrchr(expected,L'\\');
    if (length && length<MAX_PATH && slash && GetNamedPipeServerProcessId(pipe,&pid)) {
        *slash=0; slash=wcsrchr(expected,L'\\');
        if (slash) {
            *slash=0;
            process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
            if (!wcscat_s(expected,MAX_PATH,L"\\l4con\\l4con.exe") && process &&
                QueryFullProcessImageNameW(process,0,actual,&size) && !_wcsicmp(actual,expected)) {
                UserEvent request={0}; request.version=2; request.code=7011;
                int32_t result=EVENT_DENIED;
                ok=pipe_io(pipe,true,&request,sizeof(request)) &&
                   pipe_io(pipe,false,&result,sizeof(result)) && result==EVENT_OK;
                unsigned char receipt=1; pipe_io(pipe,true,&receipt,1);
            }
        }
    }
    if (process) CloseHandle(process);
    CloseHandle(pipe); return ok;
}

static bool hash_pin(const char* pin,BYTE out[32]) {
    DWORD size=32;
    return CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM,0,NULL,(BYTE*)pin,6,out,&size) && size==32;
}
static HKEY pending_key(const char* sn) {
    wchar_t name[256];
    for (const char* p=sn;*p;p++) if (!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||*p=='-')) return NULL;
    if (!*sn || strlen(sn)>64) return NULL;
    swprintf_s(name,256,L"SOFTWARE\\Leo4\\l4pin\\Renewal\\%hs",sn);
    PSECURITY_DESCRIPTOR sd=NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;KA;;;SY)(A;;KA;;;BA)",SDDL_REVISION_1,&sd,NULL)) return NULL;
    SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE}; HKEY key=NULL;
    LONG result=RegCreateKeyExW(HKEY_LOCAL_MACHINE,name,0,NULL,0,KEY_ALL_ACCESS|KEY_WOW64_64KEY,&sa,&key,NULL);
    if (result==ERROR_SUCCESS) result=RegSetKeySecurity(key,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd);
    LocalFree(sd);
    if (result!=ERROR_SUCCESS) { if (key) RegCloseKey(key); return NULL; }
    return key;
}
/* DPAPI additionally protects persisted recovery data; private key stays in CNG. */
static int pending_load(HKEY key,PendingRenewal* state) {
    DWORD type=0,size=0;
    LONG status=RegQueryValueExW(key,L"Pending",NULL,&type,NULL,&size);
    if (status==ERROR_FILE_NOT_FOUND) return 0;
    if (status!=ERROR_SUCCESS || type!=REG_BINARY || size>sizeof(*state)+4096) return -1;
    BYTE* bytes=(BYTE*)malloc(size); if (!bytes) return -1;
    DATA_BLOB input={size,bytes},output={0};
    bool ok=RegQueryValueExW(key,L"Pending",NULL,&type,bytes,&size)==ERROR_SUCCESS &&
        CryptUnprotectData(&input,NULL,NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&output) && output.cbData==sizeof(*state);
    if (ok) memcpy(state,output.pbData,sizeof(*state));
    if (output.pbData) { SecureZeroMemory(output.pbData,output.cbData); LocalFree(output.pbData); }
    SecureZeroMemory(bytes,size); free(bytes);
    if (!ok || state->version!=PENDING_VERSION || !memchr(state->csr,0,sizeof(state->csr)) ||
        !memchr(state->sign,0,sizeof(state->sign)) || !memchr(state->email,0,sizeof(state->email)) ||
        !memchr(state->pkcs7,0,sizeof(state->pkcs7)) || !wmemchr(state->key,0,128)) return -1;
    return 1;
}
static bool pending_save(HKEY key,const PendingRenewal* state) {
    DATA_BLOB input={sizeof(*state),(BYTE*)state},output={0};
    if (!CryptProtectData(&input,L"Leo4 renewal recovery",NULL,NULL,NULL,
        CRYPTPROTECT_UI_FORBIDDEN|CRYPTPROTECT_LOCAL_MACHINE,&output)) return false;
    bool ok=RegSetValueExW(key,L"Pending",0,REG_BINARY,output.pbData,output.cbData)==ERROR_SUCCESS;
    if (ok) ok=RegFlushKey(key)==ERROR_SUCCESS;
    SecureZeroMemory(output.pbData,output.cbData); LocalFree(output.pbData); return ok;
}

static PCCERT_CONTEXT active_certificate(cert_info* info) {
    cert_state state=cert_discover(NULL,info);
    if (state!=CERT_VALID && state!=CERT_EXPIRING) return NULL;
    BYTE hash[20]; DWORD size=sizeof(hash);
    if (!CryptStringToBinaryA(info->thumbprint_hex,0,CRYPT_STRING_HEX,hash,&size,NULL,NULL)) return NULL;
    CRYPT_HASH_BLOB blob={size,hash};
    HCERTSTORE store=CertOpenStore(CERT_STORE_PROV_SYSTEM_W,0,0,
        CERT_SYSTEM_STORE_LOCAL_MACHINE|CERT_STORE_READONLY_FLAG,L"MY");
    if (!store) return NULL;
    PCCERT_CONTEXT cert=CertFindCertificateInStore(store,X509_ASN_ENCODING,0,CERT_FIND_SHA1_HASH,&blob,NULL);
    CertCloseStore(store,0);
    if (cert && CertVerifyTimeValidity(NULL,cert->pCertInfo)!=0) { CertFreeCertificateContext(cert); cert=NULL; }
    return cert;
}

static int remaining(ULONGLONG deadline) {
    ULONGLONG now=GetTickCount64(); return now<deadline ? (int)(deadline-now):0;
}
/* Bounded response, redirects disabled, Schannel server validation never bypassed. */
static bool post(const char* base,PCCERT_CONTEXT cert,const char* body,ULONGLONG deadline,char** response) {
    *response=NULL;
    wchar_t url[1024],host[256],path[768];
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,base,-1,url,1024)) return false;
    size_t len=wcslen(url); while (len && url[len-1]==L'/') url[--len]=0;
    if (wcscat_s(url,1024,L"/renew")) return false;
    URL_COMPONENTS parts={sizeof(parts)};
    parts.dwExtraInfoLength=(DWORD)-1;parts.dwUserNameLength=(DWORD)-1;parts.dwPasswordLength=(DWORD)-1;
    parts.lpszHostName=host;parts.dwHostNameLength=256;parts.lpszUrlPath=path;parts.dwUrlPathLength=768;
    if (!WinHttpCrackUrl(url,0,0,&parts) || parts.dwExtraInfoLength || parts.dwUserNameLength || parts.dwPasswordLength) return false;
    bool secure=parts.nScheme==INTERNET_SCHEME_HTTPS;
    bool loopback=!_wcsicmp(host,L"127.0.0.1") || !_wcsicmp(host,L"localhost") || !_wcsicmp(host,L"[::1]");
    if (!secure && (!loopback || parts.nScheme!=INTERNET_SCHEME_HTTP)) return false;
    int budget=remaining(deadline); if (!budget) return false;
    HINTERNET session=WinHttpOpen(L"l4pin authenticated renewal",WINHTTP_ACCESS_TYPE_NO_PROXY,NULL,NULL,0);
    if (!session) return false;
    WinHttpSetTimeouts(session,budget,budget,budget,budget);
    HINTERNET connection=WinHttpConnect(session,host,parts.nPort,0);
    HINTERNET request=connection?WinHttpOpenRequest(connection,L"POST",path,NULL,NULL,NULL,secure?WINHTTP_FLAG_SECURE:0):NULL;
    bool ok=false; char* data=NULL;size_t used=0;
    if (request) {
        DWORD disabled=WINHTTP_DISABLE_REDIRECTS;
        ok=WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)) &&
           (!secure || WinHttpSetOption(request,WINHTTP_OPTION_CLIENT_CERT_CONTEXT,(void*)cert,sizeof(CERT_CONTEXT))) &&
           WinHttpSendRequest(request,L"Content-Type: application/json\r\n",(DWORD)-1L,(void*)body,(DWORD)strlen(body),(DWORD)strlen(body),0) &&
           remaining(deadline)>0 &&
           WinHttpSetTimeouts(request,remaining(deadline),remaining(deadline),remaining(deadline),remaining(deadline)) &&
           WinHttpReceiveResponse(request,NULL);
        DWORD status=0,size=sizeof(status);
        ok=ok && WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,NULL,&status,&size,NULL) && status==200;
        data=(char*)malloc(131073);
        ok=ok && data!=NULL;
        while (ok) {
            int timeout=remaining(deadline);DWORD available=0,read=0;
            if (!timeout || !WinHttpSetTimeouts(request,timeout,timeout,timeout,timeout) || !WinHttpQueryDataAvailable(request,&available)) {ok=false;break;}
            if (!available) break;
            if (available>131072-used || !WinHttpReadData(request,data+used,available,&read) || !read) {ok=false;break;}
            used+=read;
        }
        if (ok) { data[used]=0;*response=data;data=NULL; }
        WinHttpCloseHandle(request);
    }
    free(data);if (connection) WinHttpCloseHandle(connection);WinHttpCloseHandle(session);return ok;
}
int renewal_run(const char* pin,const char* url) {
    if (strlen(pin)!=6) return 2;
    for (int n=0;n<6;n++) if (pin[n]<'0'||pin[n]>'9') return 2;
    if (!authorize()) { fprintf(stderr,"Authenticated renewal requires an active l4con Job with at least 100s remaining.\n");return 3; }
    ULONGLONG deadline=GetTickCount64()+RENEW_BUDGET;
    cert_info info={0};PCCERT_CONTEXT cert=active_certificate(&info);
    if (!cert) {fprintf(stderr,"A live machine terminal certificate is required.\n");return 3;}
    HKEY key=pending_key(info.sn); PendingRenewal* pending=(PendingRenewal*)calloc(1,sizeof(PendingRenewal));
    int result=4;char* xml=NULL;BYTE hash[32];SetupResponse setup={0};
    if (!key || !pending || !hash_pin(pin,hash)) goto done;
    int loaded=pending_load(key,pending);
    if (loaded<0 || (loaded && memcmp(hash,pending->pin_hash,32))) {
        fprintf(stderr,"Unfinished renewal must be recovered with the original PIN before another renewal.\n");goto done;
    }
    if (!loaded) {
        char tosign[256];CheckResponse check={0};char body[1024];
        if (!generate_tosign(tosign,sizeof(tosign))) goto done;
        snprintf(body,sizeof(body),"{\"function\":\"check\",\"pin\":\"%s\",\"tosign\":\"%s\"}",pin,tosign);
        bool checked=post(url,cert,body,deadline,&xml) && parse_check_response(xml,&check) && check.sign[0];
        SecureZeroMemory(body,sizeof(body));free(xml);xml=NULL;
        if (!checked) goto done;
        pending->version=PENDING_VERSION;memcpy(pending->pin_hash,hash,32);
        GUID guid;if (FAILED(CoCreateGuid(&guid))) goto done;
        swprintf_s(pending->key,128,L"Leo4Renew-%08lX%04hX%04hX%02X%02X%02X%02X%02X%02X%02X%02X",
            guid.Data1,guid.Data2,guid.Data3,guid.Data4[0],guid.Data4[1],guid.Data4[2],guid.Data4[3],guid.Data4[4],guid.Data4[5],guid.Data4[6],guid.Data4[7]);
        strcpy_s(pending->sign,128,check.sign);extract_email_from_dn(check.dn,pending->email,sizeof(pending->email));
        char* dn=replace_cn_in_dn(check.dn,check.sign);char* csr=NULL;
        bool generated=dn && cng_generate_key_and_csr(NULL,pending->key,dn,2048,true,&csr);
        free(dn);
        if (!generated || !csr || strlen(csr)>=sizeof(pending->csr)) { free(csr);goto done; }
        strcpy_s(pending->csr,sizeof(pending->csr),csr);free(csr);
        if (!pending_save(key,pending)) { cng_delete_key_container(NULL,pending->key,true);goto done; }
    }
    if (!pending->pkcs7[0]) {
        size_t capacity=strlen(pending->csr)+1024;char* body=(char*)malloc(capacity);
        if (!body) goto done;
        snprintf(body,capacity,"{\"function\":\"setup\",\"pin\":\"%s\",\"cpserial\":\"%s\",\"csr\":\"%s\"}",pin,pending->sign,pending->csr);
        bool received=post(url,cert,body,deadline,&xml);
        /* One immediate retry uses identical persisted CSR, including after response loss. */
        if (!received && remaining(deadline)>5000) { free(xml);xml=NULL;received=post(url,cert,body,deadline,&xml); }
        SecureZeroMemory(body,capacity);free(body);
        if (!received || !parse_setup_response(xml,&setup) || !setup.certdata || strlen(setup.certdata)>=sizeof(pending->pkcs7)) goto done;
        strcpy_s(pending->pkcs7,sizeof(pending->pkcs7),setup.certdata);
        if (!pending_save(key,pending)) goto done;
    }
    if (!remaining(deadline)) {result=124;goto done;}
    CertDetails installed={0};
    if (!cert_store_install_pkcs7(pending->pkcs7,pending->key,true,pending->email,&installed)) {result=5;goto done;}
    if (RegDeleteValueW(key,L"Pending")!=ERROR_SUCCESS || RegFlushKey(key)!=ERROR_SUCCESS) {result=5;goto done;}
    printf("Certificate renewed: SN=%s serial=%s thumbprint=%s. Identity hot rotation follows the normal scan.\n",info.sn,installed.serial,installed.thumbprint);
    result=0;
done:
    if (result && !remaining(deadline)) result=124;
    if (result) fprintf(stderr,"Renewal incomplete; persisted CSR/key are retained for recovery. Exit=%d.\n",result);
    free(xml);free_setup_response(&setup);
    if (pending) {SecureZeroMemory(pending,sizeof(*pending));free(pending);}
    SecureZeroMemory(hash,sizeof(hash));
    if (key) RegCloseKey(key);CertFreeCertificateContext(cert);return result;
}
