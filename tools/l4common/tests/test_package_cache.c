/* Modeled Registry stream, actual protected filesystem/hash/handles. */
#include "../package_cache.h"
#include <sddl.h>
#include <aclapi.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures,requests;static unsigned mode;static BYTE body[64];
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL cache line %u: %s error=%lu\n",__LINE__,#x,GetLastError());}}while(0)
static bool fetch(WORD port,const char* version,const char* name,ULONGLONG limit,DWORD timeout,L4RegistrySink sink,void* ctx,ULONGLONG* received){
    CHECK(port==18443 && !strcmp(version,"1.13.3") && !strcmp(name,"l4tools-layout-x86.zip") && limit==sizeof(body) && timeout==3000);++requests;*received=0;
    if(mode==1){if(!sink(body,13,ctx))return false;SetLastError(ERROR_TIMEOUT);return false;}
    if(mode==2){if(!sink(body,63,ctx))return false;*received=63;return true;}
    if(mode==3)return sink(body,65,ctx);
    if(!sink(body,17,ctx) || !sink(body+17,47,ctx))return false;*received=64;return true;
}
#define l4_registry_fetch fetch
#include "../package_cache.c"
static unsigned entries(const wchar_t* path){wchar_t pattern[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",path);WIN32_FIND_DATAW data;HANDLE h=FindFirstFileW(pattern,&data);unsigned count=0;
    if(h!=INVALID_HANDLE_VALUE){do{if(wcscmp(data.cFileName,L".") && wcscmp(data.cFileName,L".."))++count;}while(FindNextFileW(h,&data));FindClose(h);}return count;
}
static void cleanup(const wchar_t* directory){wchar_t pattern[MAX_PATH],path[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",directory);WIN32_FIND_DATAW data;HANDLE h=FindFirstFileW(pattern,&data);
    if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L".."))continue;swprintf_s(path,MAX_PATH,L"%ls\\%ls",directory,data.cFileName);
        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))cleanup(path);else RemoveDirectoryW(path);}else DeleteFileW(path);
    }while(FindNextFileW(h,&data));FindClose(h);}RemoveDirectoryW(directory);
}
int main(void){
    wchar_t temp[MAX_PATH],root[MAX_PATH],programs[MAX_PATH],data[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));
    swprintf_s(root,MAX_PATH,L"%lsl4cache-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(root,NULL));
    swprintf_s(programs,MAX_PATH,L"%ls\\Programs",root);swprintf_s(data,MAX_PATH,L"%ls\\Data",root);
    L4Layout layout;CHECK(l4_layout_from_roots(&layout,programs,data,L"1.13.3"));CHECK(l4_layout_prepare(&layout));
    for(unsigned i=0;i<sizeof(body);i++)body[i]=(BYTE)i;BYTE sha[32];CHECK(l4_store_hash(body,sizeof(body),NULL,0,sha));
    L4CachedPackage* p=NULL,*again=NULL;CHECK(l4_package_download(&layout,18443,"1.13.3","l4tools-layout-x86.zip",64,sha,3000,&p));
    CHECK(p && entries(layout.cache)==1);if(!p){cleanup(root);return 1;}
    wchar_t leaf[44],path[MAX_PATH];wcscpy_s(leaf,44,l4_package_leaf(p));wcscpy_s(path,MAX_PATH,l4_package_path(p));CHECK(canonical_leaf(leaf));
    CHECK(l4_package_reopen(&layout,leaf,64,sha,&again));l4_package_close(again);again=NULL;
    HANDLE h=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(h==INVALID_HANDLE_VALUE);if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
    CHECK(!DeleteFileW(path));
    L4CachedPackage* imported=NULL;unsigned prior_requests=requests;BYTE invalid_digest[32]={0};
    CHECK(l4_package_import(&layout,p->file,64,sha,&imported));CHECK(imported && entries(layout.cache)==2 && requests==prior_requests);
    wchar_t imported_path[MAX_PATH];wcscpy_s(imported_path,MAX_PATH,l4_package_path(imported));
    CHECK(!DeleteFileW(imported_path));l4_package_close(imported);imported=NULL;CHECK(DeleteFileW(imported_path));
    CHECK(!l4_package_import(&layout,p->file,64,invalid_digest,&imported));CHECK(!imported && entries(layout.cache)==1 && requests==prior_requests);
    CHECK(!l4_package_import(&layout,p->file,63,sha,&imported));CHECK(!imported && entries(layout.cache)==1);
    CHECK(!l4_package_import(&layout,INVALID_HANDLE_VALUE,64,sha,&imported));CHECK(!imported);
    l4_package_close(p);p=NULL;
    BYTE wrong[32]={0};CHECK(!l4_package_reopen(&layout,leaf,64,wrong,&p));CHECK(!p);
    CHECK(!l4_package_reopen(&layout,leaf,63,sha,&p));CHECK(!p);
    CHECK(!l4_package_reopen(&layout,L"..\\file.zip",64,sha,&p));CHECK(!p);
    CHECK(!l4_package_reopen(&layout,L"{00000000-0000-0000-0000-000000000000}.zip",64,sha,&p));CHECK(!p);
    for(mode=1;mode<=3;mode++){CHECK(!l4_package_download(&layout,18443,"1.13.3","l4tools-layout-x86.zip",64,sha,3000,&p));CHECK(!p && GetLastError()!=0);CHECK(entries(layout.cache)==1);}mode=0;
    CHECK(!l4_package_download(&layout,18443,"1.13.3","l4tools-layout-x86.zip",64,wrong,3000,&p));CHECK(!p && entries(layout.cache)==1);
    wchar_t stream[MAX_PATH];swprintf_s(stream,MAX_PATH,L"%ls:extra",path);h=CreateFileW(stream,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);CHECK(h!=INVALID_HANDLE_VALUE);if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
    CHECK(!l4_package_reopen(&layout,leaf,64,sha,&p));CHECK(!p);CHECK(DeleteFileW(stream));
    wchar_t link[MAX_PATH];swprintf_s(link,MAX_PATH,L"%ls\\hardlink",layout.cache);CHECK(CreateHardLinkW(link,path,NULL));CHECK(!l4_package_reopen(&layout,leaf,64,sha,&p));CHECK(!p);CHECK(DeleteFileW(link));
    CHECK(l4_package_reopen(&layout,leaf,64,sha,&p));l4_package_close(p);p=NULL;
    CHECK(SetNamedSecurityInfoW(path,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|UNPROTECTED_DACL_SECURITY_INFORMATION,NULL,NULL,NULL,NULL)==ERROR_SUCCESS);
    CHECK(!l4_package_reopen(&layout,leaf,64,sha,&p));CHECK(!p);
    unsigned before=requests;CHECK(!l4_package_download(&layout,18443,"1.13.3","other.zip",64,sha,3000,&p));CHECK(!p && requests==before);
    CHECK(!l4_package_download(&layout,18443,"1.13.3","l4tools-layout-x86.zip",L4_REGISTRY_MAX_ARCHIVE+1,sha,3000,&p));CHECK(!p && requests==before);
    L4Layout altered=layout;wcscpy_s(altered.cache,MAX_PATH,root);CHECK(!l4_package_download(&altered,18443,"1.13.3","l4tools-layout-x86.zip",64,sha,3000,&p));CHECK(!p && requests==before);
    PSECURITY_DESCRIPTOR sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;BU)",SDDL_REVISION_1,&sd,NULL));
    PACL acl=NULL;BOOL present,defaulted;CHECK(GetSecurityDescriptorDacl(sd,&present,&acl,&defaulted));
    CHECK(SetNamedSecurityInfoW(layout.cache,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,NULL,NULL,acl,NULL)==ERROR_SUCCESS);LocalFree(sd);
    CHECK(!l4_package_download(&layout,18443,"1.13.3","l4tools-layout-x86.zip",64,sha,3000,&p));CHECK(!p && requests==before);
    cleanup(root);CHECK(GetFileAttributesW(root)==INVALID_FILE_ATTRIBUTES);
    printf("Protected package cache: %u checks, %u failures; actual file IO, modeled stream, no services\n",checks,failures);return failures?1:0;
}
