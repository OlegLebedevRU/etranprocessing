/* Actual producer -> strict active-updater reader contract, no signature model,
 * installed paths, SCM, pointer creation or authority initialization. */
#include "../src/install_bundle.c"
#define fail updater_test_fail
#include "../../l4common/active_updater.c"
#undef fail
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL %u %s error=%lu\n",__LINE__,#x,GetLastError());}}while(0)
int wmain(void){
 wchar_t temp[MAX_PATH],root[MAX_PATH],source[MAX_PATH],private_file[MAX_PATH],inherited[MAX_PATH],public_file[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));swprintf_s(root,MAX_PATH,L"%lsL4BundleAcl-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());
 PSECURITY_DESCRIPTOR sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",SDDL_REVISION_1,&sd,NULL));SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};CHECK(CreateDirectoryW(root,&sa));LocalFree(sd);
 swprintf_s(source,MAX_PATH,L"%ls\\source",root);swprintf_s(private_file,MAX_PATH,L"%ls\\private",root);swprintf_s(inherited,MAX_PATH,L"%ls\\inherited",root);swprintf_s(public_file,MAX_PATH,L"%ls\\public",root);
 const BYTE payload[]={1,2,3,4,5};BYTE digest[32];CHECK(l4_store_hash(payload,sizeof(payload),NULL,0,digest));HANDLE src=CreateFileW(source,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);CHECK(src!=INVALID_HANDLE_VALUE);CHECK(l4_store_write(src,payload,sizeof(payload)));
 CHECK(copy_held(src,private_file,sizeof(payload),digest,true));HANDLE file=open_fixed(root,L"private");CHECK(file!=INVALID_HANDLE_VALUE);BYTE* bytes=NULL;DWORD size=0;CHECK(read_bytes(file,128,&bytes,&size)&&size==sizeof(payload)&&!memcmp(bytes,payload,size));free(bytes);CloseHandle(file);
 CHECK(copy_held(src,private_file,sizeof(payload),digest,true));BYTE wrong[32];memcpy(wrong,digest,32);wrong[0]^=1;CHECK(!copy_held(src,private_file,sizeof(payload),wrong,true));
 CHECK(copy_held(src,inherited,sizeof(payload),digest,false));file=open_fixed(root,L"inherited");CHECK(file!=INVALID_HANDLE_VALUE);CHECK(!read_bytes(file,128,&bytes,&size));CHECK(!private_destination(file));CloseHandle(file);CHECK(!copy_held(src,inherited,sizeof(payload),digest,true));
 CHECK(copy_held(src,public_file,sizeof(payload),digest,false));file=open_fixed(root,L"public");CHECK(file!=INVALID_HANDLE_VALUE);CHECK(safe_file(file,false));CloseHandle(file);
 CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GR;;;BU)",SDDL_REVISION_1,&sd,NULL));CHECK(SetFileSecurityW(private_file,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd));LocalFree(sd);CHECK(!copy_held(src,private_file,sizeof(payload),digest,true));file=open_fixed(root,L"private");CHECK(file!=INVALID_HANDLE_VALUE);CHECK(!read_bytes(file,128,&bytes,&size));CloseHandle(file);
 CloseHandle(src);CHECK(DeleteFileW(source));CHECK(DeleteFileW(private_file));CHECK(DeleteFileW(inherited));CHECK(DeleteFileW(public_file));CHECK(RemoveDirectoryW(root));printf("bundle copy actual ACL/strict-reader: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
/* Copy-only fixture: unrelated admission paths must never be entered. These
 * fail loudly rather than granting modeled signature/root/package authority. */
#pragma warning(push)
#pragma warning(disable:4100)
static void unused_admission(void){++failures;fputs("unexpected admission path\n",stderr);SetLastError(ERROR_CALL_NOT_IMPLEMENTED);}
bool setup_signed_executable_policy(HANDLE h,const wchar_t* p,const BYTE publisher[32],SetupAdmissionPolicy policy){unused_admission();return false;}
bool setup_root_parse_trusted(const void* b,DWORD n,const BYTE* s,DWORD sn,const L4CatalogRelease* r,const char* a,SetupRootManifest** out){unused_admission();return false;}
void setup_root_free(SetupRootManifest* r){unused_admission();}
const SetupRootAsset* setup_root_asset(const SetupRootManifest* r,unsigned i){unused_admission();return NULL;}
const SetupRootAsset* setup_root_installer(const SetupRootManifest* r){unused_admission();return NULL;}
const BYTE* setup_root_publisher(const SetupRootManifest* r){unused_admission();return NULL;}
const BYTE* setup_root_identity(const SetupRootManifest* r){unused_admission();return NULL;}
bool setup_root_descriptor_trusted(const SetupRootManifest* r,const void* b,DWORD n,const BYTE* s,DWORD sn,const L4Layout* l,SetupManifest** out){unused_admission();return false;}
bool l4_catalog_parse_trusted(const void* b,DWORD n,const BYTE* s,DWORD sn,ULONGLONG now,L4Catalog** out){unused_admission();return false;}
void l4_catalog_free(L4Catalog* c){unused_admission();}
bool l4_catalog_resolve(const L4Catalog* c,const char* r,L4CatalogRelease* out){unused_admission();return false;}
bool l4_package_import(const L4Layout* l,HANDLE h,ULONGLONG n,const BYTE sha[32],L4CachedPackage** out){unused_admission();return false;}
bool setup_bootstrap_receipt_trusted(const void* b,DWORD n,const BYTE* s,DWORD sn,const L4CatalogRelease* r,const char* a,SetupBootstrapReceipt* out){unused_admission();return false;}
bool setup_recovery_receipt_check(L4Journal* j,const char* a,const SetupBootstrapReceipt* r,SetupAdmissionPolicy p){unused_admission();return false;}
bool setup_recovery_receipt_install(L4Journal* j,const char* a,const SetupBootstrapReceipt* r,HANDLE h,const void* b,DWORD n,const BYTE* s,DWORD sn,SetupAdmissionPolicy p){unused_admission();return false;}
#pragma warning(pop)
