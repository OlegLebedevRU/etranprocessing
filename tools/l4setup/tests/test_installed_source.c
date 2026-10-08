/* Native receipt/reader guards, no production trust substitution or SCM calls. */
#include "../src/installed_source.c"
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL source %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static void updater_guards(void){SetupUpdaterIdentity* p=(SetupUpdaterIdentity*)1;CHECK(!setup_updater_identity_from_source(NULL,&p)&&p==NULL);CHECK(!setup_updater_identity_verify(NULL));CHECK(setup_updater_identity_version(NULL)==NULL);CHECK(setup_updater_identity_asset(NULL)==NULL);setup_updater_identity_free(NULL);Saved h={0};CHECK(saved(102,1,NULL,0,&h)&&h.commit==1);CHECK(!saved(102,2,NULL,0,&h)&&GetLastError()==ERROR_INVALID_DATA);SetupInstalledSource source={0};strcpy_s(source.version,64,"1.13.7");strcpy_s(source.updater_version,64,"1.13.6");CHECK(!strcmp(setup_installed_source_suite_version(&source),"1.13.7")&&!strcmp(setup_installed_source_updater_version(&source),"1.13.6"));wchar_t visited[HISTORY_LIMIT][40]={0};wcscpy_s(source.operation,40,L"17730000-0000-4000-8000-000000000001");wcscpy_s(visited[0],40,source.operation);CHECK(!historical(&source,visited,1)&&GetLastError()==ERROR_CIRCULAR_DEPENDENCY);CHECK(!historical(&source,visited,HISTORY_LIMIT)&&GetLastError()==ERROR_TOO_MANY_NAMES);}
static void cleanup(const wchar_t* root){wchar_t pattern[MAX_PATH],child[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",root);WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW(pattern,&d);if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L".."))continue;swprintf_s(child,MAX_PATH,L"%ls\\%ls",root,d.cFileName);if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))cleanup(child);else RemoveDirectoryW(child);}else DeleteFileW(child);}while(FindNextFileW(h,&d));FindClose(h);}RemoveDirectoryW(root);}
static void config_guard_tests(void){
 BYTE remote_bytes[128]={0};const BYTE* candidate=NULL;DWORD candidate_size=0;const char* relative="l4superv.json";DWORD path_size=(DWORD)strlen(relative);l4_store_u32(remote_bytes,1);l4_store_u32(remote_bytes+4,1);l4_store_u32(remote_bytes+8,path_size);l4_store_u32(remote_bytes+12,1);l4_store_u32(remote_bytes+16,1);l4_store_u32(remote_bytes+20,sizeof(SECURITY_DESCRIPTOR_RELATIVE));memcpy(remote_bytes+24,relative,path_size);remote_bytes[24+path_size]='o';remote_bytes[25+path_size]='n';DWORD record_size=26+path_size+sizeof(SECURITY_DESCRIPTOR_RELATIVE);CHECK(!config_baseline(remote_bytes,record_size,0,&candidate,&candidate_size));CHECK(config_baseline_mode(remote_bytes,record_size,0,&candidate,&candidate_size,true)&&candidate_size==1&&*candidate=='n');CHECK(!config_baseline_mode(remote_bytes,record_size-1,0,&candidate,&candidate_size,true));
 const char* names[]={"l4superv.json","mosquitto\\mosquitto.conf","launchers\\ffmpeg.target"};BYTE b[128]={0};const BYTE* out=NULL;DWORD length=0;
 for(unsigned i=0;i<3;i++){unsigned index=i<2?i:11;DWORD path=(DWORD)strlen(names[i]);DWORD size=24+path+1+sizeof(SECURITY_DESCRIPTOR_RELATIVE);memset(b,0,sizeof(b));l4_store_u32(b,i==1?2:1);l4_store_u32(b+8,path);l4_store_u32(b+16,1);l4_store_u32(b+20,sizeof(SECURITY_DESCRIPTOR_RELATIVE));memcpy(b+24,names[i],path);b[24+path]='x';
  CHECK(config_baseline(b,size,index,&out,&length));CHECK(length==1&&*out=='x');CHECK(!config_baseline(b,size-1,index,&out,&length));l4_store_u32(b+4,1);CHECK(!config_baseline(b,size,index,&out,&length));l4_store_u32(b+4,0);l4_store_u32(b,3);CHECK(!config_baseline(b,size,index,&out,&length));
 }
}
int wmain(void){updater_guards();config_guard_tests();CHECK(!source_system());CHECK(canonical_id(L"17730000-0000-4000-8000-0000000000ab"));CHECK(!canonical_id(L"17730000-0000-4000-8000-0000000000AB"));Saved saved_plan={0};CHECK(saved(85,1,NULL,0,&saved_plan));CHECK(!saved(85,2,NULL,0,&saved_plan));CHECK(saved(40,2,NULL,0,&saved_plan));CHECK(!saved(40,3,NULL,0,&saved_plan));
 wchar_t temp[MAX_PATH],root[MAX_PATH],pf[MAX_PATH],pd[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));swprintf_s(root,MAX_PATH,L"%lsl4source-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());if(!CreateDirectoryW(root,NULL))return 1;swprintf_s(pf,MAX_PATH,L"%ls\\PF",root);swprintf_s(pd,MAX_PATH,L"%ls\\PD",root);L4Layout layout;CHECK(l4_layout_from_roots(&layout,pf,pd,L"1.13.8"));CHECK(l4_layout_prepare(&layout));L4Journal *old=NULL,*owner=NULL;const wchar_t* source=L"17730000-0000-4000-8000-0000000000ab";const wchar_t* current=L"17730000-0000-4000-8000-0000000000cd";CHECK(l4_journal_open(&layout,source,true,&old));if(!old){cleanup(root);return 1;}ULONGLONG refs[13];for(unsigned i=0;i<13;i++)CHECK(l4_journal_append(old,20,"fixture",7,&refs[i]));ULONGLONG boot,path,good,bad,wrong_path;
 CHECK(l4_journal_append(old,40,"fixture",7,&boot));BYTE path_bytes[26]={0};memcpy(path_bytes,"L4PATH01",8);l4_store_u32(path_bytes+16,REG_EXPAND_SZ);l4_store_u32(path_bytes+20,2);CHECK(l4_journal_append(old,80,path_bytes,26,&path));
 BYTE b[174]={0};memcpy(b,"L4FRSH01",8);memset(b+8,0x42,32);l4_store_u64(b+40,boot);l4_store_u64(b+48,path);l4_store_u32(b+56,13);l4_store_u32(b+60,1);l4_store_u32(b+64,1);for(unsigned i=0;i<13;i++)l4_store_u64(b+68+i*8,refs[i]);b[172]='d';b[173]='s';CHECK(l4_journal_append(old,85,b,sizeof(b),&good));l4_store_u64(b+68+8,refs[0]);CHECK(l4_journal_append(old,85,b,sizeof(b),&bad));l4_store_u64(b+68+8,refs[1]);l4_store_u64(b+48,refs[0]);CHECK(l4_journal_append(old,85,b,sizeof(b),&wrong_path));l4_journal_close(old);
 CHECK(l4_journal_open(&layout,current,true,&owner));if(!owner){cleanup(root);return 1;}SetupInstalledSource s={0};s.bootstrap=boot;s.receipt=good;strcpy_s(s.version,64,"1.13.8");CHECK(l4_journal_reader_open(owner,source,&s.reader));if(s.reader){L4CatalogRelease release;CHECK(receipt(&s,&release));CHECK(!strcmp(release.version,"1.13.8")&&release.manifest_sha256[0]==0x42);s.receipt=bad;CHECK(!receipt(&s,&release));s.receipt=wrong_path;CHECK(!receipt(&s,&release));s.receipt=good;s.bootstrap=boot+1;CHECK(!receipt(&s,&release));l4_journal_reader_close(s.reader);}l4_journal_close(owner);cleanup(root);CHECK(GetFileAttributesW(root)==INVALID_FILE_ATTRIBUTES);printf("installed source receipt guards: %u checks, %u failures\n",checks,failures);return failures?1:0;}
/* Receipt-only fixture must never enter external authentication or SCM paths.
 * Missing dependencies fail loudly; they do not confer fixture trust. */
static bool unused(void){++failures;SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return false;}
bool l4_release_pin(const L4Layout* l,const L4ReleaseFile* f,L4ReleaseFence** r,wchar_t p[MAX_PATH]){(void)l;(void)f;(void)r;(void)p;return unused();}
void l4_release_unpin(L4ReleaseFence* f){(void)f;unused();}
void setup_manifest_free(SetupManifest* m){(void)m;unused();}
const L4ReleaseFile* setup_manifest_files(const SetupManifest* m,unsigned* n){(void)m;(void)n;unused();return NULL;}
bool setup_manifest_verify(const SetupManifest* m){(void)m;return unused();}
const BYTE* setup_root_identity(const SetupRootManifest* r){(void)r;unused();return NULL;}
bool setup_bundle_open(const wchar_t* p,const char* v,const char* a,const L4CatalogRelease* o,SetupInstallBundle** b){(void)p;(void)v;(void)a;(void)o;(void)b;return unused();}
void setup_bundle_free(SetupInstallBundle* b){(void)b;unused();}
const SetupRootManifest* setup_bundle_root(const SetupInstallBundle* b){(void)b;unused();return NULL;}
bool setup_bundle_manifest(const SetupInstallBundle* b,const L4Layout* l,SetupManifest** m){(void)b;(void)l;(void)m;return unused();}
const void* setup_bundle_descriptor(const SetupInstallBundle* b,DWORD* n){(void)b;(void)n;unused();return NULL;}
const BYTE* setup_bundle_signature(const SetupInstallBundle* b,DWORD* n){(void)b;(void)n;unused();return NULL;}
bool setup_bundle_self(const SetupInstallBundle* b,const wchar_t* p){(void)b;(void)p;return unused();}
bool l4_bootstrap_load(L4Journal* j,ULONGLONG n,L4BootstrapPlan* p){(void)j;(void)n;(void)p;return unused();}
bool l4_bootstrap_terminal(L4Journal* j,ULONGLONG n,bool* c,bool* a){(void)j;(void)n;(void)c;(void)a;return unused();}
bool l4_bootstrap_local_terminal(L4Journal* j,ULONGLONG n,bool* c){(void)j;(void)n;(void)c;return unused();}
bool l4_access_service_token(const wchar_t* name,HANDLE* token){(void)name;(void)token;return unused();}

const SetupRootAsset* setup_root_installer(const SetupRootManifest* r){(void)r;unused();return NULL;}
const BYTE* setup_root_publisher(const SetupRootManifest* r){(void)r;unused();return NULL;}
bool setup_signed_executable(HANDLE h,const wchar_t* p,const BYTE publisher[32]){(void)h;(void)p;(void)publisher;return unused();}

bool setup_remote_commit_admit_target(const L4JournalReader* r,const L4Layout* l,SetupRemoteCommit* p,SetupOperationPlan** o){(void)r;(void)l;(void)p;(void)o;return unused();}
void setup_operation_free(SetupOperationPlan* p){if(p)unused();}
const SetupRootManifest* setup_operation_target_root(const SetupOperationPlan* p,unsigned h){(void)p;(void)h;unused();return NULL;}
const SetupManifest* setup_operation_target(const SetupOperationPlan* p,unsigned h){(void)p;(void)h;unused();return NULL;}
const L4RoutePlan* setup_operation_route(const SetupOperationPlan* p){(void)p;unused();return NULL;}
unsigned setup_operation_hops(const SetupOperationPlan* p){(void)p;unused();return 0;}
const L4ServiceSwitch* setup_operation_switch(const SetupOperationPlan* p,unsigned h,unsigned i){(void)p;(void)h;(void)i;unused();return NULL;}
bool setup_operation_verify_images(const SetupOperationPlan* p){(void)p;return unused();}
const L4Layout* setup_manifest_layout(const SetupManifest* m){(void)m;unused();return NULL;}
const char* l4_route_arch(const L4RoutePlan* p){(void)p;unused();return NULL;}
bool l4_route_source(const L4RoutePlan* p,L4CatalogRelease* r){(void)p;(void)r;return unused();}
