#include "registry_http.h"
#include "metadata.h"
#include "layout.h"
#include <winhttp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#pragma comment(lib,"winhttp.lib")
static bool fail(DWORD code){SetLastError(code);return false;}
static bool path_for(const char* version,const char* file,wchar_t path[160],ULONGLONG* bound){
    if(!file)return false;
    if(!version){if(strcmp(file,"catalog.json") && strcmp(file,"catalog.json.sig"))return false;
        *bound=strstr(file,".sig")?L4_METADATA_SIGNATURE_BYTES:L4_METADATA_MAX_BYTES;
        return swprintf_s(path,160,L"/l4tools/metadata/%hs",file)>0;}
    wchar_t wide[64];L4Layout layout;
    const char* digit=version;
    for(unsigned part=0;part<3;part++){unsigned value=0;if(*digit<'0' || *digit>'9')return false;
        while(*digit>='0' && *digit<='9'){value=value*10+(unsigned)(*digit++-'0');if(value>65535)return false;}
        if(part<2 && *digit++!='.')return false;}
    if(*digit)return false;
    if(strlen(version)>=64 || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,version,-1,wide,64) ||
       !l4_layout_from_roots(&layout,L"C:\\L4Registry\\Programs",L"C:\\L4Registry\\Data",wide))return false;
    const char* names[]={"l4tools-release.json","l4tools-release.json.sig","l4tools-layout-x86.json","l4tools-layout-x86.json.sig",
        "l4tools-layout-x64.json","l4tools-layout-x64.json.sig","l4tools-layout-x86.zip","l4tools-layout-x64.zip"};
    bool known=false;for(unsigned i=0;i<_countof(names);i++)known=known || !strcmp(file,names[i]);if(!known)return false;
    *bound=strstr(file,".sig")?L4_METADATA_SIGNATURE_BYTES:strstr(file,".zip")?L4_REGISTRY_MAX_ARCHIVE:L4_METADATA_MAX_BYTES;
    return swprintf_s(path,160,L"/l4tools/%ls/%hs",wide,file)>0;
}
static bool remaining(HINTERNET session,ULONGLONG deadline){
    ULONGLONG now=GetTickCount64();if(now>=deadline)return fail(ERROR_TIMEOUT);
    int slice=(int)(deadline-now);if(slice>10000)slice=10000;
    return WinHttpSetTimeouts(session,slice,slice,slice,slice)!=FALSE;
}
bool l4_registry_fetch(WORD port,const char* version,const char* file,ULONGLONG limit,DWORD timeout,
    L4RegistrySink sink,void* context,ULONGLONG* received){
    if(!received)return fail(ERROR_INVALID_PARAMETER);*received=0;wchar_t path[160],proxy[40];ULONGLONG bound=0;
    if(!port || !sink || timeout<100 || timeout>600000 || !path_for(version,file,path,&bound) || !limit || limit>bound)return fail(ERROR_INVALID_PARAMETER);
    swprintf_s(proxy,_countof(proxy),L"127.0.0.1:%u",(unsigned)port);ULONGLONG deadline=GetTickCount64()+timeout;
    HINTERNET session=WinHttpOpen(L"L4Tools-Update/1",WINHTTP_ACCESS_TYPE_NAMED_PROXY,proxy,WINHTTP_NO_PROXY_BYPASS,0),connection=NULL,request=NULL;
    DWORD code=ERROR_INVALID_DATA;bool ok=session && remaining(session,deadline);
    DWORD protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    if(ok)ok=WinHttpSetOption(session,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof(protocols))!=FALSE;
    if(ok){connection=WinHttpConnect(session,L4_REGISTRY_HOST_W,443,0);ok=connection!=NULL;}
    if(ok){request=WinHttpOpenRequest(connection,L"GET",path,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);ok=request!=NULL;}
    DWORD disabled=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION,autologon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if(ok)ok=WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)) &&
        WinHttpSetOption(request,WINHTTP_OPTION_AUTOLOGON_POLICY,&autologon,sizeof(autologon));
    if(ok)ok=remaining(request,deadline) && WinHttpSendRequest(request,L"Accept-Encoding: identity\r\n",(DWORD)-1,
        WINHTTP_NO_REQUEST_DATA,0,0,0) && remaining(request,deadline) && WinHttpReceiveResponse(request,NULL);
    DWORD status=0,size=sizeof(status);if(ok)ok=WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX) && status==200;
    wchar_t length[32];size=sizeof(length);ULONGLONG expected=0;
    if(ok)ok=WinHttpQueryHeaders(request,WINHTTP_QUERY_CONTENT_LENGTH,WINHTTP_HEADER_NAME_BY_INDEX,length,&size,WINHTTP_NO_HEADER_INDEX)!=FALSE;
    if(ok){size_t count=wcsnlen_s(length,_countof(length));ok=count>0 && count<_countof(length) && count<=10;
        for(size_t i=0;ok && i<count;i++){if(length[i]<L'0' || length[i]>L'9')ok=false;else expected=expected*10+(ULONGLONG)(length[i]-L'0');}
        ok=ok && expected>0 && expected<=limit;}
    BYTE buffer[16384];ULONGLONG total=0;
    while(ok){DWORD read=0;ok=remaining(request,deadline) && WinHttpReadData(request,buffer,sizeof(buffer),&read);
        if(!ok || !read)break;if(read>sizeof(buffer) || total+read>expected){ok=false;break;}
        ok=sink(buffer,read,context);if(ok)total+=read;}
    ok=ok && total==expected && GetTickCount64()<deadline;
    if(!ok){DWORD error=GetLastError();if(error)code=error;}
    if(request)WinHttpCloseHandle(request);if(connection)WinHttpCloseHandle(connection);if(session)WinHttpCloseHandle(session);
    if(!ok)return fail(code);*received=total;return true;
}
typedef struct{BYTE* bytes;DWORD size;} Buffer;
static bool collect(const BYTE* bytes,DWORD size,void* context){Buffer* b=(Buffer*)context;
    if(size>L4_METADATA_MAX_BYTES-b->size)return false;memcpy(b->bytes+b->size,bytes,size);b->size+=size;return true;}
bool l4_registry_metadata(WORD port,const char* version,const char* file,DWORD timeout,BYTE** bytes,DWORD* size){
    if(bytes)*bytes=NULL;if(size)*size=0;
    if(!bytes || !size)return fail(ERROR_INVALID_PARAMETER);wchar_t path[160];ULONGLONG bound=0;
    if(!path_for(version,file,path,&bound) || bound>L4_METADATA_MAX_BYTES)return fail(ERROR_INVALID_PARAMETER);
    Buffer b={(BYTE*)malloc(L4_METADATA_MAX_BYTES),0};if(!b.bytes)return fail(ERROR_NOT_ENOUGH_MEMORY);ULONGLONG received=0;
    bool ok=l4_registry_fetch(port,version,file,bound,timeout,collect,&b,&received);DWORD error=GetLastError();
    if(ok && strstr(file,".sig") && received!=L4_METADATA_SIGNATURE_BYTES){ok=false;error=ERROR_INVALID_DATA;}
    if(!ok){free(b.bytes);return fail(error?error:ERROR_INVALID_DATA);}*bytes=b.bytes;*size=b.size;return true;
}
