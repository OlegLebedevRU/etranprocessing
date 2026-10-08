/* Actual protected journal, hash chain, FlushFileBuffers and disk replay;
 * owner-signed metadata and SCM/owned epochs are explicitly modeled. Native
 * real-process/SCM observer boundaries have separate remote_service fixtures. */
#include "../src/remote_service_transaction.h"
#include "../../l4common/journal_internal.h"
#include <stdio.h>
#include <string.h>
static unsigned checks,failures,mutations,observations;static DWORD append_fail;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL line%d error%lu: %s\n",__LINE__,GetLastError(),#x);}}while(0)
static bool fixture_append(L4Journal*,DWORD,const void*,DWORD,ULONGLONG*);
static ULONGLONG fixture_ticks(void),tick_offset;
#define l4_journal_append fixture_append
#define GetTickCount64 fixture_ticks
#include "../src/remote_service_transaction.c"
#undef l4_journal_append
#undef GetTickCount64
static ULONGLONG fixture_ticks(void){return GetTickCount64()+tick_offset;}
struct SetupOperationPlan {ULONGLONG sequence,refs[4],config,configs[12];unsigned hops;bool trusted,images;wchar_t directory[MAX_PATH];};
struct SetupManifest {L4Layout layout;};static SetupManifest target_manifest;
struct SetupRootManifest {BYTE identity[32],publisher[32];};static SetupRootManifest target_root;
static L4ServiceSwitch signed_members[4];static unsigned post_done_fault;
struct SetupRemoteService {SetupRemoteServiceObservation value;bool lost,issued;};
static SetupOperationPlan operation;static SetupRemoteService services[4];static SetupRemoteService* pointers[4];static L4UpdateState current;
static unsigned mutation_mode;static bool observe_fail;
struct L4RecoveryGuard {L4RecoveryPlan plan;};static L4RecoveryGuard recovery;
static bool active_audit_ok=true,helper_wait=true,guard_open_ok=true;static unsigned guard_holds,audits;
bool l4_worker_recheck_active(L4Journal* j,const L4UpdateState* expected,L4WorkerAdmission* out){(void)j;audits++;CHECK(!guard_holds);memset(out,0,sizeof(*out));if(!active_audit_ok || !helper_wait || !same_state(expected,&current))return fail(ERROR_ACCESS_DENIED);out->sequence=operation.sequence;return true;}
bool l4_recovery_open(const L4Layout* layout,const wchar_t* uuid,DWORD timeout,L4RecoveryGuard** out){(void)layout;(void)uuid;(void)timeout;*out=NULL;if(!guard_open_ok)return fail(ERROR_ACCESS_DENIED);guard_holds++;*out=&recovery;return true;}
void l4_recovery_close(L4RecoveryGuard* p){if(p){CHECK(guard_holds==1);guard_holds--;}}
const L4RecoveryPlan* l4_recovery_plan(const L4RecoveryGuard* p){return &p->plan;}
bool l4_recovery_action(L4RecoveryGuard* p,ULONGLONG now,bool boot,L4RecoveryAction* out){(void)p;(void)now;CHECK(!boot);*out=helper_wait?L4_RECOVERY_WAIT:L4_RECOVERY_BLOCKED;return true;}
static unsigned config_state=1;
bool l4_config_verify(L4Journal* j,ULONGLONG ref,bool candidate){(void)j;return ref==operation.config && config_state==(candidate?2u:1u)?true:fail(ERROR_RETRY);}
static L4Journal* active_journal;static unsigned last_kind,last_action;
static bool last_record(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){(void)sequence;(void)context;last_kind=kind;last_action=((kind==100 || kind==104) && size==ACTION_BYTES)?l4_store_get32((const BYTE*)bytes+12):0;return true;}
static bool fixture_append(L4Journal* j,DWORD k,const void* b,DWORD n,ULONGLONG* sequence){if(k==append_fail){SetLastError(ERROR_WRITE_FAULT);return false;}bool ok=l4_journal_append(j,k,b,n,sequence);if(ok && k==101){if(post_done_fault==1)observe_fail=true;if(post_done_fault==2)tick_offset+=2000;}return ok;}
ULONGLONG setup_operation_sequence(const SetupOperationPlan* p){return p->sequence;}
unsigned setup_operation_hops(const SetupOperationPlan* p){return p->hops;}
ULONGLONG setup_operation_switch_reference(const SetupOperationPlan* p,unsigned hop,unsigned service){return !hop && service<4?p->refs[service]:0;}
bool setup_operation_verify_images(const SetupOperationPlan* p){return p->images?true:fail(ERROR_INVALID_IMAGE_HASH);}
const ULONGLONG* setup_operation_configs(const SetupOperationPlan* p,unsigned* count){*count=12;return p->configs;}
const SetupManifest* setup_operation_target(const SetupOperationPlan* p,unsigned hop){(void)p;return !hop?&target_manifest:NULL;}
const SetupRootManifest* setup_operation_target_root(const SetupOperationPlan* p,unsigned hop){(void)p;return !hop?&target_root:NULL;}
const L4ServiceSwitch* setup_operation_switch(const SetupOperationPlan* p,unsigned hop,unsigned service){(void)p;return !hop && service<4?&signed_members[service]:NULL;}
const BYTE* setup_root_identity(const SetupRootManifest* p){return p->identity;}
const BYTE* setup_root_publisher(const SetupRootManifest* p){return p->publisher;}
const L4Layout* setup_manifest_layout(const SetupManifest* p){return &p->layout;}
const char* setup_manifest_arch(const SetupManifest* p){(void)p;return "x86";}
/* Codec is modeled here; production calls actual strict codec, separately
 * covered by test_remote_commit. This fixture tests authority bindings. */
bool setup_remote_commit_decode(const void* bytes,DWORD size,SetupRemoteCommit* out){memset(out,0,sizeof(*out));if(size!=sizeof(*out))return fail(ERROR_INVALID_DATA);memcpy(out,bytes,size);return true;}
bool l4_remote_outcome_decode(const void* bytes,DWORD size,L4RemoteOutcome* out){memset(out,0,sizeof(*out));if(size!=sizeof(*out))return fail(ERROR_INVALID_DATA);memcpy(out,bytes,size);return true;}
bool setup_operation_binding(const SetupOperationPlan* p,L4Journal* j,ULONGLONG seq){BYTE* bytes=NULL;DWORD size=0;
    if(!p->trusted || seq!=p->sequence || wcscmp(p->directory,j->directory))return fail(ERROR_ACCESS_DENIED);
    bool ok=l4_store_find_record(j,64,seq,&bytes,&size) && size==8 && l4_store_get64(bytes)==0x12345678ull;free(bytes);return ok?true:fail(ERROR_INVALID_DATA);}
bool l4_update_state_read(const L4Layout* layout,L4UpdateState* out){(void)layout;*out=current;return true;}
bool setup_remote_service_observe(SetupRemoteService* s,SetupRemoteServiceObservation* out){observations++;memset(out,0,sizeof(*out));if(s->lost || observe_fail)return fail(ERROR_NOT_READY);*out=s->value;return true;}
static bool mutation(SetupRemoteService* s,unsigned action){mutations++;
    /* Native mutation must see the durable pending intent, never just RAM. */
    CHECK(active_journal!=NULL && l4_journal_replay(active_journal,last_record,NULL));CHECK((last_kind==100 || last_kind==104) && last_action==action);
    if(mutation_mode==1)return fail(ERROR_ACCESS_DENIED);
    s->value.phase=action==1?(s->value.phase==4?SETUP_SERVICE_STOPPED_TARGET:SETUP_SERVICE_STOPPED_OLD):action==2?SETUP_SERVICE_STOPPED_TARGET:SETUP_SERVICE_RUNNING_TARGET;
    if(action==3){s->issued=true;if(mutation_mode!=4){s->value.pid+=100;s->value.created.dwLowDateTime+=100;}}
    if(mutation_mode==3)observe_fail=true;if(mutation_mode==5)tick_offset+=2000;
    return mutation_mode==2?fail(ERROR_TIMEOUT):true;
}
bool setup_remote_service_stop(SetupRemoteService* s,DWORD timeout){(void)timeout;return mutation(s,1);}
bool setup_remote_service_switch(SetupRemoteService* s,bool target,DWORD timeout){CHECK(s!=NULL);(void)timeout;bool ok=mutation(s,2);if(!target && mutation_mode!=1)s->value.phase=1;return ok;}
bool setup_remote_service_start(SetupRemoteService* s,bool target,DWORD timeout){CHECK(s!=NULL);(void)timeout;bool ok=mutation(s,3);if(!target && mutation_mode!=1)s->value.phase=3;return ok;}
bool setup_remote_service_observe_start(SetupRemoteService* s,DWORD timeout){(void)timeout;return s->issued && !s->lost?true:fail(ERROR_NOT_READY);}
static unsigned number;
static bool start(const L4Layout* layout,L4Journal** j,SetupServiceTransaction** t){wchar_t uuid[40];swprintf_s(uuid,40,L"432a5678-1234-4876-9456-%012u",++number);
    if(!l4_journal_open(layout,uuid,true,j))return false;active_journal=*j;memset(&operation,0,sizeof(operation));operation.hops=1;operation.trusted=operation.images=true;wcscpy_s(operation.directory,MAX_PATH,(*j)->directory);
    for(unsigned i=0;i<4;i++)if(!l4_journal_append(*j,10,"switch",6,&operation.refs[i]))return false;
    for(unsigned i=0;i<12;i++)if(!l4_journal_append(*j,20,"config",6,&operation.configs[i]))return false;operation.config=operation.configs[0];BYTE plan[8];l4_store_u64(plan,0x12345678);if(!l4_journal_append(*j,64,plan,8,&operation.sequence))return false;
    CHECK(l4_layout_from_roots(&target_manifest.layout,layout->binaries,layout->data,L"1.13.7"));memset(&target_root,1,sizeof(target_root));memset(signed_members,0,sizeof(signed_members));for(unsigned i=0;i<4;i++){swprintf_s(signed_members[i].after,2048,L"signed-command-%u",i);signed_members[i].before.start_type=SERVICE_AUTO_START;}
    memset(&current,0,sizeof(current));WideCharToMultiByte(CP_UTF8,0,uuid,-1,current.owner,40,NULL,NULL);current.window=1;current.generation=8;current.plan_sequence=operation.sequence;current.deadline_utc=123456789;
    memset(services,0,sizeof(services));for(unsigned i=0;i<4;i++){pointers[i]=&services[i];services[i].value.state=current;services[i].value.switch_reference=operation.refs[i];services[i].value.pid=1000+i;services[i].value.created.dwLowDateTime=2000+i;services[i].value.phase=SETUP_SERVICE_RUNNING_OLD;}
    mutation_mode=0;observe_fail=false;append_fail=0;tick_offset=0;post_done_fault=0;active_audit_ok=helper_wait=guard_open_ok=true;config_state=1;guard_holds=0;memset(&recovery,0,sizeof(recovery));memcpy(&recovery.plan.operation,(*j)->header+8,16);recovery.plan.sequence=operation.sequence;recovery.plan.worker_pid=GetCurrentProcessId();FILETIME ended,kernel,user;CHECK(GetProcessTimes(GetCurrentProcess(),&recovery.plan.worker_created,&ended,&kernel,&user));recovery.plan.deadline_utc=current.deadline_utc+10000000;wcscpy_s(recovery.plan.after,2048,signed_members[3].after);recovery.plan.start_type=SERVICE_AUTO_START;
    return setup_service_transaction_open(*j,&operation,pointers,t);
}
static void finish(L4Journal** j,SetupServiceTransaction** t){wchar_t directory[MAX_PATH],file[MAX_PATH];setup_service_transaction_close(*t);*t=NULL;wcscpy_s(directory,MAX_PATH,(*j)->directory);l4_journal_close(*j);*j=NULL;active_journal=NULL;swprintf_s(file,MAX_PATH,L"%ls\\journal.bin",directory);CHECK(DeleteFileW(file));CHECK(RemoveDirectoryW(directory));}
static void window2(void){current.window=2;current.generation++;current.deadline_utc+=1000;for(unsigned i=0;i<4;i++)services[i].value.state=current;}
static void full_pair(SetupServiceTransaction* t){for(unsigned i=0;i<2;i++)for(unsigned a=1;a<=3;a++)CHECK(setup_service_transaction_execute(t,i,(SetupServiceAction)a,&current,1000));}
static SetupRemoteCommit make_commit(L4Journal* j){SetupRemoteCommit c={0};strcpy_s(c.operation,37,current.owner);strcpy_s(c.suite_version,32,"1.13.7");strcpy_s(c.arch,8,"x86");memcpy(c.suite_root,target_root.identity,32);memcpy(c.publisher,target_root.publisher,32);c.operation_sequence=operation.sequence;BYTE* raw=NULL;DWORD length=0;CHECK(l4_store_find_record(j,64,operation.sequence,&raw,&length));CHECK(l4_store_hash(raw,length,NULL,0,c.operation_sha256));free(raw);memcpy(c.configs,operation.configs,sizeof(c.configs));FILETIME now;GetSystemTimeAsFileTime(&now);c.finished_utc=birth(now);for(unsigned i=0;i<4;i++){c.pids[i]=services[i].value.pid;c.births[i]=services[i].value.created;c.start_types[i]=SERVICE_AUTO_START;wcscpy_s(c.commands[i],2048,signed_members[i].after);}return c;}
static bool remove_tree(const wchar_t* root){wchar_t pattern[MAX_PATH],path[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",root);WIN32_FIND_DATAW data;HANDLE search=FindFirstFileW(pattern,&data);bool ok=true;if(search!=INVALID_HANDLE_VALUE){do{if(!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L".."))continue;swprintf_s(path,MAX_PATH,L"%ls\\%ls",root,data.cFileName);if(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){ok=false;break;}if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)ok=remove_tree(path);else ok=DeleteFileW(path)!=0;if(!ok)break;}while(FindNextFileW(search,&data));FindClose(search);}return ok && RemoveDirectoryW(root);}
int wmain(void){wchar_t temp[MAX_PATH],root[MAX_PATH],pf[MAX_PATH],pd[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));swprintf_s(root,MAX_PATH,L"%lsL4ServiceTransaction-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(root,NULL));swprintf_s(pf,MAX_PATH,L"%ls\\PF",root);swprintf_s(pd,MAX_PATH,L"%ls\\PD",root);L4Layout layout;CHECK(l4_layout_from_roots(&layout,pf,pd,L"1.13.6") && l4_layout_prepare(&layout));L4Journal* j=NULL;SetupServiceTransaction* t=NULL;
    CHECK(start(&layout,&j,&t));unsigned before=mutations;SetupRemoteService* duplicate[4]={pointers[0],pointers[0],pointers[2],pointers[3]};SetupServiceTransaction* rejected=NULL;CHECK(!setup_service_transaction_open(j,&operation,duplicate,&rejected) && !rejected);CHECK(!setup_service_transaction_execute(t,0,2,&current,1000) && mutations==before);full_pair(t);window2();for(unsigned i=2;i<4;i++)for(unsigned a=1;a<=3;a++)CHECK(setup_service_transaction_execute(t,i,(SetupServiceAction)a,&current,1000));
    before=mutations;CHECK(setup_service_transaction_execute(t,3,3,&current,1000) && mutations==before);CHECK(!setup_service_transaction_recovery_required(t));CHECK(setup_service_transaction_complete(t,j,&operation,pointers,&current,1000) && mutations==before);setup_service_transaction_close(t);t=NULL;CHECK(setup_service_transaction_open(j,&operation,pointers,&t));CHECK(setup_service_transaction_complete(t,j,&operation,pointers,&current,1000) && mutations==before);services[0].value.pid++;CHECK(!setup_service_transaction_complete(t,j,&operation,pointers,&current,1000));services[0].value.pid--;services[1].value.phase=SETUP_SERVICE_STOPPED_TARGET;CHECK(!setup_service_transaction_complete(t,j,&operation,pointers,&current,1000));services[1].value.phase=SETUP_SERVICE_RUNNING_TARGET;SetupRemoteService* wrong[4]={pointers[0],pointers[1],pointers[2],pointers[0]};CHECK(!setup_service_transaction_complete(t,j,&operation,wrong,&current,1000));finish(&j,&t);
    for(unsigned mode=1;mode<=5;mode++){if(mode==4)continue;CHECK(start(&layout,&j,&t));mutation_mode=mode;before=mutations;CHECK(!setup_service_transaction_execute(t,0,1,&current,1000) && mutations==before+1 && setup_service_transaction_recovery_required(t));ULONGLONG seq=j->sequence;CHECK(!setup_service_transaction_execute(t,1,1,&current,1000) && j->sequence==seq && mutations==before+1);mutation_mode=0;observe_fail=false;
        if(mode==1)CHECK(!setup_service_transaction_reconcile(t,&current,1000) && mutations==before+1);
        else CHECK(setup_service_transaction_reconcile(t,&current,1000) && j->sequence==seq+1 && mutations==before+1);finish(&j,&t);}
    CHECK(start(&layout,&j,&t));append_fail=100;before=mutations;CHECK(!setup_service_transaction_execute(t,0,1,&current,1000) && mutations==before && setup_service_transaction_recovery_required(t));append_fail=0;CHECK(!setup_service_transaction_execute(t,0,1,&current,1000) && mutations==before);CHECK(!setup_service_transaction_reconcile(t,&current,1000));finish(&j,&t);
    CHECK(start(&layout,&j,&t));append_fail=101;before=mutations;CHECK(!setup_service_transaction_execute(t,0,1,&current,1000) && mutations==before+1 && setup_service_transaction_recovery_required(t));append_fail=0;CHECK(setup_service_transaction_reconcile(t,&current,1000) && mutations==before+1);finish(&j,&t);
    for(unsigned mode=1;mode<=2;mode++){CHECK(start(&layout,&j,&t));post_done_fault=mode;before=mutations;CHECK(!setup_service_transaction_execute(t,0,1,&current,1000) && t->done[0]==1 && !t->pending_sequence && setup_service_transaction_recovery_required(t));post_done_fault=0;observe_fail=false;CHECK(!setup_service_transaction_execute(t,1,1,&current,1000) && mutations==before+1);CHECK(!setup_service_transaction_reconcile(t,&current,1000));finish(&j,&t);}
    CHECK(start(&layout,&j,&t));CHECK(setup_service_transaction_execute(t,0,1,&current,1000));CHECK(setup_service_transaction_execute(t,0,2,&current,1000));mutation_mode=2;before=mutations;CHECK(!setup_service_transaction_execute(t,0,3,&current,1000));mutation_mode=0;CHECK(setup_service_transaction_reconcile(t,&current,1000) && mutations==before+1);finish(&j,&t);
    CHECK(start(&layout,&j,&t));CHECK(setup_service_transaction_execute(t,0,1,&current,1000));CHECK(setup_service_transaction_execute(t,0,2,&current,1000));mutation_mode=4;CHECK(!setup_service_transaction_execute(t,0,3,&current,1000));CHECK(!setup_service_transaction_reconcile(t,&current,1000));finish(&j,&t);
    CHECK(start(&layout,&j,&t));mutation_mode=2;CHECK(!setup_service_transaction_execute(t,0,1,&current,1000));services[0].lost=true;before=mutations;CHECK(!setup_service_transaction_reconcile(t,&current,1000) && mutations==before);services[0].lost=false;current.generation++;CHECK(!setup_service_transaction_reconcile(t,&current,1000));finish(&j,&t);
    CHECK(start(&layout,&j,&t));operation.trusted=false;before=mutations;CHECK(!setup_service_transaction_execute(t,0,1,&current,1000) && mutations==before);operation.trusted=true;operation.images=false;CHECK(!setup_service_transaction_execute(t,0,1,&current,1000) && mutations==before);finish(&j,&t);
    CHECK(start(&layout,&j,&t));operation.hops=2;before=mutations;CHECK(!setup_service_transaction_execute(t,0,1,&current,1000) && mutations==before);finish(&j,&t);
    CHECK(start(&layout,&j,&t));window2();before=mutations;CHECK(!setup_service_transaction_execute(t,2,1,&current,1000) && mutations==before);finish(&j,&t);
    CHECK(start(&layout,&j,&t));BYTE config[12];l4_store_u64(config,operation.config);l4_store_u32(config+8,1);CHECK(l4_journal_append(j,21,config,12,NULL));before=mutations;CHECK(!setup_service_transaction_execute(t,0,1,&current,1000) && mutations==before && setup_service_transaction_recovery_required(t));CHECK(l4_journal_append(j,22,config,12,NULL));CHECK(setup_service_transaction_execute(t,0,1,&current,1000));finish(&j,&t);
    for(unsigned mode=0;mode<7;mode++){CHECK(start(&layout,&j,&t));Action a={0};a.action=1;a.service=0;a.window=1;a.generation=current.generation;a.deadline=current.deadline_utc;a.plan=operation.sequence;a.reference=operation.refs[0];a.pid=1000;a.birth=2000;BYTE bytes[ACTION_BYTES];encode(&a,bytes);
        if(mode==0)bytes[100]=1;else if(mode==1)l4_store_u64(bytes+48,999);else if(mode==2)l4_store_u64(bytes+40,999);else if(mode==3)l4_store_u32(bytes+12,2);else if(mode==4)l4_store_u64(bytes+72,9);else if(mode==5)l4_store_u32(bytes+16,4);else l4_store_u32(bytes+20,2);
        CHECK(l4_journal_append(j,100,bytes,sizeof(bytes),NULL));before=mutations;CHECK(!setup_service_transaction_execute(t,0,1,&current,1000) && mutations==before && setup_service_transaction_recovery_required(t));finish(&j,&t);}
    for(unsigned mode=0;mode<8;mode++){CHECK(start(&layout,&j,&t));mutation_mode=2;CHECK(!setup_service_transaction_execute(t,0,1,&current,1000));Action a=t->pending;a.intent=t->pending_sequence;BYTE bytes[ACTION_BYTES];encode(&a,bytes);
        if(mode==0)l4_store_u64(bytes+72,a.intent+1);else if(mode==1)l4_store_u64(bytes+64,a.birth+1);else if(mode==2)l4_store_u32(bytes+56,a.pid+1);else if(mode==3)l4_store_u64(bytes+24,a.generation+1);else if(mode==4)l4_store_u64(bytes+32,a.deadline+1);else if(mode==5)l4_store_u32(bytes+16,1);else if(mode==6)bytes[111]=1;
        CHECK(l4_journal_append(j,mode==7?100:101,bytes,sizeof(bytes),NULL));before=mutations;CHECK(!setup_service_transaction_reconcile(t,&current,1000) && mutations==before && setup_service_transaction_recovery_required(t));finish(&j,&t);}
    CHECK(start(&layout,&j,&t));CHECK(l4_journal_append(j,95,"terminal",8,NULL));before=mutations;CHECK(!setup_service_transaction_execute(t,0,1,&current,1000) && mutations==before);finish(&j,&t);
    for(unsigned mode=0;mode<7;mode++){CHECK(start(&layout,&j,&t));full_pair(t);window2();for(unsigned i=2;i<4;i++)for(unsigned a=1;a<=3;a++)CHECK(setup_service_transaction_execute(t,i,(SetupServiceAction)a,&current,1000));SetupRemoteCommit c=make_commit(j);
        if(mode==1)c.pids[0]++;else if(mode==2)c.operation_sha256[0]^=1;else if(mode==3)c.configs[0]++;else if(mode==4)c.commands[3][0]=L'x';else if(mode==5)c.suite_root[0]^=1;else if(mode==6)c.finished_utc=~0ull;
        CHECK(l4_journal_append(j,102,&c,sizeof(c),NULL));before=mutations;
        if(!mode){CHECK(setup_service_transaction_complete(t,j,&operation,pointers,&current,1000));CHECK(!setup_service_transaction_execute(t,3,3,&current,1000) && mutations==before);CHECK(setup_service_transaction_complete(t,j,&operation,pointers,&current,1000));CHECK(!setup_service_transaction_reconcile(t,&current,1000) && mutations==before);
            L4RemoteOutcome outcome={0};outcome.result.result=L4_REMOTE_OUTCOME_SUCCESS;strcpy_s(outcome.result.operation_id,37,current.owner);outcome.plan_sequence=operation.sequence;outcome.proof_sequence=j->sequence;memcpy(outcome.plan_sha256,c.operation_sha256,32);CHECK(l4_store_hash(&c,sizeof(c),NULL,0,outcome.proof_sha256));CHECK(l4_journal_append(j,103,&outcome,sizeof(outcome),NULL));CHECK(setup_service_transaction_complete(t,j,&operation,pointers,&current,1000));CHECK(!setup_service_transaction_execute(t,3,3,&current,1000) && mutations==before);
            BYTE clear[112]={0};memcpy(clear,"L4UPD01",8);memcpy(clear+8,current.owner,40);l4_store_u64(clear+56,current.generation+1);l4_store_u64(clear+64,operation.sequence);CHECK(l4_store_hash(clear,80,NULL,0,clear+80));CHECK(l4_journal_append(j,65,clear,sizeof(clear),NULL));CHECK(setup_service_transaction_complete(t,j,&operation,pointers,&current,1000));L4UpdateState saved=current;current.window=0;current.generation++;current.deadline_utc=0;CHECK(!setup_service_transaction_complete(t,j,&operation,pointers,&saved,1000));current=saved;CHECK(l4_journal_append(j,102,&c,sizeof(c),NULL));}
        CHECK(!setup_service_transaction_complete(t,j,&operation,pointers,&current,1000) && mutations==before);finish(&j,&t);}
    CHECK(start(&layout,&j,&t));full_pair(t);window2();for(unsigned i=2;i<4;i++)for(unsigned a=1;a<=3;a++)CHECK(setup_service_transaction_execute(t,i,(SetupServiceAction)a,&current,1000));
    for(int i=3;i>=0;i--)for(unsigned a=1;a<=3;a++)CHECK(setup_service_transaction_restore(t,(unsigned)i,(SetupServiceRestore)a,&current,1000));CHECK(setup_service_transaction_restored(t,&current,1000));before=mutations;CHECK(!setup_service_transaction_execute(t,0,1,&current,1000) && mutations==before);CHECK(!setup_service_transaction_complete(t,j,&operation,pointers,&current,1000));services[0].value.pid++;CHECK(!setup_service_transaction_restored(t,&current,1000));services[0].value.pid--;helper_wait=false;CHECK(!setup_service_transaction_restored(t,&current,1000));finish(&j,&t);
    for(unsigned failed=1;failed<=3;failed++){CHECK(start(&layout,&j,&t));for(unsigned a=1;a<failed;a++)CHECK(setup_service_transaction_execute(t,0,(SetupServiceAction)a,&current,1000));mutation_mode=2;CHECK(!setup_service_transaction_execute(t,0,(SetupServiceAction)failed,&current,1000));mutation_mode=0;before=mutations;CHECK(setup_service_transaction_abandon(t,&current,1000) && mutations==before);for(unsigned a=1;a<=3;a++)CHECK(setup_service_transaction_restore(t,0,(SetupServiceRestore)a,&current,1000));for(unsigned i=1;i<4;i++)CHECK(setup_service_transaction_restore(t,i,SETUP_SERVICE_RESTORE_VERIFY,&current,1000));CHECK(setup_service_transaction_restored(t,&current,1000));finish(&j,&t);}
    for(unsigned mode=0;mode<5;mode++){CHECK(start(&layout,&j,&t));if(mode==0)active_audit_ok=false;else if(mode==1)helper_wait=false;else if(mode==2)guard_open_ok=false;else if(mode==3)recovery.plan.worker_pid++;else recovery.plan.old_sha256[0]=1;ULONGLONG seq=j->sequence;before=mutations;CHECK(!setup_service_transaction_restore(t,0,SETUP_SERVICE_RESTORE_VERIFY,&current,1000) && mutations==before && j->sequence==seq);finish(&j,&t);}
    CHECK(start(&layout,&j,&t));full_pair(t);window2();before=mutations;mutation_mode=2;CHECK(!setup_service_transaction_restore(t,0,SETUP_SERVICE_RESTORE_STOP,&current,1000));mutation_mode=0;CHECK(setup_service_transaction_restore_reconcile(t,&current,1000) && mutations==before+1);CHECK(setup_service_transaction_restore(t,0,SETUP_SERVICE_RESTORE_SWITCH,&current,1000));CHECK(setup_service_transaction_restore(t,0,SETUP_SERVICE_RESTORE_START,&current,1000));finish(&j,&t);
    for(unsigned state=0;state<=2;state++){CHECK(start(&layout,&j,&t));BYTE cfg[12];l4_store_u64(cfg,operation.config);l4_store_u32(cfg+8,1);CHECK(l4_journal_append(j,21,cfg,12,NULL));config_state=state;before=mutations;ULONGLONG seq=j->sequence;
        if(!state)CHECK(!setup_service_transaction_abandon_config(t,&current,1000) && mutations==before && j->sequence==seq);
        else{CHECK(setup_service_transaction_abandon_config(t,&current,1000) && mutations==before && j->sequence==seq+1);l4_store_u32(cfg+8,0);CHECK(l4_journal_append(j,21,cfg,12,NULL));CHECK(l4_journal_append(j,22,cfg,12,NULL));for(unsigned i=0;i<4;i++)CHECK(setup_service_transaction_restore(t,i,SETUP_SERVICE_RESTORE_VERIFY,&current,1000));CHECK(setup_service_transaction_restored(t,&current,1000));}finish(&j,&t);}
    CHECK(remove_tree(root));printf("Service mutation transaction: %u passed, %u failed\n",checks-failures,failures);return failures?1:0;
}
