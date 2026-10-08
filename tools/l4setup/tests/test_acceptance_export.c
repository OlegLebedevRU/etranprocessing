/* Composition fixture: trust/SCM/identity/private handles are modeled explicitly.
 * The production exporter/JSON/binding/control flow is compiled unchanged. No
 * production files, services, MQTT clients, certificates or owner keys used. */
#include "../src/acceptance_export.h"
#include "../src/acceptance_local.h"
#include "../src/remote_commit.h"
#include "../src/remote_restore.h"
#include "../../l4common/remote_status.h"
#include "../../l4common/update_state.h"
#include "../../l4common/journal_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct L4JournalReader{int unused;};struct SetupOperationPlan{int unused;};struct SetupAcceptancePin{int unused;};struct SetupRootManifest{int unused;};
static struct L4JournalReader model_reader;static struct SetupOperationPlan model_plan;static struct SetupAcceptancePin model_purpose;static struct SetupRootManifest model_root;
static L4Layout model_roots;static L4RemoteStatus model_status;static SetupAcceptanceFacts model_facts;static SetupRootAsset model_descriptor;static SetupRemoteCommit model_commit;static SetupRemoteRestoreProof model_restore;
static ULONGLONG model_refs[12];static unsigned checks,failures,writes,created,closed,opens,clear_reads;static int fault;static char saved[SETUP_ACCEPTANCE_EXPORT_LIMIT];
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL export:%u %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static BOOL WINAPI token_open(HANDLE p,DWORD a,PHANDLE h){(void)p;(void)a;*h=(HANDLE)1;return TRUE;}
static BOOL WINAPI thread_open(HANDLE t,DWORD a,BOOL s,PHANDLE h){(void)t;(void)a;(void)s;(void)h;SetLastError(ERROR_NO_TOKEN);return FALSE;}
static BOOL WINAPI token_info(HANDLE h,TOKEN_INFORMATION_CLASS c,LPVOID out,DWORD size,PDWORD n){(void)h;(void)size;*n=sizeof(DWORD);if(c==TokenUser)((TOKEN_USER*)out)->User.Sid=(PSID)1;else if(c==TokenType)*(TOKEN_TYPE*)out=TokenPrimary;else *(DWORD*)out=0;return TRUE;}
static BOOL WINAPI known_sid(PSID s,WELL_KNOWN_SID_TYPE t){(void)s;(void)t;return fault!=1;}
static BOOL WINAPI handle_close(HANDLE h){(void)h;closed++;return TRUE;}
static HANDLE WINAPI file_open(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES attrs,DWORD disposition,DWORD flags,HANDLE template){(void)access;(void)attrs;(void)flags;(void)template;
 if(wcsstr(path,L"acceptance.result.json")){CHECK(disposition==CREATE_NEW&&share==0);created++;if(fault==19){SetLastError(ERROR_FILE_EXISTS);return INVALID_HANDLE_VALUE;}return (HANDLE)3;}
 CHECK(disposition==OPEN_EXISTING&&share==0);opens++;if(fault==2){SetLastError(ERROR_SHARING_VIOLATION);return INVALID_HANDLE_VALUE;}return (HANDLE)2;
}
static BOOL WINAPI file_info(HANDLE h,LPBY_HANDLE_FILE_INFORMATION info){(void)h;memset(info,0,sizeof(*info));info->nNumberOfLinks=fault==3?2:1;return TRUE;}
static BOOL WINAPI file_info_ex(HANDLE h,FILE_INFO_BY_HANDLE_CLASS c,LPVOID p,DWORD n){(void)h;(void)c;memset(p,0,n);FILE_STREAM_INFO* s=p;s->StreamNameLength=14;memcpy(s->StreamName,L"::$DATA",14);return TRUE;}
static BOOL WINAPI sd_owner(PSECURITY_DESCRIPTOR sd,PSID* owner,LPBOOL d){(void)sd;*owner=(PSID)1;*d=FALSE;return TRUE;}
static BOOL WINAPI file_write(HANDLE h,LPCVOID b,DWORD n,LPDWORD out,LPOVERLAPPED o){(void)h;(void)o;if(fault==20){SetLastError(ERROR_WRITE_FAULT);return FALSE;}CHECK(n<sizeof(saved));memcpy(saved,b,n);saved[n]=0;*out=n;writes++;return TRUE;}
static BOOL WINAPI file_flush(HANDLE h){(void)h;return TRUE;}
#define OpenProcessToken token_open
#define OpenThreadToken thread_open
#define GetTokenInformation token_info
#define IsWellKnownSid known_sid
#define CloseHandle handle_close
#define CreateFileW file_open
#define GetFileInformationByHandle file_info
#define GetFileInformationByHandleEx file_info_ex
#define GetSecurityDescriptorOwner sd_owner
#define WriteFile file_write
#define FlushFileBuffers file_flush
#include "../src/acceptance_export.c"

bool l4_store_security(HANDLE h,bool control,BYTE** sd,DWORD* n){(void)h;(void)control;*sd=calloc(1,32);*n=32;return *sd!=NULL;}
bool l4_store_pin(const wchar_t* p,const wchar_t* r,bool c,L4FileFence* f){(void)p;(void)r;(void)c;(void)f;return true;}
void l4_store_unpin(L4FileFence* f){(void)f;}
bool l4_layout_data_path(const L4Layout* l,const wchar_t* relative,wchar_t out[MAX_PATH]){(void)l;swprintf_s(out,MAX_PATH,L"C:\\fixture\\%ls",relative);return true;}
bool l4_journal_reader_open_fixed_immutable_reference(const L4Layout* l,const wchar_t* id,L4JournalReader** out){(void)l;(void)id;*out=&model_reader;return fault!=4;}
bool l4_journal_reader_is_immutable(const L4JournalReader* r){(void)r;return fault!=5;}
void l4_journal_reader_close(L4JournalReader* r){(void)r;}
const wchar_t* l4_journal_reader_directory(const L4JournalReader* r){(void)r;return L"C:\\fixture\\17730000-0000-4000-8000-000000000001";}
bool l4_remote_status_snapshot(const L4JournalReader* r,const L4Layout* l,L4RemoteStatus* out){(void)r;(void)l;*out=model_status;if(fault==6)out->has_outcome=false;return true;}
bool l4_remote_status_observe(const L4Layout* l,const wchar_t* id,L4RemoteStatus* out){(void)l;(void)id;*out=model_status;out->outcome_cleared=fault!=7;if(fault==8)out->last_sequence++;return true;}
bool l4_update_state_read(const L4Layout* l,L4UpdateState* out){(void)l;memset(out,0,sizeof(*out));strcpy_s(out->owner,40,model_status.operation_id);out->generation=model_status.outcome_clear_generation;out->plan_sequence=model_status.plan_sequence;clear_reads++;if(fault==24&&clear_reads>1)out->generation++;if(fault==9)out->generation++;if(fault==21)out->window=1;if(fault==22)out->owner[0]='2';return true;}
bool setup_update_load_operation_snapshot(const L4JournalReader* r,const L4Layout* l,ULONGLONG s,SetupOperationPlan** out){(void)r;(void)l;(void)s;*out=&model_plan;return fault!=10;}
unsigned setup_operation_hops(const SetupOperationPlan* p){(void)p;return fault==11?2:1;}
bool setup_acceptance_open_snapshot(const L4JournalReader* r,const L4Layout* l,const SetupOperationPlan* p,SetupAcceptancePin** out){(void)r;(void)l;(void)p;*out=fault==12?NULL:&model_purpose;return true;}
bool setup_acceptance_verify_snapshot(SetupAcceptancePin* p,const L4JournalReader* r,const L4Layout* l,const SetupOperationPlan* op){(void)p;(void)r;(void)l;(void)op;return fault!=18;}
const SetupAcceptanceFacts* setup_acceptance_facts(const SetupAcceptancePin* p){(void)p;return &model_facts;}
void setup_acceptance_close(SetupAcceptancePin* p){(void)p;}
void setup_operation_free(SetupOperationPlan* p){(void)p;}
bool l4_store_hash(const void* a,DWORD n,const void* b,DWORD m,BYTE digest[32]){(void)a;(void)n;(void)b;(void)m;memset(digest,fault==13?8:4,32);return true;}
bool l4_journal_reader_find(const L4JournalReader* r,DWORD kind,ULONGLONG seq,BYTE** b,DWORD* n){(void)r;(void)kind;(void)seq;*b=malloc(1);**b=1;*n=1;return true;}
bool l4_journal_reader_replay(const L4JournalReader* r,L4JournalVisitor visitor,void* context){(void)r;bool ok=visitor(103,100,NULL,0,context);return fault==23?ok&&visitor(103,101,NULL,0,context):ok;}
const ULONGLONG* setup_operation_configs(const SetupOperationPlan* p,unsigned* n){(void)p;*n=12;return model_refs;}
bool setup_remote_commit_admit_target(const L4JournalReader* r,const L4Layout* l,SetupRemoteCommit* out,SetupOperationPlan** p){(void)r;(void)l;*out=model_commit;if(fault==14)out->configs[2]++;if(fault==15)out->pids[0]=0;*p=&model_plan;return true;}
bool setup_remote_restore_decode(const void* b,DWORD n,SetupRemoteRestoreProof* out){(void)b;(void)n;*out=model_restore;if(fault==14)out->configs[2]++;if(fault==15)out->births[0].dwLowDateTime=0;return true;}
const SetupRootManifest* setup_operation_root(const SetupOperationPlan* p){(void)p;return &model_root;}
const SetupRootManifest* setup_operation_target_root(const SetupOperationPlan* p,unsigned h){(void)p;(void)h;return &model_root;}
const SetupRootAsset* setup_root_asset(const SetupRootManifest* p,unsigned i){(void)p;(void)i;return &model_descriptor;}
bool l4_platform_read(L4PlatformFacts* p){memset(p,0,sizeof(*p));p->major=10;p->build=19045;p->native_arch=PROCESSOR_ARCHITECTURE_AMD64;p->product_type=VER_NT_WORKSTATION;return true;}
bool l4_platform_format(const L4PlatformFacts* p,char out[L4_PLATFORM_PROFILE_SIZE]){(void)p;strcpy_s(out,L4_PLATFORM_PROFILE_SIZE,fault==16?"wrong":"windows-nt-10.0.19045-x64-client");return true;}
static void reset(bool rollback){fault=0;writes=created=closed=opens=clear_reads=0;memset(saved,0,sizeof(saved));memset(&model_status,0,sizeof(model_status));memset(&model_facts,0,sizeof(model_facts));memset(&model_commit,0,sizeof(model_commit));memset(&model_restore,0,sizeof(model_restore));
 strcpy_s(model_status.operation_id,37,"17730000-0000-4000-8000-000000000001");strcpy_s(model_facts.operation,37,model_status.operation_id);strcpy_s(model_facts.source_version,32,"1.13.7");strcpy_s(model_facts.target_version,32,"1.13.8");strcpy_s(model_facts.arch,8,"x86");strcpy_s(model_facts.profile,sizeof(model_facts.profile),"windows-nt-10.0.19045-x64-client");model_facts.force_rollback=rollback;model_facts.configured_terminal=773;model_facts.configured_tenant=1;model_facts.catalog_revision=1;
 memset(model_facts.source_root,1,32);memset(model_facts.target_root,2,32);memset(model_facts.catalog_sha256,3,32);memset(model_facts.intent_sha256,4,32);memset(model_descriptor.sha256,5,32);model_status.has_host=model_status.has_outcome=model_status.outcome_clear_recorded=true;model_status.plan_sequence=64;model_status.last_sequence=101;model_status.outcome_clear_generation=3;
 strcpy_s(model_status.host.source_version,32,model_facts.source_version);strcpy_s(model_status.host.updater_version,32,model_facts.source_version);strcpy_s(model_status.resolved_version,32,model_facts.target_version);model_status.host.executable_size=123;memset(model_status.host.executable_sha256,6,32);
 model_status.request.accepted_utc=100;model_status.outcome.result.finished_at=200;model_status.outcome.result.result=rollback?L4_REMOTE_OUTCOME_RESTORED:L4_REMOTE_OUTCOME_SUCCESS;model_status.outcome.result.error=rollback?ERROR_CANCELLED:0;model_status.outcome.proof_sequence=99;memset(model_status.outcome.plan_sha256,4,32);memset(model_status.outcome.proof_sha256,4,32);
 model_commit.operation_sequence=model_restore.operation_sequence=64;model_commit.finished_utc=model_restore.finished_utc=190;memset(model_commit.operation_sha256,4,32);memset(model_restore.operation_sha256,4,32);memcpy(model_restore.source_root,model_facts.source_root,32);strcpy_s(model_restore.operation,37,model_facts.operation);strcpy_s(model_restore.version,32,model_facts.source_version);
 for(unsigned i=0;i<12;i++)model_refs[i]=model_commit.configs[i]=model_restore.configs[i]=i+1;for(unsigned i=0;i<4;i++){model_commit.pids[i]=model_restore.pids[i]=100+i;model_commit.births[i].dwLowDateTime=model_restore.births[i].dwLowDateTime=150+i;}
}
int main(void){for(unsigned mode=0;mode<2;mode++){reset(mode!=0);CHECK(setup_acceptance_export(&model_roots,L"17730000-0000-4000-8000-000000000001"));CHECK(writes==1&&created==1&&opens==1);CHECK(strstr(saved,mode?"\"result\":\"RESTORED\"":"\"result\":\"SUCCESS\"")!=NULL);CHECK(strstr(saved,"\"inventory_sha256\"")!=NULL);CHECK(strlen(saved)<8192);
 for(int f=1;f<=25;f++){reset(mode!=0);fault=f;if(f==17)model_status.outcome.result.result=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED;if(f==25)model_facts.configured_tenant=2;CHECK(!setup_acceptance_export(&model_roots,L"17730000-0000-4000-8000-000000000001"));CHECK(!writes);if(f!=19&&f!=20)CHECK(!created);}}
 printf("Acceptance exporter: %u checks, %u failures (modeled trust/SCM/identity/files)\n",checks,failures);return failures?1:0;}
