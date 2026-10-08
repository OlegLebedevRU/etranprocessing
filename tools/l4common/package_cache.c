#include "package_cache.h"
#include "journal_internal.h"
#include <bcrypt.h>
#include <sddl.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct L4CachedPackage{L4FileFence parents;HANDLE file;wchar_t path[MAX_PATH],leaf[44];};
static bool fail(DWORD error){SetLastError(error);return false;}
void l4_package_close(L4CachedPackage* p){if(!p)return;if(p->file!=INVALID_HANDLE_VALUE)CloseHandle(p->file);l4_store_unpin(&p->parents);free(p);}
const wchar_t* l4_package_path(const L4CachedPackage* p){return p?p->path:NULL;}
const wchar_t* l4_package_leaf(const L4CachedPackage* p){return p?p->leaf:NULL;}
static bool canonical_leaf(const wchar_t* leaf){
    if(!leaf || wcslen(leaf)!=42 || wcscmp(leaf+38,L".zip"))return false;
    wchar_t text[39],check[39];memcpy(text,leaf,38*sizeof(wchar_t));text[38]=0;GUID id;
    return SUCCEEDED(CLSIDFromString(text,&id)) && StringFromGUID2(&id,check,39) && !wcscmp(text,check);
}
static bool allocate(const L4Layout* layout,const wchar_t* leaf,ULONGLONG size,const BYTE* sha,L4CachedPackage** out){
    *out=NULL;if(!layout || !canonical_leaf(leaf) || !sha || size<22 || size>L4_REGISTRY_MAX_ARCHIVE)return fail(ERROR_INVALID_PARAMETER);
    const wchar_t* version=wcsrchr(layout->release,L'\\');L4Layout checked;
    if(!version || !l4_layout_from_roots(&checked,layout->binaries,layout->data,version+1) || memcmp(layout,&checked,sizeof(checked)))return fail(ERROR_INVALID_PARAMETER);
    L4CachedPackage* p=(L4CachedPackage*)calloc(1,sizeof(*p));if(!p)return fail(ERROR_NOT_ENOUGH_MEMORY);p->file=INVALID_HANDLE_VALUE;
    /* Common cache parent is read-only to Users; package itself is private. All
     * parents are held without DELETE sharing through preparation and use. */
    bool ok=l4_store_pin(layout->cache,layout->data,false,&p->parents);
    if(ok && wcslen(layout->cache)+wcslen(leaf)+2>MAX_PATH)ok=fail(ERROR_FILENAME_EXCED_RANGE);
    if(ok){wcscpy_s(p->leaf,_countof(p->leaf),leaf);swprintf_s(p->path,MAX_PATH,L"%ls\\%ls",layout->cache,leaf);}
    DWORD error=GetLastError();if(!ok){l4_package_close(p);return fail(error);}*out=p;return true;
}
static bool file_security(L4CachedPackage* p){
    BY_HANDLE_FILE_INFORMATION info;BYTE* sd=NULL;DWORD sd_size;
    bool ok=GetFileInformationByHandle(p->file,&info)!=0 && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks==1;
    if(ok)ok=l4_store_security(p->file,true,&sd,&sd_size);free(sd);
    WIN32_FIND_STREAM_DATA data;HANDLE streams=INVALID_HANDLE_VALUE;
    if(ok){streams=FindFirstStreamW(p->path,FindStreamInfoStandard,&data,0);ok=streams!=INVALID_HANDLE_VALUE;
        if(ok){do{if(wcscmp(data.cStreamName,L"::$DATA")){ok=false;break;}}while(FindNextStreamW(streams,&data));if(ok)ok=GetLastError()==ERROR_HANDLE_EOF;FindClose(streams);}}
    return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool verify(L4CachedPackage* p,ULONGLONG expected,const BYTE sha[32]){
    LARGE_INTEGER size,zero={0};if(!file_security(p) || !GetFileSizeEx(p->file,&size) || size.QuadPart<0 || (ULONGLONG)size.QuadPart!=expected)return fail(ERROR_INVALID_DATA);
    if(!SetFilePointerEx(p->file,zero,NULL,FILE_BEGIN))return false;
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;BYTE buffer[16384],actual[32];ULONGLONG total=0;
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)==0;
    if(ok)ok=BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)==0;
    while(ok && total<expected){DWORD got=0,want=(DWORD)((expected-total)>sizeof(buffer)?sizeof(buffer):expected-total);
        ok=ReadFile(p->file,buffer,want,&got,NULL)!=0 && got==want;if(ok)ok=BCryptHashData(hash,buffer,got,0)==0;total+=got;}
    if(ok)ok=BCryptFinishHash(hash,actual,32,0)==0 && !memcmp(actual,sha,32);
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok?true:fail(ERROR_CRC);
}
bool l4_package_reopen(const L4Layout* layout,const wchar_t* leaf,ULONGLONG size,const BYTE sha[32],L4CachedPackage** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;L4CachedPackage* p=NULL;if(!allocate(layout,leaf,size,sha,&p))return false;
    p->file=CreateFileW(p->path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    bool ok=p->file!=INVALID_HANDLE_VALUE && verify(p,size,sha);DWORD error=GetLastError();if(!ok){l4_package_close(p);return fail(error);}*result=p;return true;
}
typedef struct{HANDLE file;ULONGLONG total,limit;} Writer;
static bool write_chunk(const BYTE* bytes,DWORD size,void* context){Writer* w=(Writer*)context;
    if(size>w->limit-w->total)return fail(ERROR_FILE_TOO_LARGE);if(!l4_store_write(w->file,bytes,size))return false;w->total+=size;return true;
}
bool l4_package_download(const L4Layout* layout,WORD port,const char* version,const char* name,ULONGLONG size,const BYTE sha[32],DWORD timeout,L4CachedPackage** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;
    if(!version || !name || (strcmp(name,"l4tools-layout-x86.zip") && strcmp(name,"l4tools-layout-x64.zip")) || !port || timeout<100 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);
    GUID id;wchar_t leaf[44];if(FAILED(CoCreateGuid(&id)) || !StringFromGUID2(&id,leaf,39))return fail(ERROR_GEN_FAILURE);wcscat_s(leaf,_countof(leaf),L".zip");
    L4CachedPackage* p=NULL;if(!allocate(layout,leaf,size,sha,&p))return false;
    HANDLE previous=NULL;PSECURITY_DESCRIPTOR sd=NULL;bool owner=l4_layout_owner_begin(&previous);bool ok=owner;
    if(ok)ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL)!=0;
    SECURITY_ATTRIBUTES attrs={sizeof(attrs),sd,FALSE};
    if(ok){p->file=CreateFileW(p->path,GENERIC_READ|GENERIC_WRITE|READ_CONTROL|DELETE,FILE_SHARE_READ,&attrs,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,NULL);ok=p->file!=INVALID_HANDLE_VALUE;}
    DWORD error=ok?0:GetLastError();LocalFree(sd);if(owner && !l4_layout_owner_end(previous)){ok=false;error=GetLastError();}
    Writer w={p->file,0,size};ULONGLONG received=0;
    if(ok)ok=l4_registry_fetch(port,version,name,size,timeout,write_chunk,&w,&received);
    if(ok && (received!=size || w.total!=size))ok=fail(ERROR_INVALID_DATA);
    if(ok)ok=FlushFileBuffers(p->file)!=0 && verify(p,size,sha);
    if(!ok){if(!error)error=GetLastError();
        /* Delete only the CREATE_NEW object through its held handle. Crashes may
         * leave an unreferenced file; no scan/adoption of partial cache entries. */
        if(p->file!=INVALID_HANDLE_VALUE){FILE_DISPOSITION_INFO remove={TRUE};if(!SetFileInformationByHandle(p->file,FileDispositionInfo,&remove,sizeof(remove)))error=GetLastError();}
        l4_package_close(p);return fail(error?error:ERROR_INVALID_DATA);}
    /* Drop write access and reopen/read-hash under the still held parent fence.
     * A privileged race also fails the repeated content/security check. */
    CloseHandle(p->file);p->file=INVALID_HANDLE_VALUE;
    p->file=CreateFileW(p->path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    ok=p->file!=INVALID_HANDLE_VALUE && verify(p,size,sha);error=GetLastError();if(!ok){l4_package_close(p);return fail(error);}*result=p;return true;
}

bool l4_package_import(const L4Layout* layout,HANDLE source,ULONGLONG size,const BYTE sha[32],L4CachedPackage** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;if(!source || source==INVALID_HANDLE_VALUE)return fail(ERROR_INVALID_HANDLE);
    GUID id;wchar_t leaf[44];if(FAILED(CoCreateGuid(&id)) || !StringFromGUID2(&id,leaf,39))return fail(ERROR_GEN_FAILURE);wcscat_s(leaf,_countof(leaf),L".zip");
    L4CachedPackage* p=NULL;if(!allocate(layout,leaf,size,sha,&p))return false;
    HANDLE previous=NULL;PSECURITY_DESCRIPTOR sd=NULL;bool owner=l4_layout_owner_begin(&previous);bool ok=owner;
    if(ok)ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL)!=0;
    SECURITY_ATTRIBUTES attrs={sizeof(attrs),sd,FALSE};
    if(ok){p->file=CreateFileW(p->path,GENERIC_READ|GENERIC_WRITE|READ_CONTROL|DELETE,FILE_SHARE_READ,&attrs,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,NULL);ok=p->file!=INVALID_HANDLE_VALUE;}
    DWORD error=ok?0:GetLastError();LocalFree(sd);if(owner && !l4_layout_owner_end(previous)){ok=false;error=GetLastError();}
    LARGE_INTEGER zero={0},actual={0};BYTE buffer[32768];ULONGLONG total=0;
    if(ok)ok=GetFileSizeEx(source,&actual) && actual.QuadPart>=0 && (ULONGLONG)actual.QuadPart==size && SetFilePointerEx(source,zero,NULL,FILE_BEGIN);
    while(ok && total<size){DWORD got=0,want=(DWORD)((size-total)>sizeof(buffer)?sizeof(buffer):size-total);
        ok=ReadFile(source,buffer,want,&got,NULL) && got==want && l4_store_write(p->file,buffer,got);if(ok)total+=got;}
    if(ok)ok=total==size && FlushFileBuffers(p->file) && verify(p,size,sha);if(!ok && !error)error=GetLastError();
    if(!ok){if(p->file!=INVALID_HANDLE_VALUE){FILE_DISPOSITION_INFO disposition={TRUE};SetFileInformationByHandle(p->file,FileDispositionInfo,&disposition,sizeof(disposition));}
        l4_package_close(p);return fail(error?error:ERROR_INVALID_DATA);}
    CloseHandle(p->file);p->file=CreateFileW(p->path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    ok=p->file!=INVALID_HANDLE_VALUE && verify(p,size,sha);error=GetLastError();if(!ok){l4_package_close(p);return fail(error);}*result=p;return true;
}
