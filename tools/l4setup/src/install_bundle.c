#include "install_bundle.h"
#include "bootstrap_receipt.h"
#include "recovery_receipt.h"
#include "../../l4common/journal_internal.h"
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BUNDLE_FILES 10u
struct SetupInstallBundle {SetupRootManifest* root;L4Catalog* catalog;HANDLE directories[MAX_PATH/2],files[BUNDLE_FILES];unsigned dirs;
    wchar_t directory[MAX_PATH],names[BUNDLE_FILES][48],paths[BUNDLE_FILES][MAX_PATH];BYTE* documents[BUNDLE_FILES];DWORD sizes[BUNDLE_FILES];const char* arch;SetupAdmissionPolicy policy;SetupBootstrapReceipt bootstrap;bool bootstrap_ready;};
static bool fail(DWORD code){SetLastError(code);return false;}
static bool held_hash(HANDLE file,ULONGLONG expected,const BYTE digest[32]){
    BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER size,zero={0};if(!GetFileInformationByHandle(file,&info) || info.nNumberOfLinks!=1 || (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !GetFileSizeEx(file,&size) || size.QuadPart<0 || (ULONGLONG)size.QuadPart!=expected || !SetFilePointerEx(file,zero,NULL,FILE_BEGIN))return fail(ERROR_INVALID_DATA);
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;BYTE buffer[32768],actual[32];DWORD read=0;ULONGLONG total=0;
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)==0 && BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)==0;
    while(ok){ok=ReadFile(file,buffer,sizeof(buffer),&read,NULL)!=0;if(!ok || !read)break;total+=read;ok=total<=expected && BCryptHashData(hash,buffer,read,0)==0;}
    if(ok)ok=total==expected && BCryptFinishHash(hash,actual,sizeof(actual),0)==0 && !memcmp(actual,digest,32);
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok?true:fail(ERROR_CRC);
}
static bool open_file(SetupInstallBundle* b,unsigned index){
    if(swprintf_s(b->paths[index],MAX_PATH,L"%ls\\%ls",b->directory,b->names[index])<0)return fail(ERROR_FILENAME_EXCED_RANGE);
    b->files[index]=CreateFileW(b->paths[index],GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(b->files[index]==INVALID_HANDLE_VALUE)return false;BY_HANDLE_FILE_INFORMATION info;
    return GetFileInformationByHandle(b->files[index],&info) && info.nNumberOfLinks==1 && !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY));
}
static bool document(SetupInstallBundle* b,unsigned index,DWORD maximum){
    LARGE_INTEGER size;if(!open_file(b,index) || !GetFileSizeEx(b->files[index],&size) || size.QuadPart<=0 || size.QuadPart>maximum)return fail(ERROR_INVALID_DATA);
    b->sizes[index]=(DWORD)size.QuadPart;b->documents[index]=malloc(b->sizes[index]);DWORD read=0;
    return b->documents[index] && ReadFile(b->files[index],b->documents[index],b->sizes[index],&read,NULL) && read==b->sizes[index];
}
void setup_bundle_free(SetupInstallBundle* b){if(!b)return;for(unsigned i=0;i<BUNDLE_FILES;i++)if(b->files[i] && b->files[i]!=INVALID_HANDLE_VALUE)CloseHandle(b->files[i]);
    while(b->dirs)CloseHandle(b->directories[--b->dirs]);for(unsigned i=0;i<BUNDLE_FILES;i++)free(b->documents[i]);setup_root_free(b->root);l4_catalog_free(b->catalog);free(b);}
const SetupRootManifest* setup_bundle_root(const SetupInstallBundle* b){return b?b->root:NULL;}
const void* setup_bundle_descriptor(const SetupInstallBundle* b,DWORD* size){if(!b || !size)return NULL;*size=b->sizes[4];return b->documents[4];}
const BYTE* setup_bundle_signature(const SetupInstallBundle* b,DWORD* size){if(!b || !size)return NULL;*size=b->sizes[5];return b->documents[5];}
const wchar_t* setup_bundle_archive(const SetupInstallBundle* b){return b?b->paths[6]:NULL;}
bool setup_bundle_manifest(const SetupInstallBundle* b,const L4Layout* layout,SetupManifest** manifest){return b && setup_root_descriptor_trusted(b->root,b->documents[4],b->sizes[4],b->documents[5],b->sizes[5],layout,manifest);}
static bool bundle_open(const wchar_t* directory,const char* version,const char* arch,const L4CatalogRelease* original,SetupInstallBundle** output,SetupAdmissionPolicy policy){
    if(!output)return fail(ERROR_INVALID_PARAMETER);*output=NULL;wchar_t canonical[MAX_PATH];
    if(!directory || !version || !arch || (strcmp(arch,"x86") && strcmp(arch,"x64")) || wcslen(directory)<4 || wcslen(directory)>=MAX_PATH || directory[1]!=L':' || directory[2]!=L'\\' ||
        (!GetFullPathNameW(directory,MAX_PATH,canonical,NULL) || GetFullPathNameW(directory,MAX_PATH,canonical,NULL)>=MAX_PATH) || wcscmp(directory,canonical) || wcschr(directory+2,L':'))return fail(ERROR_INVALID_NAME);
    SetupInstallBundle* b=calloc(1,sizeof(*b));if(!b)return fail(ERROR_NOT_ENOUGH_MEMORY);wcscpy_s(b->directory,MAX_PATH,directory);b->arch=!strcmp(arch,"x86")?"x86":"x64";b->policy=policy;
    bool ok=true;wchar_t ancestor[MAX_PATH];wcscpy_s(ancestor,MAX_PATH,canonical);
    for(wchar_t* p=ancestor+3;ok;){wchar_t* end=wcschr(p,L'\\');wchar_t saved=end?*end:0;if(end)*end=0;
        HANDLE h=CreateFileW(ancestor,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        BY_HANDLE_FILE_INFORMATION info;ok=h!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(h,&info) && (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
        if(ok)b->directories[b->dirs++]=h;else if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);if(end)*end=saved;if(!end)break;p=end+1;
    }
    wcscpy_s(b->names[0],48,L"setup-catalog.json");wcscpy_s(b->names[1],48,L"setup-catalog.json.sig");wcscpy_s(b->names[2],48,L"l4tools-release.json");wcscpy_s(b->names[3],48,L"l4tools-release.json.sig");
    swprintf_s(b->names[4],48,L"l4tools-layout-%hs.json",arch);swprintf_s(b->names[5],48,L"l4tools-layout-%hs.json.sig",arch);swprintf_s(b->names[6],48,L"l4tools-layout-%hs.zip",arch);
    for(unsigned i=0;ok && i<6;i++)ok=document(b,i,i%2?384:65535);
    L4CatalogRelease selected={0};FILETIME ft;GetSystemTimeAsFileTime(&ft);ULONGLONG now=((((ULONGLONG)ft.dwHighDateTime<<32)|ft.dwLowDateTime)-116444736000000000ULL)/10000000ULL;
    if(ok){if(original){selected=*original;ok=!strcmp(selected.version,version) && !selected.revoked;}else ok=l4_catalog_parse_trusted(b->documents[0],b->sizes[0],b->documents[1],b->sizes[1],now,&b->catalog) && l4_catalog_resolve(b->catalog,version,&selected);}
    if(ok)ok=setup_root_parse_trusted(b->documents[2],b->sizes[2],b->documents[3],b->sizes[3],&selected,arch,&b->root);
    for(unsigned i=0;ok && i<2;i++){const SetupRootAsset* a=setup_root_asset(b->root,i);ok=a && held_hash(b->files[4+i],a->size,a->sha256);}
    if(ok){const SetupRootAsset* a=setup_root_asset(b->root,2);ok=a && open_file(b,6) && held_hash(b->files[6],a->size,a->sha256);}
    if(!ok){DWORD code=GetLastError();setup_bundle_free(b);return fail(code?code:ERROR_INVALID_DATA);}*output=b;return true;
}
bool setup_bundle_self(const SetupInstallBundle* b,const wchar_t* self){
    const SetupRootAsset* asset=b?setup_root_installer(b->root):NULL;if(!asset || !self)return fail(ERROR_INVALID_PARAMETER);
    HANDLE file=CreateFileW(self,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);if(file==INVALID_HANDLE_VALUE)return false;
    bool ok=held_hash(file,asset->size,asset->sha256) && setup_signed_executable_policy(file,self,setup_root_publisher(b->root),b->policy);DWORD code=GetLastError();CloseHandle(file);return ok?true:fail(code);
}
static bool copy_held(HANDLE source,const wchar_t* target,ULONGLONG size,const BYTE digest[32]){
    HANDLE dest=CreateFileW(target,GENERIC_READ|GENERIC_WRITE|READ_CONTROL,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(dest==INVALID_HANDLE_VALUE){if(GetLastError()!=ERROR_FILE_EXISTS)return false;dest=CreateFileW(target,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        if(dest==INVALID_HANDLE_VALUE)return false;bool ok=held_hash(dest,size,digest);DWORD code=GetLastError();CloseHandle(dest);return ok?true:fail(code);}
    LARGE_INTEGER zero={0};BYTE buffer[32768];DWORD read=0,written=0;ULONGLONG total=0;bool ok=SetFilePointerEx(source,zero,NULL,FILE_BEGIN)!=0;
    while(ok){ok=ReadFile(source,buffer,sizeof(buffer),&read,NULL)!=0;if(!ok || !read)break;total+=read;ok=total<=size && WriteFile(dest,buffer,read,&written,NULL) && written==read;}
    if(ok)ok=total==size && FlushFileBuffers(dest) && held_hash(dest,size,digest);DWORD code=GetLastError();CloseHandle(dest);return ok?true:fail(code?code:ERROR_CRC);
}
bool setup_bundle_stage(const SetupInstallBundle* b,L4Journal* j,wchar_t host[MAX_PATH]){
    if(!b || !j || j->sequence || j->poisoned || !host)return fail(ERROR_INVALID_PARAMETER);const wchar_t* operation=wcsrchr(j->directory,L'\\');wchar_t relative[MAX_PATH],inputs[MAX_PATH];
    if(!operation || swprintf_s(relative,MAX_PATH,L"update\\operations\\%ls\\inputs",operation+1)<0 || !l4_layout_prepare_leaf(&j->layout,relative,NULL,NULL,true) ||
        !l4_layout_data_path(&j->layout,relative,inputs) || !l4_layout_prepare_installer(&j->layout,host))return false;
    for(unsigned i=0;i<(b->bootstrap_ready?BUNDLE_FILES:7u);i++){wchar_t target[MAX_PATH];ULONGLONG size=0;BYTE digest[32];
        if(swprintf_s(target,MAX_PATH,L"%ls\\%ls",inputs,b->names[i])<0)return fail(ERROR_FILENAME_EXCED_RANGE);
        if(i<6 || i==7 || i==8){size=b->sizes[i];if(!l4_store_hash(b->documents[i],b->sizes[i],NULL,0,digest))return false;}else if(i==9){size=b->bootstrap.helper.size;memcpy(digest,b->bootstrap.helper.sha256,32);}else{const SetupRootAsset* a=setup_root_asset(b->root,2);size=a->size;memcpy(digest,a->sha256,32);}
        if(!copy_held(b->files[i],target,size,digest))return false;
    }
    wchar_t self[MAX_PATH];if(!GetModuleFileNameW(NULL,self,MAX_PATH) || !setup_bundle_self(b,self))return false;
    HANDLE input=CreateFileW(self,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);if(input==INVALID_HANDLE_VALUE)return false;
    const SetupRootAsset* a=setup_root_installer(b->root);bool ok=copy_held(input,host,a->size,a->sha256);DWORD code=GetLastError();CloseHandle(input);return ok?setup_bundle_self(b,host):fail(code);
}

bool setup_bundle_cache(const SetupInstallBundle* b,const L4Layout* layout,L4CachedPackage** result){
    const SetupRootAsset* asset=b?setup_root_asset(b->root,2):NULL;if(!asset)return fail(ERROR_INVALID_PARAMETER);
    return l4_package_import(layout,b->files[6],asset->size,asset->sha256,result);
}

bool setup_bundle_open(const wchar_t* directory,const char* version,const char* arch,const L4CatalogRelease* original,SetupInstallBundle** output){return bundle_open(directory,version,arch,original,output,SETUP_ADMISSION_STRICT_REMOTE);}
bool setup_bundle_open_local(const wchar_t* directory,const char* version,const char* arch,const L4CatalogRelease* original,SetupInstallBundle** output){return bundle_open(directory,version,arch,original,output,SETUP_ADMISSION_LOCAL_OFFLINE);}

bool setup_bundle_prepare_bootstrap(SetupInstallBundle* b,const char* version){
    if(!b || !version || b->bootstrap_ready || b->files[7])return fail(ERROR_INVALID_PARAMETER);
    L4CatalogRelease binding={0};if(strlen(version)>=sizeof(binding.version))return fail(ERROR_INVALID_PARAMETER);strcpy_s(binding.version,sizeof(binding.version),version);
    const BYTE* identity=setup_root_identity(b->root);if(!identity)return fail(ERROR_ACCESS_DENIED);memcpy(binding.manifest_sha256,identity,32);
    wcscpy_s(b->names[7],48,L"l4tools-bootstrap.json");wcscpy_s(b->names[8],48,L"l4tools-bootstrap.json.sig");swprintf_s(b->names[9],48,L"l4rollback-%hs.exe",b->arch);
    if(!document(b,7,L4_METADATA_MAX_BYTES) || !document(b,8,L4_METADATA_SIGNATURE_BYTES) ||
       !setup_bootstrap_receipt_trusted(b->documents[7],b->sizes[7],b->documents[8],b->sizes[8],&binding,b->arch,&b->bootstrap) ||
       memcmp(b->bootstrap.publisher,setup_root_publisher(b->root),32) || !open_file(b,9) || !held_hash(b->files[9],b->bootstrap.helper.size,b->bootstrap.helper.sha256) ||
       !setup_signed_executable_policy(b->files[9],b->paths[9],b->bootstrap.publisher,b->policy))return fail(GetLastError()?GetLastError():ERROR_INVALID_DATA);
    b->bootstrap_ready=true;return true;
}
bool setup_bundle_install_bootstrap(const SetupInstallBundle* b,L4Journal* j){
    if(!b || !b->bootstrap_ready || !b->bootstrap.owner_trusted)return fail(ERROR_ACCESS_DENIED);
    return setup_recovery_receipt_install(j,b->arch,&b->bootstrap,b->files[9],b->documents[7],b->sizes[7],b->documents[8],b->sizes[8],b->policy);
}
bool setup_bundle_check_bootstrap(const SetupInstallBundle* b,L4Journal* j){
    if(!b || !b->bootstrap_ready || !b->bootstrap.owner_trusted)return fail(ERROR_ACCESS_DENIED);
    return setup_recovery_receipt_check(j,b->arch,&b->bootstrap,b->policy);
}
