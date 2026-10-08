/* Actual fixed receipt codec and held files; owner signature modeled.
 No SYSTEM, operator, source, SCM or route happy path is substituted. */
#include "../src/acceptance_local.h"
#include "../../l4common/journal_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;static bool owner_signature=true;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL %u error%lu: %s\n",__LINE__,GetLastError(),#x);}}while(0)
bool l4_metadata_verify_trusted(const void* data,DWORD size,const BYTE* signature,DWORD signature_size,BYTE digest[32]){(void)signature;(void)signature_size;if(!owner_signature){SetLastError(ERROR_INVALID_DATA);return false;}return l4_store_hash(data,size,NULL,0,digest);}
#include "../src/acceptance_local.c"

/* Unexercised authority calls fail loudly, never fabricate admission. */
#pragma warning(push)
#pragma warning(disable:4100)
#define REFUSE() do{CHECK(false);SetLastError(ERROR_CALL_NOT_IMPLEMENTED);}while(0)
bool l4_catalog_parse_trusted(const void*a,DWORD b,const BYTE*c,DWORD d,ULONGLONG e,L4Catalog**f){REFUSE();return false;}
void l4_catalog_free(L4Catalog*a){if(a)REFUSE();}
bool l4_catalog_route(const L4Catalog*a,const char*b,const BYTE*c,const char*d,const char*e,const char*f,L4CatalogRoute*g){REFUSE();return false;}
const BYTE* setup_root_identity(const SetupRootManifest*a){REFUSE();return NULL;}
bool setup_installed_source_verify(SetupInstalledSource*a){REFUSE();return false;}
const SetupRootManifest* setup_installed_source_root(const SetupInstalledSource*a){REFUSE();return NULL;}
const char* setup_installed_source_suite_version(const SetupInstalledSource*a){REFUSE();return NULL;}
const char* setup_installed_source_arch(const SetupInstalledSource*a){REFUSE();return NULL;}
bool setup_installed_source_owned_by(const SetupInstalledSource*a,const L4Journal*b){REFUSE();return false;}
bool l4_route_save_trusted(L4Journal*a,const void*b,DWORD c,const BYTE*d,DWORD e,ULONGLONG f,const char*g,const BYTE*h,const char*i,const char*j,const char*k,ULONGLONG*l){REFUSE();return false;}
bool l4_route_load_trusted(L4Journal*a,ULONGLONG b,L4RoutePlan**c){REFUSE();return false;}
void l4_route_free(L4RoutePlan*a){if(a)REFUSE();}
bool l4_route_is_owner_trusted(const L4RoutePlan*a){REFUSE();return false;}
const char* l4_route_requested(const L4RoutePlan*a){REFUSE();return NULL;}
const char* l4_route_arch(const L4RoutePlan*a){REFUSE();return NULL;}
const char* l4_route_profile(const L4RoutePlan*a){REFUSE();return NULL;}
ULONGLONG l4_route_revision(const L4RoutePlan*a){REFUSE();return 0;}
const BYTE* l4_route_catalog_sha256(const L4RoutePlan*a){REFUSE();return NULL;}
bool l4_route_source(const L4RoutePlan*a,L4CatalogRelease*b){REFUSE();return false;}
bool l4_journal_reader_replay(const L4JournalReader*a,L4JournalVisitor b,void*c){REFUSE();return false;}
const wchar_t* l4_journal_reader_directory(const L4JournalReader*a){REFUSE();return NULL;}
bool l4_journal_reader_is_immutable(const L4JournalReader*a){REFUSE();return false;}
ULONGLONG setup_operation_sequence(const SetupOperationPlan*a){REFUSE();return 0;}
bool setup_operation_binding(const SetupOperationPlan*a,L4Journal*b,ULONGLONG c){REFUSE();return false;}
const L4RoutePlan* setup_operation_route(const SetupOperationPlan*a){REFUSE();return NULL;}
const SetupRootManifest* setup_operation_target_root(const SetupOperationPlan*a,unsigned b){REFUSE();return NULL;}
unsigned setup_operation_hops(const SetupOperationPlan*a){REFUSE();return 0;}
bool l4_platform_current(char a[L4_PLATFORM_PROFILE_SIZE]){REFUSE();return false;}
bool l4_remote_request_load(L4Journal*a,L4RemoteRequest*b){REFUSE();return false;}
bool l4_remote_request_decode(const L4Layout*a,const void*b,DWORD c,L4RemoteRequest*d){REFUSE();return false;}
bool l4_remote_host_load(L4Journal*a,L4RemoteHostReceipt*b){REFUSE();return false;}
bool uac_is_elevated(void){REFUSE();return false;}
#pragma warning(pop)
static void receipt(SetupAcceptancePin* p){memset(p,0,sizeof(*p));CHECK(l4_layout_from_roots(&p->layout,L"C:\\AcceptancePF",L"C:\\AcceptancePD",L"1.13.7"));wcscpy_s(p->directory,MAX_PATH,L"C:\\AcceptancePD\\operations\\17730000-0000-4000-8000-000000000001");memcpy(p->intent,"L4ACTR01",8);l4_store_u32(p->intent+8,1);l4_store_u32(p->intent+32,1);DWORD size=68;CHECK(CreateWellKnownSid(WinBuiltinUsersSid,NULL,p->intent+40,&size));l4_store_u32(p->intent+36,size);strcpy_s((char*)p->intent+108,37,"17730000-0000-4000-8000-000000000001");strcpy_s((char*)p->intent+145,32,"1.13.7");strcpy_s((char*)p->intent+177,32,"1.13.8");strcpy_s((char*)p->intent+209,8,"x86");ULONGLONG now=utc();l4_store_u64(p->intent+16,now-10000000ULL);l4_store_u64(p->intent+24,now+600000000ULL);}
static void doc(SetupAcceptancePin* p,char json[2048],unsigned mismatch){ULONGLONG expiry=(utc()-116444736000000000ULL)/10000000ULL+3600;sprintf_s(json,2048,"{\"schema\":1,\"kind\":\"l4tools-local-acceptance\",\"operation_id\":\"17730000-0000-4000-8000-00000000000%u\",\"source_version\":\"1.13.%u\",\"target_version\":\"1.13.%u\",\"arch\":\"%s\",\"force_rollback\":true,\"catalog_sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"expires_at\":%llu,\"terminal_id\":%u,\"tenant_id\":%u%s}",mismatch==1?2:1,mismatch==2?6:7,mismatch==3?9:8,mismatch==4?"x64":"x86",expiry,mismatch==5?772:773,mismatch==6?2:1,mismatch==7?",\"PASS\":true":"");p->authorization=(BYTE*)json;p->authorization_size=(DWORD)strlen(json);}
/* Real canonical ACLs, invoking production optional-input gate; no SYSTEM,
 * operator signature/source or successful trial authority is fabricated. */
static bool optional_inputs(const L4Layout* roots,const wchar_t* directory){
 SetupAcceptancePin* p=calloc(1,sizeof(*p));CHECK(p);if(!p)return false;p->layout=*roots;wcscpy_s(p->directory,MAX_PATH,directory);SetupAcceptancePin* out=NULL;
 bool ok=open_inputs(p,true,&out);CHECK(!out);return ok;
}
static void input_acl(void){
 wchar_t temp[MAX_PATH],data[MAX_PATH],update[MAX_PATH],ops[MAX_PATH],dir[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));
 swprintf_s(data,MAX_PATH,L"%lsL4AcceptanceInputAcl-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());swprintf_s(update,MAX_PATH,L"%ls\\update",data);swprintf_s(ops,MAX_PATH,L"%ls\\operations",update);swprintf_s(dir,MAX_PATH,L"%ls\\17730000-0000-4000-8000-000000000031",ops);
 PSECURITY_DESCRIPTOR pub=NULL,priv=NULL,write=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",SDDL_REVISION_1,&pub,NULL));CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",SDDL_REVISION_1,&priv,NULL));CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;BU)",SDDL_REVISION_1,&write,NULL));
 SECURITY_ATTRIBUTES readable={sizeof(readable),pub,FALSE},private={sizeof(private),priv,FALSE};CHECK(CreateDirectoryW(data,&readable));CHECK(CreateDirectoryW(update,&readable));CHECK(CreateDirectoryW(ops,&readable));CHECK(CreateDirectoryW(dir,&private));
 L4Layout roots;CHECK(l4_layout_from_roots(&roots,L"C:\\AcceptanceFixturePF",data,L"1.13.7"));L4FileFence old={0};CHECK(!l4_store_pin(dir,ops,true,&old)&&GetLastError()==ERROR_ACCESS_DENIED);CHECK(optional_inputs(&roots,dir));CHECK(SetFileSecurityW(dir,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,pub));CHECK(!optional_inputs(&roots,dir)&&GetLastError()==ERROR_ACCESS_DENIED);CHECK(SetFileSecurityW(dir,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,priv));CHECK(SetFileSecurityW(ops,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,write));CHECK(!optional_inputs(&roots,dir)&&GetLastError()==ERROR_ACCESS_DENIED);CHECK(SetFileSecurityW(ops,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,pub));CHECK(optional_inputs(&roots,dir));
 CHECK(RemoveDirectoryW(dir));CHECK(RemoveDirectoryW(ops));CHECK(RemoveDirectoryW(update));CHECK(RemoveDirectoryW(data));LocalFree(pub);LocalFree(priv);LocalFree(write);
}
int wmain(void){input_acl();volatile DWORD inner=SETUP_ACCEPTANCE_INNER_WAIT_MS,outer=SETUP_ACCEPTANCE_OUTER_WAIT_MS;CHECK(inner==8880000u && outer==9060000u);CHECK(!sufficient_lifetime(utc()+10000000ULL));CHECK(!sufficient_lifetime(utc()-1));CHECK(sufficient_lifetime(utc()+((ULONGLONG)SETUP_ACCEPTANCE_OUTER_WAIT_MS+1000)*10000ULL));CHECK(!sufficient_lifetime(utc()+((ULONGLONG)SETUP_ACCEPTANCE_OUTER_WAIT_MS-1000)*10000ULL));SetupAcceptancePin* absent=(SetupAcceptancePin*)1;bool forced=true;CHECK(!setup_acceptance_open_optional(NULL,NULL));CHECK(!setup_acceptance_open_optional(NULL,&absent)&&!absent);CHECK(!setup_acceptance_route(NULL,NULL,NULL));CHECK(!setup_acceptance_forcepoint(NULL,NULL,NULL));CHECK(!setup_acceptance_forcepoint(NULL,NULL,&forced)&&!forced);CHECK(!setup_acceptance_open_snapshot(NULL,NULL,NULL,&absent)&&!absent);CHECK(!setup_acceptance_facts(NULL));SetupAcceptancePin p,saved;receipt(&p);saved=p;CHECK(decode(&p));for(unsigned i=0;i<INTENT_SIZE;i++){p=saved;p.intent[i]^=0x80;if(i<16||i>=289||i==108||i==145||i==177||i==209)CHECK(!decode(&p));}p=saved;l4_store_u64(p.intent+24,utc()-1);CHECK(!decode(&p));p=saved;l4_store_u64(p.intent+16,utc()+100000000ULL);CHECK(!decode(&p));p=saved;l4_store_u64(p.intent+24,l4_store_get64(p.intent+16)+144000000001ULL);CHECK(!decode(&p));p=saved;strcpy_s((char*)p.intent+108,37,"00000000-0000-0000-0000-000000000000");CHECK(!decode(&p));char json[2048];p=saved;doc(&p,json,0);CHECK(authorization(&p,true)&&l4_store_get32(p.intent+12)==1);CHECK(authorization(&p,false));l4_store_u32(p.intent+12,0);CHECK(!authorization(&p,false));for(unsigned i=1;i<=7;i++){p=saved;doc(&p,json,i);CHECK(!authorization(&p,true));}p=saved;doc(&p,json,0);owner_signature=false;CHECK(!authorization(&p,true));owner_signature=true;p.intent[217]=1;CHECK(!authorization(&p,true));
 wchar_t temp[MAX_PATH],file[MAX_PATH],alias[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));swprintf_s(file,MAX_PATH,L"%lsL4Acceptance-%lu-%llu.bin",temp,GetCurrentProcessId(),GetTickCount64());HANDLE writer=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);DWORD wrote=0;CHECK(writer!=INVALID_HANDLE_VALUE&&WriteFile(writer,"receipt",7,&wrote,NULL)&&wrote==7);CHECK(CloseHandle(writer));HANDLE held=NULL;BYTE* bytes=NULL;DWORD size=0;CHECK(read_file(file,8,false,&held,&bytes,&size)&&size==7&&!memcmp(bytes,"receipt",7));writer=CreateFileW(file,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(writer==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_SHARING_VIOLATION);CHECK(!DeleteFileW(file)&&GetLastError()==ERROR_SHARING_VIOLATION);free(bytes);CHECK(CloseHandle(held));bytes=NULL;CHECK(!read_file(file,6,false,&held,&bytes,&size));CHECK(CloseHandle(held));swprintf_s(alias,MAX_PATH,L"%ls.alias",file);CHECK(CreateHardLinkW(alias,file,NULL));CHECK(!read_file(file,8,false,&held,&bytes,&size));CHECK(CloseHandle(held));CHECK(DeleteFileW(alias));swprintf_s(alias,MAX_PATH,L"%ls:unexpected",file);writer=CreateFileW(alias,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);CHECK(writer!=INVALID_HANDLE_VALUE);CHECK(CloseHandle(writer));CHECK(!read_file(file,8,false,&held,&bytes,&size));CHECK(CloseHandle(held));CHECK(DeleteFileW(alias));CHECK(DeleteFileW(file));printf("Local acceptance receipt: %u passed, %u failed; codec/auth bindings/file pins actual, RSA modeled; no live SYSTEM/SCM\n",checks-failures,failures);return failures?1:0;}
