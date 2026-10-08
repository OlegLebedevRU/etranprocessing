/* Isolated composition: modeled network/clock/trust inputs, real ephemeral
 * Python signatures/CNG/root/descriptor/floor/journal. No owner candidate keys,
 * live services, Registry or actual installed state. Production has no overrides. */
#include "../src/update_metadata.h"
#include "../../l4common/worker_handoff.h"
#include "../../l4common/probe_ipc.h"
#include "../../l4common/recovery_store.h"
#include "../../l4common/journal_internal.h"
#include <bcrypt.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Stop/worker/communication ownership tests perform real CNG + signed multi-hop I/O.
 * Deadline/cancel tests below retain their explicit controlled budgets. */
#define STOP_SEMANTIC_BUDGET_MS 10000u
static unsigned check_count,failures,fetches;static wchar_t fixture[MAX_PATH];static char key_id[65];static BYTE public_key[412];static DWORD public_size;
static ULONGLONG fixture_utc=99;static bool fixture_trust;
static bool modeled_admission,verified_release;static unsigned prepared_calls,verify_calls;
static bool multi_hop,fail_second;
static bool fail_completion;
static volatile LONG prep_cancel;static bool cancel_on_fetch;
static bool cancel_after_complete,timeout_after_complete;static ULONGLONG clock_offset;
static ULONGLONG fixture_tick(void){return GetTickCount64()+clock_offset;}
static bool operation_catalog,physical_releases,scm_drift;static L4Layout installed_layout;
static bool identity_capture_ok=true;static unsigned preflight_calls;static bool preflight_ok=true;
#define CHECK(x) do{++check_count;if(!(x)){++failures;printf("FAIL acquisition %u: %s (error=%lu)\n",__LINE__,#x,GetLastError());}}while(0)
bool setup_readiness_preflight(L4Journal* j,const L4BootstrapPlan* source,const L4AccessActors* actors,const ULONGLONG* configs,unsigned count,const L4BootstrapChecks* checks,DWORD timeout){
    CHECK(j && source && actors && configs && count==3 && checks && timeout==1000);CHECK(wcsstr(source->commands[0],L"1.13.2"));++preflight_calls;return preflight_ok;
}
static L4UpdateState stop_marker;static bool stop_state_ok=true,stop_confirm_ok=true,stop_capture_drift;
static bool recovery_ok=true,recovery_second_failure;static unsigned recovery_calls;
static bool fixture_recovery(DWORD pid,const L4UpdateState* expected,DWORD timeout){
    ++recovery_calls;CHECK(pid==126 && expected && expected->window==1 && expected->generation==1 && expected->plan_sequence && timeout && timeout<=STOP_SEMANTIC_BUDGET_MS);
    if(!recovery_ok || (recovery_second_failure && recovery_calls==2)){SetLastError(ERROR_NOT_SUPPORTED);return false;}
    if(stop_marker.window)CHECK(!memcmp(expected,&stop_marker,sizeof(*expected)));
    return true;
}
#define l4_probe_recovery_call fixture_recovery
static unsigned stop_captures,stop_confirms;
static unsigned worker_fault,worker_checks;static ULONGLONG worker_sequence;
bool l4_worker_recheck(L4Journal* j,L4WorkerAdmission* out){
    memset(out,0,sizeof(*out));++worker_checks;if(worker_fault==1 || (worker_fault==7 && worker_checks==2)){SetLastError(ERROR_NOT_READY);return false;}
    SetupOperationPlan* p=NULL;if(!setup_update_load_operation(j,worker_sequence,&p))return false;const L4ServiceSwitch* s=setup_operation_switch(p,0,3);
    out->sequence=worker_sequence;out->generation=stop_marker.generation;out->deadline_utc=~0ull;out->supervisor_pid=126;out->supervisor_created.dwLowDateTime=103;
    out->start_type=s->before.start_type;out->old_size=s->before_size;out->new_size=s->size;wcscpy_s(out->before,2048,s->before.image_path);wcscpy_s(out->after,2048,s->after);
    memcpy(out->old_sha256,s->before_sha256,32);memcpy(out->new_sha256,s->sha256,32);setup_operation_free(p);
    if(worker_fault==2)out->sequence++;if(worker_fault==3)out->supervisor_pid++;if(worker_fault==4)out->after[1]^=1;if(worker_fault==5)out->old_sha256[0]^=1;
    if(worker_fault==6 && worker_checks==2)out->generation++;if(worker_fault==8)out->old_size++;if(worker_fault==9)out->start_type++;if(worker_fault==10)out->supervisor_created.dwLowDateTime++;
    return true;
}
static bool fixture_stop_state(const L4Layout* layout,L4UpdateState* state){(void)layout;if(!stop_state_ok){SetLastError(ERROR_INVALID_DATA);return false;}*state=stop_marker;return true;}
bool setup_readiness_capture(L4Journal* j,const L4BootstrapPlan* source,const L4AccessActors* actors,const ULONGLONG* configs,unsigned count,const L4BootstrapChecks* checks,DWORD timeout,L4ReadinessSnapshot* snapshot){
    CHECK(j && source && actors && configs && count==3 && checks && timeout && timeout<=STOP_SEMANTIC_BUDGET_MS && snapshot);++stop_captures;
    for(unsigned i=0;i<4;i++){snapshot->services[i].pid=123+i;snapshot->services[i].created.dwLowDateTime=100+i;}
    if(stop_capture_drift)stop_marker.generation++;return true;
}
bool setup_readiness_proxy_identity(const L4BootstrapPlan* source,const L4ReadinessSnapshot* original,DWORD timeout,char thumb[64]){
    CHECK(source && original && timeout && original->services[0].pid==123);if(!identity_capture_ok){SetLastError(ERROR_NOT_READY);return false;}strcpy_s(thumb,64,"1111111111111111111111111111111111111111");return true;
}
bool setup_readiness_quiescence(L4Journal* j,const L4BootstrapPlan* source,const L4AccessActors* actors,const ULONGLONG* configs,unsigned count,const L4BootstrapChecks* checks,const L4ReadinessSnapshot* original,const L4UpdateState* expected,DWORD timeout){
    CHECK(j && source && actors && configs && count==3 && checks && original && expected && timeout && timeout<=STOP_SEMANTIC_BUDGET_MS);++stop_confirms;
    for(unsigned i=0;i<4;i++)CHECK(original->services[i].pid==123+i && original->services[i].created.dwLowDateTime==100+i);
    CHECK(expected->window==1 && expected->generation==1 && expected->plan_sequence);return stop_confirm_ok;
}
#define l4_update_state_read fixture_stop_state
typedef struct{L4Journal* journal;SetupStopGate* gate;const L4AccessActors* actors;const L4BootstrapChecks* checks;HANDLE go;bool ok;} StopConfirmRace;
static DWORD WINAPI racing_confirm(void* context){
    StopConfirmRace* race=context;WaitForSingleObject(race->go,5000);
    race->ok=setup_update_confirm_stop(race->journal,race->gate,race->actors,race->checks,STOP_SEMANTIC_BUDGET_MS);return 0;
}
static BYTE* read_file(const wchar_t* name,DWORD* size){wchar_t path[MAX_PATH];swprintf_s(path,MAX_PATH,L"%ls\\%ls",fixture,name);FILE* file=NULL;
    CHECK(_wfopen_s(&file,path,L"rb")==0 && file);if(!file)return NULL;BYTE* bytes=(BYTE*)malloc(65536);CHECK(bytes);if(!bytes){fclose(file);return NULL;}
    *size=(DWORD)fread(bytes,1,65536,file);CHECK(!ferror(file) && feof(file));fclose(file);return bytes;
}
static bool fixture_metadata(WORD port,const char* version,const char* file,DWORD timeout,BYTE** bytes,DWORD* size){
    CHECK(port==18443 && timeout>=100 && timeout<=3000);++fetches;wchar_t name[80];
    if(!version){CHECK(!strcmp(file,"catalog.json") || !strcmp(file,"catalog.json.sig"));swprintf_s(name,_countof(name),L"%ls-%hs",operation_catalog?L"operation":multi_hop?L"multi":L"update",file);fixture_utc=151;}
    else{CHECK(!strcmp(version,"1.13.2") || !strcmp(version,"1.13.3") || !strcmp(version,"1.13.4"));
        if(!strcmp(version,"1.13.4") && fail_second){*bytes=NULL;*size=0;SetLastError(ERROR_TIMEOUT);return false;}
        swprintf_s(name,_countof(name),L"%ls%hs",!strcmp(version,"1.13.2")?L"installed\\":!strcmp(version,"1.13.4")?L"hop2\\":L"",file);}
    *bytes=read_file(name,size);if(cancel_on_fetch)InterlockedExchange(&prep_cancel,1);return *bytes!=NULL;
}
static void fixture_time(LPFILETIME time){ULARGE_INTEGER value;value.QuadPart=116444736000000000ULL+fixture_utc*10000000;time->dwLowDateTime=value.LowPart;time->dwHighDateTime=value.HighPart;}
static bool fixture_save(L4Journal* j,const void* bytes,DWORD size,const BYTE* sig,DWORD sig_size,ULONGLONG now,const char* current,const BYTE hash[32],const char* requested,const char* arch,const char* profile,ULONGLONG* sequence){
    CHECK(now==151);return l4_route_save_signed(j,bytes,size,sig,sig_size,public_key,public_size,key_id,now,current,hash,requested,arch,profile,sequence);
}
static bool fixture_owner(const L4RoutePlan* p){return p && fixture_trust;}
static bool fixture_root(const void* bytes,DWORD size,const BYTE* sig,DWORD sig_size,const L4CatalogRelease* release,const char* arch,SetupRootManifest** result){
    return setup_root_parse_signed(bytes,size,sig,sig_size,public_key,public_size,key_id,release,arch,result);
}
static bool fixture_descriptor(const SetupRootManifest* root,const void* bytes,DWORD size,const BYTE* sig,DWORD sig_size,const L4Layout* layout,SetupManifest** result){
    return setup_root_descriptor_signed(root,bytes,size,sig,sig_size,public_key,public_size,layout,result);
}
static bool fixture_load(L4Journal* j,ULONGLONG sequence,L4RoutePlan** result){return l4_route_load_signed(j,sequence,public_key,public_size,key_id,result);}
static bool fixture_decode(const void* bytes,DWORD size,L4RoutePlan** result){return l4_route_decode_signed(bytes,size,public_key,public_size,key_id,result);}
static bool fixture_prepare(const SetupManifest* manifest,const wchar_t* path){
    if(!modeled_admission)return setup_manifest_prepare(manifest,path);
    ++prepared_calls;CHECK(manifest && path && GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES);
    if(physical_releases){unsigned count;const L4ReleaseFile* files=setup_manifest_files(manifest,&count);
        return l4_release_unpack_publish(setup_manifest_layout(manifest),path,setup_manifest_archive_sha256(manifest),files,count);}return true;
}
static bool fixture_verify(const SetupManifest* manifest){
    ++verify_calls;CHECK(manifest);if(!verified_release){SetLastError(ERROR_ACCESS_DENIED);return false;}
    if(physical_releases){unsigned count;const L4ReleaseFile* files=setup_manifest_files(manifest,&count);return l4_release_verify(setup_manifest_layout(manifest),files,count);}return true;
}
static bool fixture_append(L4Journal* j,DWORD kind,const void* bytes,DWORD size,ULONGLONG* sequence){
    if((kind==L4_RECORD_PACKAGES_COMPLETE || kind==L4_RECORD_OPERATION_PLAN) && fail_completion){*sequence=0;SetLastError(ERROR_DISK_FULL);return false;}
    bool ok=l4_journal_append(j,kind,bytes,size,sequence);
    if(ok && kind==L4_RECORD_PACKAGES_COMPLETE){if(cancel_after_complete)InterlockedExchange(&prep_cancel,1);if(timeout_after_complete)clock_offset+=600000;}
    return ok;
}
bool l4_registry_fetch(WORD port,const char* version,const char* file,ULONGLONG limit,DWORD timeout,L4RegistrySink sink,void* context,ULONGLONG* received){
    CHECK(port==18443 && (!strcmp(version,"1.13.2") || !strcmp(version,"1.13.3") || !strcmp(version,"1.13.4")) && timeout>=100 && timeout<=3000);++fetches;
    wchar_t name[80];swprintf_s(name,_countof(name),L"%ls%hs",!strcmp(version,"1.13.2")?L"installed\\":!strcmp(version,"1.13.4")?L"hop2\\":L"",file);DWORD size;BYTE* bytes=read_file(name,&size);*received=0;
    if(!bytes)return false;CHECK(size==limit);bool ok=size==limit && sink(bytes,size,context);if(ok)*received=size;free(bytes);return ok;
}
static bool fixture_inventory(const wchar_t* service,L4ServiceInventory* result){
    const wchar_t* names[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};const wchar_t* exes[]={L"leo4proxy.exe",L"mosquitto.exe",L"l4con.exe",L"l4superv.exe"};
    for(unsigned i=0;i<4;i++)if(!wcscmp(service,names[i])){wchar_t path[MAX_PATH];CHECK(l4_layout_component(&installed_layout,components[i],exes[i],path));
        memset(result,0,sizeof(*result));result->installed=true;result->start_type=SERVICE_AUTO_START;wcscpy_s(result->account,256,L"LocalSystem");swprintf_s(result->image_path,2048,L"\"%ls\" --fixture-service",path);
        if(scm_drift)result->start_type=SERVICE_DEMAND_START;return true;}return false;
}
#define l4_service_inventory fixture_inventory
#define l4_registry_metadata fixture_metadata
#define GetSystemTimeAsFileTime fixture_time
#define l4_route_save_trusted fixture_save
#define l4_route_is_owner_trusted fixture_owner
#define setup_root_parse_trusted fixture_root
#define setup_root_descriptor_trusted fixture_descriptor
#define l4_route_load_trusted fixture_load
#define l4_route_decode_trusted fixture_decode
#define setup_manifest_prepare fixture_prepare
#define setup_manifest_verify fixture_verify
#define l4_journal_append fixture_append
static unsigned communication_fault,communication_publishes,job_verifies;static L4Journal* communication_journal;static L4RecoveryPlan communication_supervisor_plan;
static bool fixture_guard_open(const L4Layout* roots,const wchar_t* operation,DWORD timeout,L4RecoveryGuard** out){
    CHECK(roots && operation && timeout && timeout<=STOP_SEMANTIC_BUDGET_MS);L4WorkerAdmission a={0};unsigned saved_fault=worker_fault;worker_fault=0;
    bool ok=l4_worker_recheck(communication_journal,&a);worker_fault=saved_fault;if(!ok)return false;
    memset(&communication_supervisor_plan,0,sizeof(communication_supervisor_plan));L4RecoveryPlan* r=&communication_supervisor_plan;
    memcpy(&r->operation,communication_journal->header+8,16);r->sequence=worker_sequence;r->worker_pid=GetCurrentProcessId();FILETIME e,k,u;
    CHECK(GetProcessTimes(GetCurrentProcess(),&r->worker_created,&e,&k,&u));r->supervisor_pid=a.supervisor_pid;r->supervisor_created=a.supervisor_created;
    r->start_type=a.start_type;r->old_size=a.old_size;r->new_size=a.new_size;wcscpy_s(r->before,2048,a.before);wcscpy_s(r->after,2048,a.after);
    memcpy(r->old_sha256,a.old_sha256,32);memcpy(r->new_sha256,a.new_sha256,32);FILETIME now;fixture_time(&now);
    r->armed_utc=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;r->deadline_utc=r->armed_utc+18000000000ull;
    if(communication_fault==4)r->supervisor_pid++;*out=(L4RecoveryGuard*)(UINT_PTR)1;return true;
}
static const L4RecoveryPlan* fixture_guard_plan(const L4RecoveryGuard* guard){CHECK(guard);return &communication_supervisor_plan;}
static bool fixture_guard_action(L4RecoveryGuard* guard,ULONGLONG now,bool boot,L4RecoveryAction* action){
    CHECK(guard && now && !boot);*action=communication_fault==2?L4_RECOVERY_REQUIRED:L4_RECOVERY_WAIT;return true;
}
static void fixture_guard_release(L4RecoveryGuard* guard){CHECK(guard);}
static bool fixture_guard_relock(L4RecoveryGuard* guard,DWORD timeout){CHECK(guard && timeout && timeout<=STOP_SEMANTIC_BUDGET_MS);return true;}
static void fixture_guard_close(L4RecoveryGuard* guard){if(guard)CHECK(guard);}
static bool fixture_job_verify(L4WorkerJob* job,const L4RecoveryPlan* plan){
    CHECK(job && plan);job_verifies++;return communication_fault!=1 && !(communication_fault==5 && job_verifies==2);
}
static HANDLE fixture_job_process(const L4WorkerJob* job){CHECK(job);return GetCurrentProcess();}
static bool fixture_communication_publish(L4Journal* j,const L4CommunicationPlan* plan,HANDLE process){
    CHECK(j==communication_journal && process==GetCurrentProcess());BYTE* bytes=NULL;DWORD size=0;
    CHECK(l4_communication_plan_encode(&j->layout,plan,&bytes,&size));L4CommunicationPlan decoded;CHECK(l4_communication_plan_decode(&j->layout,bytes,size,&decoded));free(bytes);
    CHECK(plan->sequence==worker_sequence && plan->generation==1 && plan->supervisor_pid==126 && plan->worker_pid==GetCurrentProcessId());
    for(unsigned i=0;i<2;i++)CHECK(plan->switch_sequence[i]<plan->sequence && plan->config_sequence[i]<plan->sequence);
    CHECK(plan->con_pid==125 && plan->con_created.dwLowDateTime==102 && plan->con_sequence<plan->sequence && plan->con_switch && strlen(plan->thumbprint)==40);
    communication_publishes++;if(communication_fault==6){SetLastError(ERROR_DISK_FULL);return false;}return true;
}
#define l4_recovery_open fixture_guard_open
#define l4_recovery_plan fixture_guard_plan
#define l4_recovery_action fixture_guard_action
#define l4_recovery_release fixture_guard_release
#define l4_recovery_relock fixture_guard_relock
#define l4_recovery_close fixture_guard_close
#define l4_worker_job_verify fixture_job_verify
#define l4_worker_job_process fixture_job_process
#define l4_communication_plan_prepare fixture_communication_publish
#define GetTickCount64 fixture_tick
#include "../src/update_metadata.c"
#undef GetTickCount64
static void cleanup(const wchar_t* root){wchar_t pattern[MAX_PATH],child[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",root);WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW(pattern,&d);
    if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(d.cFileName,L".") || !wcscmp(d.cFileName,L".."))continue;swprintf_s(child,MAX_PATH,L"%ls\\%ls",root,d.cFileName);
        if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))cleanup(child);else RemoveDirectoryW(child);}else DeleteFileW(child);
    }while(FindNextFileW(h,&d));FindClose(h);}RemoveDirectoryW(root);
}
int wmain(int argc,wchar_t** argv){if(argc!=3)return 1;wcscpy_s(fixture,MAX_PATH,argv[1]);DWORD size=0;BYTE* bytes=read_file(L"fixture-public.blob",&size);
    CHECK(bytes && size==411);if(!bytes)return 1;memcpy(public_key,bytes,size);public_size=size;free(bytes);
    bytes=read_file(L"fixture-key-id.txt",&size);CHECK(bytes && size==64);if(!bytes)return 1;memcpy(key_id,bytes,size);key_id[size]=0;free(bytes);
    char arch[8];CHECK(WideCharToMultiByte(CP_UTF8,0,argv[2],-1,arch,sizeof(arch),NULL,NULL));
    wchar_t temporary[MAX_PATH],directory[MAX_PATH],programs[MAX_PATH],data[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temporary));
    swprintf_s(directory,MAX_PATH,L"%lsl4acquire-%lu-%llu",temporary,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(directory,NULL));
    swprintf_s(programs,MAX_PATH,L"%ls\\Programs",directory);swprintf_s(data,MAX_PATH,L"%ls\\Data",directory);L4Layout layout;CHECK(l4_layout_from_roots(&layout,programs,data,L"1.13.3"));CHECK(l4_layout_prepare(&layout));
    L4Journal* journal=NULL;CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000003",true,&journal));BYTE source[32];memset(source,0x22,32);ULONGLONG sequence=99;
    CHECK(!setup_update_acquire_route(NULL,18443,3000,"1.13.2",source,"latest",arch,"windows-10-x64",&sequence));CHECK(!sequence && !fetches);
    CHECK(setup_update_acquire_route(journal,18443,3000,"1.13.2",source,"latest",arch,"windows-10-x64",&sequence));CHECK(sequence==1 && fetches==2);
    L4RoutePlan* plan=NULL;CHECK(l4_route_load_signed(journal,sequence,public_key,public_size,key_id,&plan));CHECK(plan && l4_route_admitted_at(plan)==151);
    SetupRootManifest* root=NULL;SetupManifest* descriptor_plan=NULL;
    CHECK(!setup_update_acquire_root(plan,0,18443,3000,&root));CHECK(!root && fetches==2);
    fixture_trust=true;CHECK(setup_update_acquire_root(plan,0,18443,3000,&root));CHECK(root && fetches==4);
    CHECK(setup_update_acquire_descriptor(plan,0,root,&layout,18443,3000,&descriptor_plan));CHECK(descriptor_plan && fetches==6);setup_manifest_free(descriptor_plan);descriptor_plan=NULL;
    CHECK(!setup_update_acquire_descriptor(plan,1,root,&layout,18443,3000,&descriptor_plan));CHECK(!descriptor_plan && fetches==6);
    fixture_trust=false;CHECK(!setup_update_acquire_descriptor(plan,0,root,&layout,18443,3000,&descriptor_plan));CHECK(!descriptor_plan && fetches==6);fixture_trust=true;
    SetupRootManifest* alternate=NULL;bytes=read_file(L"mismatch-publisher.json",&size);DWORD sig_size;BYTE* sig=read_file(L"mismatch-publisher.sig",&sig_size);L4CatalogRelease release={0};strcpy_s(release.version,sizeof(release.version),"1.13.3");DWORD hash_size=32;
    CHECK(CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM,0,NULL,bytes,size,release.manifest_sha256,&hash_size));
    CHECK(setup_root_parse_signed(bytes,size,sig,sig_size,public_key,public_size,key_id,&release,arch,&alternate));free(bytes);free(sig);
    CHECK(!setup_update_acquire_descriptor(plan,0,alternate,&layout,18443,3000,&descriptor_plan));CHECK(!descriptor_plan && fetches==6);setup_root_free(alternate);
    /* Signed fixture metadata/ZIP still contains unsigned synthetic EXEs. The
     * actual production Authenticode gate must reject it before PF publication. */
    L4CachedPackage* package=NULL;
    CHECK(!setup_update_prepare_package(plan,0,root,&layout,18443,3000,&package));CHECK(!package && fetches==9);
    /* Synthetic MZ is not a full PE: providers can reject its subject form or
     * missing signature. Both are actual WinVerifyTrust admission failures. */
    CHECK(GetLastError()==(DWORD)TRUST_E_NOSIGNATURE || GetLastError()==(DWORD)TRUST_E_SUBJECT_FORM_UNKNOWN);
    CHECK(GetFileAttributesW(layout.release)==INVALID_FILE_ATTRIBUTES);
    setup_root_free(root);l4_route_free(plan);l4_journal_close(journal);journal=NULL;
    CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000003",false,&journal));
    CHECK(l4_route_load_signed(journal,sequence,public_key,public_size,key_id,&plan));CHECK(l4_route_steps(plan)->count==1 && !strcmp(l4_route_steps(plan)->releases[0].version,"1.13.3"));
    l4_route_free(plan);plan=NULL;
    ULONGLONG completed=99;SetupPreparedPlan* prepared=NULL;unsigned before=fetches;
    prep_cancel=1;CHECK(!setup_update_prepare_route_controlled(journal,sequence,18443,GetTickCount64()+3000,&prep_cancel,&completed));
    CHECK(!completed && GetLastError()==ERROR_CANCELLED && fetches==before && journal->sequence==sequence);
    prep_cancel=0;CHECK(!setup_update_prepare_route_controlled(journal,sequence,18443,GetTickCount64(),&prep_cancel,&completed));
    CHECK(!completed && fetches==before && journal->sequence==sequence);
    fixture_trust=true;cancel_on_fetch=true;
    CHECK(!setup_update_prepare_route_controlled(journal,sequence,18443,GetTickCount64()+3000,&prep_cancel,&completed));
    CHECK(!completed && GetLastError()==ERROR_CANCELLED && fetches==before+2 && journal->sequence==sequence);
    cancel_on_fetch=false;prep_cancel=0;before=fetches;
    fixture_trust=false;CHECK(!setup_update_prepare_route(journal,sequence,18443,3000,&completed));CHECK(!completed && fetches==before);fixture_trust=true;
    /* Inject ONLY successful EXE admission for durability/restart tests. The
     * actual production refusal above is unchanged; CNG/cache/journal remain real. */
    modeled_admission=true;verified_release=false;
    CHECK(!setup_update_prepare_route(journal,sequence,18443,3000,&completed));CHECK(!completed && fetches==before+5 && prepared_calls==1);
    CHECK(!setup_update_load_prepared(journal,3,&prepared));CHECK(!prepared);
    BYTE* package_record_bytes=NULL;DWORD package_size;CHECK(l4_store_find_record(journal,L4_RECORD_PREPARED_PACKAGE,2,&package_record_bytes,&package_size));
    l4_journal_close(journal);journal=NULL;fixture_utc=250;before=fetches;
    CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000003",false,&journal));
    CHECK(!setup_update_prepare_route(journal,sequence,18443,3000,&completed));CHECK(!completed && fetches==before && prepared_calls==1);
    verified_release=true;fail_completion=true;CHECK(!setup_update_prepare_route(journal,sequence,18443,3000,&completed));CHECK(!completed && GetLastError()==ERROR_DISK_FULL && fetches==before);
    CHECK(!setup_update_load_prepared(journal,3,&prepared));CHECK(!prepared);fail_completion=false;
    cancel_after_complete=true;
    CHECK(!setup_update_prepare_route_controlled(journal,sequence,18443,GetTickCount64()+3000,&prep_cancel,&completed));
    CHECK(!completed && GetLastError()==ERROR_CANCELLED && journal->sequence==3 && fetches==before);
    cancel_after_complete=false;prep_cancel=0;
    CHECK(setup_update_load_prepared(journal,3,&prepared));CHECK(prepared);setup_prepared_free(prepared);prepared=NULL;
    CHECK(setup_update_prepare_route(journal,sequence,18443,3000,&completed));CHECK(completed==3 && fetches==before && prepared_calls==1);
    CHECK(setup_update_load_prepared(journal,completed,&prepared));CHECK(prepared && setup_prepared_manifest(prepared,0) && !setup_prepared_manifest(prepared,1));
    CHECK(l4_route_admitted_at(setup_prepared_route(prepared))==151 && !strcmp(l4_route_requested(setup_prepared_route(prepared)),"latest"));
    setup_prepared_free(prepared);prepared=NULL;
    ULONGLONG again=0;CHECK(setup_update_prepare_route(journal,sequence,18443,3000,&again));CHECK(again==completed && fetches==before);
    /* Signature corruption in a separately valid journal frame must fail CNG. */
    l4_journal_close(journal);journal=NULL;
    CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000004",true,&journal));
    CHECK(setup_update_acquire_route(journal,18443,3000,"1.13.2",source,"latest",arch,"windows-10-x64",&again));
    package_record_bytes[28]^=1;CHECK(l4_journal_append(journal,L4_RECORD_PREPARED_PACKAGE,package_record_bytes,package_size,&again));
    CHECK(!setup_update_prepare_route(journal,1,18443,3000,&again));CHECK(!again);package_record_bytes[28]^=1;
    l4_journal_close(journal);journal=NULL;
    CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000003",false,&journal));
    /* A package record after completion is an ambiguous operation, not a retry. */
    CHECK(l4_journal_append(journal,L4_RECORD_PREPARED_PACKAGE,package_record_bytes,package_size,&again));
    CHECK(!setup_update_load_prepared(journal,completed,&prepared));CHECK(!prepared);free(package_record_bytes);
    l4_journal_close(journal);journal=NULL;multi_hop=true;fail_second=true;
    CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000005",true,&journal));
    CHECK(setup_update_acquire_route(journal,18443,3000,"1.13.2",source,"latest",arch,"windows-10-x64",&sequence));before=fetches;
    CHECK(!setup_update_prepare_route(journal,sequence,18443,3000,&completed));CHECK(!completed && fetches==before+6);
    CHECK(l4_store_find_record(journal,L4_RECORD_PREPARED_PACKAGE,2,&package_record_bytes,&package_size));free(package_record_bytes);package_record_bytes=NULL;
    l4_journal_close(journal);journal=NULL;fail_second=false;fixture_utc=250;before=fetches;
    CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000005",false,&journal));
    timeout_after_complete=true;CHECK(!setup_update_prepare_route(journal,sequence,18443,3000,&completed));
    CHECK(!completed && GetLastError()==ERROR_TIMEOUT && journal->sequence==4 && fetches==before+5);
    timeout_after_complete=false;clock_offset=0;
    CHECK(setup_update_load_prepared(journal,4,&prepared));CHECK(prepared);setup_prepared_free(prepared);prepared=NULL;
    CHECK(setup_update_prepare_route(journal,sequence,18443,3000,&completed));CHECK(completed==4 && fetches==before+5);
    CHECK(setup_update_load_prepared(journal,completed,&prepared));CHECK(l4_route_steps(setup_prepared_route(prepared))->count==2);
    CHECK(setup_prepared_manifest(prepared,0) && setup_prepared_manifest(prepared,1) && !setup_prepared_manifest(prepared,2));setup_prepared_free(prepared);prepared=NULL;
    CHECK(l4_store_find_record(journal,L4_RECORD_PREPARED_PACKAGE,2,&package_record_bytes,&package_size));
    wchar_t leaf[43],missing[MAX_PATH];for(unsigned i=0;i<42;i++)leaf[i]=package_record_bytes[package_size-42+i];leaf[42]=0;free(package_record_bytes);
    swprintf_s(missing,MAX_PATH,L"%ls\\%ls",layout.cache,leaf);CHECK(DeleteFileW(missing));before=fetches;
    CHECK(!setup_update_load_prepared(journal,completed,&prepared));CHECK(!prepared && fetches==before);
    CHECK(!setup_update_prepare_route(journal,sequence,18443,3000,&completed));CHECK(!completed && fetches==before);
    l4_journal_close(journal);journal=NULL;operation_catalog=true;physical_releases=true;
    /* EXE trust stays modeled, but all source/target files, ZIP hashes and switch
     * reconstruction are now actual isolated protected filesystem operations. */
    bytes=read_file(L"installed-root.sha256",&size);CHECK(size==32);memcpy(source,bytes,32);free(bytes);
    CHECK(l4_layout_from_roots(&installed_layout,programs,data,L"1.13.2"));
    L4CatalogRelease installed_release={0};strcpy_s(installed_release.version,64,"1.13.2");memcpy(installed_release.manifest_sha256,source,32);
    Document installed_root={0},installed_desc={0};CHECK(document(18443,"1.13.2","l4tools-release.json","l4tools-release.json.sig",3000,&installed_root));
    CHECK(fixture_root(installed_root.bytes,installed_root.size,installed_root.signature,installed_root.signature_size,&installed_release,arch,&root));
    char desc_name[40],sig_name[44];sprintf_s(desc_name,40,"l4tools-layout-%s.json",arch);sprintf_s(sig_name,44,"%s.sig",desc_name);
    CHECK(document(18443,"1.13.2",desc_name,sig_name,3000,&installed_desc));
    CHECK(fixture_descriptor(root,installed_desc.bytes,installed_desc.size,installed_desc.signature,installed_desc.signature_size,&installed_layout,&descriptor_plan));
    const SetupRootAsset* zip=setup_root_asset(root,2);CHECK(l4_package_download(&installed_layout,18443,"1.13.2",zip->name,zip->size,zip->sha256,3000,&package));
    CHECK(fixture_prepare(descriptor_plan,l4_package_path(package)));l4_package_close(package);package=NULL;setup_manifest_free(descriptor_plan);descriptor_plan=NULL;setup_root_free(root);root=NULL;
    close_document(&installed_root);close_document(&installed_desc);
    CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000006",true,&journal));
    CHECK(setup_update_acquire_route(journal,18443,3000,"1.13.2",source,"latest",arch,"windows-10-x64",&sequence));
    CHECK(setup_update_prepare_route(journal,sequence,18443,3000,&completed));ULONGLONG config=0;
    CHECK(l4_config_prepare(journal,L"l4superv.json","fixture",7,&config));
    ULONGLONG operation_configs[3]={config,0,0};wchar_t broker_dir[MAX_PATH];swprintf_s(broker_dir,MAX_PATH,L"%ls\\mosquitto",layout.config);
    CHECK(CreateDirectoryW(broker_dir,NULL) || GetLastError()==ERROR_ALREADY_EXISTS);
    const wchar_t* broker_names[]={L"mosquitto\\mosquitto.conf",L"mosquitto\\acl.conf"};
    for(unsigned i=0;i<2;i++){wchar_t path[MAX_PATH];swprintf_s(path,MAX_PATH,L"%ls\\%ls",layout.config,broker_names[i]);
        HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);CHECK(file!=INVALID_HANDLE_VALUE);DWORD written=0;CHECK(WriteFile(file,"old",3,&written,NULL));CloseHandle(file);
        CHECK(l4_config_prepare(journal,broker_names[i],"new",3,&operation_configs[i+1]));}

    ULONGLONG operation=99;before=fetches;fail_completion=true;
    CHECK(!setup_update_plan_operation(journal,completed,operation_configs,3,18443,3000,&operation));CHECK(!operation && fetches==before+4);
    fail_completion=false;before=fetches;CHECK(setup_update_plan_operation(journal,completed,operation_configs,3,18443,3000,&operation));CHECK(operation && fetches==before);
    SetupOperationPlan* op=NULL;CHECK(setup_update_load_operation(journal,operation,&op));
    CHECK(setup_operation_source(op) && setup_operation_switch(op,0,0) && setup_operation_switch(op,1,3) && !setup_operation_switch(op,2,0));
    CHECK(wcsstr(setup_operation_switch(op,0,0)->before.image_path,L"1.13.2") && wcsstr(setup_operation_switch(op,0,0)->after,L"1.13.3") &&
        wcsstr(setup_operation_switch(op,1,0)->after,L"1.13.4") && !wcscmp(setup_operation_switch(op,1,0)->before.image_path,setup_operation_switch(op,0,0)->after));
    CHECK(wcsstr(setup_operation_switch(op,1,0)->after,L"--fixture-service"));setup_operation_free(op);op=NULL;
    l4_journal_close(journal);journal=NULL;fixture_utc=250;
    CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000006",false,&journal));
    CHECK(setup_update_load_operation(journal,operation,&op));setup_operation_free(op);op=NULL;CHECK(fetches==before);
    L4AccessActors actors={0};L4BootstrapChecks checks={0};CHECK(setup_update_operation_preflight(journal,operation,&actors,&checks,1000));CHECK(preflight_calls==1 && fetches==before);
    preflight_ok=false;CHECK(!setup_update_operation_preflight(journal,operation,&actors,&checks,1000));CHECK(preflight_calls==2);preflight_ok=true;
    CHECK(!setup_update_candidate_probe(journal,operation,0,18443,59472,1000));
    CHECK(!setup_update_candidate_probe(journal,operation,0,59471,59471,1000));
    CHECK(!setup_update_candidate_probe(journal,operation,0,59471,59472,99));
    CHECK(!setup_update_candidate_probe(journal,operation,2,59471,59472,1000));
    /* Unknown synthetic source flag must fail closed before token capture;
     * never fall back to default certificate settings or operator token/EXE. */
    CHECK(!setup_update_candidate_probe(journal,operation,0,59471,59472,1000));
    CHECK(fetches==before && preflight_calls==2);
    ULONGLONG repeat=0;CHECK(setup_update_plan_operation(journal,completed,operation_configs,3,18443,3000,&repeat));CHECK(repeat==operation && fetches==before);
    CHECK(!setup_update_plan_operation(journal,completed,NULL,0,18443,3000,&repeat));CHECK(!repeat);
    scm_drift=true;CHECK(!setup_update_load_operation(journal,operation,&op));CHECK(!op);
    /* Pinned loader authenticates immutable metadata after switching, never
     * turns the changed current SCM into a new original source. */
    CHECK(setup_update_load_operation_pinned(journal,operation,&op));
    CHECK(op && setup_operation_sequence(op)==operation && setup_operation_binding(op,journal,operation));
    CHECK(!setup_operation_binding(op,journal,operation+1));
    CHECK(setup_operation_switch_reference(op,0,0)>0 && setup_operation_switch_reference(op,0,0)<operation);
    CHECK(setup_operation_switch(op,0,0)->before.start_type==SERVICE_AUTO_START);
    CHECK(setup_operation_switch_reference(op,0,4)==0);setup_operation_free(op);op=NULL;
    /* Historical snapshot loads with actual new owner lock; cannot pass the
     * mutable/live plan binding even though signed metadata/inventory passes. */
    l4_journal_close(journal);journal=NULL;L4Journal* snapshot_owner=NULL;L4JournalReader* snapshot=NULL;
    CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000007",true,&snapshot_owner));
    if(snapshot_owner){CHECK(l4_journal_reader_open(snapshot_owner,L"17730000-0000-4000-8000-000000000006",&snapshot));
        if(snapshot){CHECK(setup_update_load_operation_snapshot(snapshot,&layout,operation,&op));
            CHECK(op&&setup_operation_target_root(op,1)&&setup_operation_hops(op)==2&&setup_operation_sequence(op)==operation);
            CHECK(op&&!setup_operation_binding(op,snapshot_owner,operation));CHECK(fetches==before);
            setup_operation_free(op);op=NULL;CHECK(!setup_update_load_operation_snapshot(snapshot,&layout,operation+1,&op));CHECK(!op);l4_journal_reader_close(snapshot);}
        l4_journal_close(snapshot_owner);}
    CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000006",false,&journal));
    CHECK(!setup_update_operation_preflight(journal,operation,&actors,&checks,1000));CHECK(preflight_calls==2);scm_drift=false;
    CHECK(l4_config_apply(journal,config));CHECK(!setup_update_load_operation(journal,operation,&op));CHECK(!op);
    CHECK(setup_update_load_operation_pinned(journal,operation,&op));CHECK(setup_operation_binding(op,journal,operation));
    CHECK(setup_operation_verify_images(op));setup_operation_free(op);op=NULL;
    CHECK(l4_config_rollback(journal,config));
    CHECK(setup_update_load_operation(journal,operation,&op));setup_operation_free(op);op=NULL;
    SetupStopGate* gate=NULL;unsigned preserved_fetches=fetches;
    worker_sequence=operation;communication_journal=journal;
    FILETIME armed_time;fixture_time(&armed_time);ULONGLONG armed=((ULONGLONG)armed_time.dwHighDateTime<<32)|armed_time.dwLowDateTime;
    L4CommunicationBudget communication_budget={100,100,100,100,100,300000,100,301000};
    for(unsigned fault=0;fault<=9;fault++){identity_capture_ok=fault!=9;communication_fault=fault;communication_publishes=job_verifies=0;memset(&stop_marker,0,sizeof(stop_marker));scm_drift=fault==3;
        ULONGLONG until=armed+(fault==7?18000000000ull:fault==8?18000000000ull-(ULONGLONG)communication_budget.total_ms*10000:6000000000ull);
        CHECK(setup_update_prepare_communication(journal,operation,(L4WorkerJob*)(UINT_PTR)1,&actors,&checks,armed,until,&communication_budget,STOP_SEMANTIC_BUDGET_MS)==(fault==0));
        CHECK(communication_publishes==((fault==0 || fault==6)?1u:0u) && !stop_marker.window);scm_drift=false;
    }
    identity_capture_ok=true;communication_fault=0;stop_captures=0;worker_checks=0;
    worker_sequence=operation;
    for(unsigned fault=0;fault<=10;fault++){worker_fault=fault;worker_checks=0;memset(&stop_marker,0,sizeof(stop_marker));
        CHECK(setup_update_worker_capture_stop(journal,operation,&actors,&checks,STOP_SEMANTIC_BUDGET_MS,&gate)==(fault==0));CHECK(fault?gate==NULL:gate!=NULL);
        CHECK(!stop_marker.window && fetches==preserved_fetches);if(!fault)CHECK(worker_checks==2);setup_update_stop_gate_free(gate);gate=NULL;}
    worker_fault=0;stop_captures=0;memset(&stop_marker,0,sizeof(stop_marker));
    stop_state_ok=false;CHECK(!setup_update_capture_stop(journal,operation,&actors,&checks,STOP_SEMANTIC_BUDGET_MS,&gate));CHECK(!gate && !stop_captures);stop_state_ok=true;
    stop_capture_drift=true;CHECK(!setup_update_capture_stop(journal,operation,&actors,&checks,STOP_SEMANTIC_BUDGET_MS,&gate));CHECK(!gate);stop_capture_drift=false;memset(&stop_marker,0,sizeof(stop_marker));
    CHECK(setup_update_capture_stop(journal,operation,&actors,&checks,STOP_SEMANTIC_BUDGET_MS,&gate));CHECK(gate);
    recovery_ok=false;CHECK(!setup_update_watch_ready(journal,gate,~0ull,STOP_SEMANTIC_BUDGET_MS));CHECK(!stop_marker.window);
    recovery_ok=true;CHECK(setup_update_watch_ready(journal,gate,~0ull,STOP_SEMANTIC_BUDGET_MS));CHECK(!stop_marker.window);
    strcpy_s(stop_marker.owner,40,"17730000-0000-4000-8000-000000000006");stop_marker.generation=1;stop_marker.plan_sequence=operation;stop_marker.window=1;stop_marker.deadline_utc=~0ull;
    CHECK(setup_update_confirm_stop(journal,gate,&actors,&checks,STOP_SEMANTIC_BUDGET_MS));CHECK(stop_confirms==1 && fetches==preserved_fetches);
    CHECK(!setup_update_confirm_stop(journal,gate,&actors,&checks,STOP_SEMANTIC_BUDGET_MS));CHECK(stop_confirms==1);setup_update_stop_gate_free(gate);gate=NULL;
    for(unsigned fault=0;fault<7;fault++){
        memset(&stop_marker,0,sizeof(stop_marker));CHECK(setup_update_capture_stop(journal,operation,&actors,&checks,STOP_SEMANTIC_BUDGET_MS,&gate));
        strcpy_s(stop_marker.owner,40,"17730000-0000-4000-8000-000000000006");stop_marker.generation=1;stop_marker.plan_sequence=operation;stop_marker.window=1;stop_marker.deadline_utc=~0ull;
        if(!fault)stop_marker.owner[0]='2';else if(fault==1)stop_marker.plan_sequence++;else if(fault==2)stop_marker.generation++;else if(fault==3)stop_marker.window=2;else if(fault==4)stop_confirm_ok=false;else if(fault==5)recovery_ok=false;else recovery_second_failure=true;recovery_calls=0;
        unsigned attempts=stop_confirms;CHECK(!setup_update_confirm_stop(journal,gate,&actors,&checks,STOP_SEMANTIC_BUDGET_MS));
        CHECK(stop_confirms==attempts+((fault==4 || fault==6)?1:0));CHECK(!setup_update_confirm_stop(journal,gate,&actors,&checks,STOP_SEMANTIC_BUDGET_MS));
        setup_update_stop_gate_free(gate);gate=NULL;stop_confirm_ok=true;recovery_ok=true;recovery_second_failure=false;
    }
    memset(&stop_marker,0,sizeof(stop_marker));CHECK(setup_update_capture_stop(journal,operation,&actors,&checks,STOP_SEMANTIC_BUDGET_MS,&gate));
    stop_marker.window=1;stop_marker.generation=1;stop_marker.plan_sequence=operation;stop_marker.deadline_utc=~0ull;
    strcpy_s(stop_marker.owner,40,"17730000-0000-4000-8000-000000000006");scm_drift=true;
    unsigned attempts=stop_confirms;CHECK(!setup_update_confirm_stop(journal,gate,&actors,&checks,STOP_SEMANTIC_BUDGET_MS));CHECK(stop_confirms==attempts);scm_drift=false;
    setup_update_stop_gate_free(gate);gate=NULL;CHECK(fetches==preserved_fetches);
    puts("Signed operation stop gate: initial clear, drift, exact owner/plan/next generation/window, original epochs, one-shot failure/success, offline reload PASS");
    memset(&stop_marker,0,sizeof(stop_marker));CHECK(setup_update_capture_stop(journal,operation,&actors,&checks,STOP_SEMANTIC_BUDGET_MS,&gate));
    stop_marker.window=1;stop_marker.generation=1;stop_marker.plan_sequence=operation;stop_marker.deadline_utc=~0ull;strcpy_s(stop_marker.owner,40,"17730000-0000-4000-8000-000000000006");
    HANDLE go=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(go);
    StopConfirmRace races[2]={{journal,gate,&actors,&checks,go,false},{journal,gate,&actors,&checks,go,false}};
    HANDLE workers[2]={CreateThread(NULL,0,racing_confirm,&races[0],0,NULL),CreateThread(NULL,0,racing_confirm,&races[1],0,NULL)};
    CHECK(workers[0] && workers[1]);unsigned confirms_before=stop_confirms;
    SetEvent(go);DWORD joined=WaitForMultipleObjects(2,workers,TRUE,30000);CHECK(joined==WAIT_OBJECT_0);
    CHECK(races[0].ok!=races[1].ok && stop_confirms==confirms_before+1);
    CloseHandle(workers[0]);CloseHandle(workers[1]);CloseHandle(go);setup_update_stop_gate_free(gate);gate=NULL;
    puts("Stop gate: two concurrent confirmation callers, exactly one consumes original gate PASS");
    memset(&stop_marker,0,sizeof(stop_marker));CHECK(l4_journal_append(journal,68,"fixture",7,NULL));
    communication_publishes=job_verifies=0;CHECK(!setup_update_prepare_communication(journal,operation,(L4WorkerJob*)(UINT_PTR)1,&actors,&checks,armed,armed+6000000000ull,&communication_budget,STOP_SEMANTIC_BUDGET_MS));
    CHECK(!communication_publishes && !job_verifies);
    BYTE* operation_record=NULL;DWORD operation_size=0;CHECK(l4_store_find_record(journal,L4_RECORD_OPERATION_PLAN,operation,&operation_record,&operation_size));
    ULONGLONG source_sequence=l4_store_get64(operation_record+12);free(operation_record);
    BYTE* source_record=NULL;DWORD source_size;CHECK(l4_store_find_record(journal,L4_RECORD_INSTALLED_SOURCE,source_sequence,&source_record,&source_size));
    CHECK(l4_journal_append(journal,L4_RECORD_INSTALLED_SOURCE,source_record,source_size,&repeat));free(source_record);
    CHECK(!setup_update_load_operation(journal,operation,&op));CHECK(!op);
    l4_journal_close(journal);cleanup(directory);CHECK(GetFileAttributesW(directory)==INVALID_FILE_ATTRIBUTES);
    printf("Metadata acquisition composition: %u checks, %u failures; modeled transport/trust, real CNG/journal, no services\n",check_count,failures);return failures?1:0;
}
