/* Modeled WinHTTP response/error paths; request options are asserted exactly.
 * TLS validation is also tested independently using real WinHTTP/untrusted peer. */
#include <windows.h>
#include <winhttp.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned opened,closed,reads,options;static DWORD response_status=200;static BOOL failed_send,failed_header,failed_option,failed_sink,late;
static ULONGLONG tick;static const wchar_t* declared=L"3";static DWORD body_size=3;static const wchar_t* expected_path=L"/l4tools/metadata/catalog.json";
static HINTERNET WINAPI model_open(LPCWSTR agent,DWORD access,LPCWSTR proxy,LPCWSTR bypass,DWORD flags){
    assert(!wcscmp(agent,L"L4Tools-Update/1") && access==WINHTTP_ACCESS_TYPE_NAMED_PROXY && !wcscmp(proxy,L"127.0.0.1:18443") && !bypass && !flags);++opened;return (HINTERNET)1;
}
static BOOL WINAPI model_timeouts(HINTERNET h,int a,int b,int c,int d){assert(h && a>0 && a<=10000 && a==b && b==c && c==d);return TRUE;}
static HINTERNET WINAPI model_connect(HINTERNET h,LPCWSTR host,INTERNET_PORT port,DWORD reserved){assert(h && !wcscmp(host,L"l4tools-generic.ar.cloud.ru") && port==443 && !reserved);++opened;return (HINTERNET)2;}
static HINTERNET WINAPI model_request(HINTERNET h,LPCWSTR method,LPCWSTR path,LPCWSTR version,LPCWSTR referrer,LPCWSTR* accept,DWORD flags){
    assert(h && !wcscmp(method,L"GET") && !wcscmp(path,expected_path) && !version && !referrer && !accept && flags==WINHTTP_FLAG_SECURE);++opened;return (HINTERNET)3;
}
static BOOL WINAPI model_option(HINTERNET h,DWORD option,LPVOID data,DWORD size){assert(h && data && size==sizeof(DWORD));DWORD value=*(DWORD*)data;++options;
    if(option==WINHTTP_OPTION_SECURE_PROTOCOLS)assert(value==WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2);
    else if(option==WINHTTP_OPTION_DISABLE_FEATURE)assert(value==(WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION));
    else if(option==WINHTTP_OPTION_AUTOLOGON_POLICY)assert(value==WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH);
    else assert(0);if(failed_option){SetLastError(ERROR_INVALID_FUNCTION);return FALSE;}return TRUE;
}
static BOOL WINAPI model_send(HINTERNET h,LPCWSTR headers,DWORD size,LPVOID body,DWORD bytes,DWORD total,DWORD_PTR context){
    assert(h && !wcscmp(headers,L"Accept-Encoding: identity\r\n") && size==(DWORD)-1 && !body && !bytes && !total && !context);
    if(failed_send){SetLastError(ERROR_WINHTTP_SECURE_FAILURE);return FALSE;}return TRUE;
}
static BOOL WINAPI model_receive(HINTERNET h,LPVOID reserved){assert(h && !reserved);return TRUE;}
static BOOL WINAPI model_query(HINTERNET h,DWORD info,LPCWSTR name,LPVOID data,LPDWORD size,LPDWORD index){
    assert(h && !name && !index);if(info==(WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER)){assert(*size==sizeof(DWORD));*(DWORD*)data=response_status;return TRUE;}
    assert(info==WINHTTP_QUERY_CONTENT_LENGTH);if(failed_header){SetLastError(ERROR_WINHTTP_HEADER_NOT_FOUND);return FALSE;}
    size_t bytes=(wcslen(declared)+1)*sizeof(wchar_t);assert(*size>=bytes);memcpy(data,declared,bytes);*size=(DWORD)bytes;return TRUE;
}
static BOOL WINAPI model_read(HINTERNET h,LPVOID bytes,DWORD size,LPDWORD read){assert(h && size==16384);++reads;
    if(reads==1){*read=body_size;memset(bytes,'a',body_size<size?body_size:size);}else *read=0;
    if(late)tick=4000;return TRUE;
}
static BOOL WINAPI model_close(HINTERNET h){assert(h);++closed;return TRUE;}
static ULONGLONG model_tick(void){return tick;}
#define WinHttpOpen model_open
#define WinHttpSetTimeouts model_timeouts
#define WinHttpConnect model_connect
#define WinHttpOpenRequest model_request
#define WinHttpSetOption model_option
#define WinHttpSendRequest model_send
#define WinHttpReceiveResponse model_receive
#define WinHttpQueryHeaders model_query
#define WinHttpReadData model_read
#define WinHttpCloseHandle model_close
#define GetTickCount64 model_tick
#include "../registry_http.c"
static bool sink(const BYTE* bytes,DWORD size,void* context){assert(bytes && size);*(DWORD*)context+=size;return !failed_sink;}
static void reset(void){opened=closed=reads=options=0;tick=0;failed_send=failed_header=failed_option=failed_sink=late=FALSE;response_status=200;declared=L"3";body_size=3;expected_path=L"/l4tools/metadata/catalog.json";SetLastError(0);}
static void refused(const char* version,const char* file){BYTE* bytes=(BYTE*)1;DWORD size=99;assert(!l4_registry_metadata(18443,version,file,3000,&bytes,&size));assert(!bytes && !size && opened==closed);}
int main(void){BYTE* bytes=NULL;DWORD size=0;reset();assert(l4_registry_metadata(18443,NULL,"catalog.json",3000,&bytes,&size));assert(size==3 && !memcmp(bytes,"aaa",3) && opened==3 && closed==3 && options==3);free(bytes);
    reset();expected_path=L"/l4tools/1.13.3/l4tools-release.json";assert(l4_registry_metadata(18443,"1.13.3","l4tools-release.json",3000,&bytes,&size));free(bytes);
    reset();declared=L"384";body_size=384;expected_path=L"/l4tools/metadata/catalog.json.sig";assert(l4_registry_metadata(18443,NULL,"catalog.json.sig",3000,&bytes,&size));assert(size==384);free(bytes);
    const char* bad[]={"../root.json","https://evil/root.json","l4setup.exe","catalog.json?x=1","l4tools-layout-arm64.json","l4tools-layout-x86.zip"};
    for(unsigned i=0;i<_countof(bad);i++){reset();refused("1.13.3",bad[i]);assert(!opened);}
    const char* versions[]={"latest","1.0.0-beta","01.0.0","65536.0.0","1.0.0/..","1.0.0?x","1.0","1.0.0.0"};
    for(unsigned i=0;i<_countof(versions);i++){reset();refused(versions[i],"l4tools-release.json");assert(!opened);}
    const DWORD statuses[]={301,302,401,403,404,500};for(unsigned i=0;i<_countof(statuses);i++){reset();response_status=statuses[i];refused(NULL,"catalog.json");assert(!reads);}
    const wchar_t* lengths[]={L"",L"0",L"-1",L"+3",L"3x",L"65536",L"99999999999"};
    for(unsigned i=0;i<_countof(lengths);i++){reset();declared=lengths[i];refused(NULL,"catalog.json");assert(!reads);}
    reset();declared=L"4";refused(NULL,"catalog.json");reset();declared=L"2";refused(NULL,"catalog.json");
    reset();body_size=16385;declared=L"20000";refused(NULL,"catalog.json");
    reset();failed_header=TRUE;refused(NULL,"catalog.json");reset();failed_option=TRUE;refused(NULL,"catalog.json");assert(opened==1);
    reset();failed_send=TRUE;refused(NULL,"catalog.json");assert(GetLastError()==ERROR_WINHTTP_SECURE_FAILURE && opened==3 && !reads);
    reset();late=TRUE;refused(NULL,"catalog.json");
    reset();body_size=383;declared=L"383";expected_path=L"/l4tools/metadata/catalog.json.sig";refused(NULL,"catalog.json.sig");assert(GetLastError()==ERROR_INVALID_DATA);
    reset();bytes=(BYTE*)1;assert(!l4_registry_metadata(18443,NULL,"catalog.json",3000,&bytes,NULL) && !bytes && !opened);
    reset();ULONGLONG received=99;DWORD delivered=0;failed_sink=TRUE;assert(!l4_registry_fetch(18443,NULL,"catalog.json",65535,3000,sink,&delivered,&received));assert(!received && delivered==3 && opened==closed);
    reset();expected_path=L"/l4tools/1.13.3/l4tools-layout-x64.zip";assert(l4_registry_fetch(18443,"1.13.3","l4tools-layout-x64.zip",L4_REGISTRY_MAX_ARCHIVE,3000,sink,&delivered,&received));assert(received==3);
    reset();assert(!l4_registry_fetch(0,NULL,"catalog.json",65535,3000,sink,&delivered,&received) && !received && !opened);
    assert(!l4_registry_fetch(18443,NULL,"catalog.json",65536,3000,sink,&delivered,&received) && !opened);
    assert(!l4_registry_fetch(18443,NULL,"catalog.json",65535,600001,sink,&delivered,&received) && !opened);
    puts("Registry WinHTTP contract PASS: loopback-only named proxy, TLS1.2, GET/fixed paths, no redirects/auth, bounds/errors/deadline/cleanup");return 0;
}
