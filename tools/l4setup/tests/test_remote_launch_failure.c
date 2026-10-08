/* Actual journal/files/codec; status/route/controller/worker/state are modeled.
 * Full strict snapshot/launch history is verified in common status tests. */
#include "../src/remote_launch_failure_report.c"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks,failures,authorizations;static int fault;static L4RemoteStatus observed;static L4Journal* owner;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL line%d error%lu: %s\n",__LINE__,GetLastError(),#x);}}while(0)
bool l4_journal_reader_open_live(const L4Layout* l,const wchar_t* id,L4JournalReader** r){(void)l;(void)id;if(fault==1)return fail(ERROR_CRC);*r=(L4JournalReader*)1;return true;}
void l4_journal_reader_close(L4JournalReader* r){(void)r;}
bool l4_remote_status_snapshot(const L4JournalReader* r,const L4Layout* l,L4RemoteStatus* s){(void)r;(void)l;*s=observed;
    if(owner->sequence>1){BYTE* b=NULL;DWORD size=0;if(!l4_store_find_record(owner,95,owner->sequence,&b,&size))return false;
        bool ok=l4_remote_launch_failure_decode(b,size,&s->launch_failure);free(b);if(!ok)return false;s->has_launch_failure=true;}
    return true;
}
bool l4_remote_host_self(const L4Layout* l,const wchar_t* id,const char* arch){(void)l;(void)id;(void)arch;authorizations++;return fault==2?fail(ERROR_ACCESS_DENIED):true;}
bool l4_worker_recheck(L4Journal* j,L4WorkerAdmission* a){(void)j;authorizations++;memset(a,0,sizeof(*a));a->sequence=fault==4?8:7;return fault==3?fail(ERROR_ACCESS_DENIED):true;}
bool l4_update_state_read(const L4Layout* l,L4UpdateState* s){(void)l;memset(s,0,sizeof(*s));s->window=fault==5?1:0;return fault==6?fail(ERROR_CRC):true;}
struct L4RoutePlan{int unused;};static L4RoutePlan route;
bool l4_route_load_trusted(L4Journal* j,ULONGLONG sequence,L4RoutePlan** out){(void)j;(void)sequence;*out=&route;return fault==7?fail(ERROR_CRC):true;}
bool l4_route_is_owner_trusted(const L4RoutePlan* p){(void)p;return fault!=8;}
const L4CatalogRoute* l4_route_steps(const L4RoutePlan* p){(void)p;static L4CatalogRoute steps;memset(&steps,0,sizeof(steps));steps.count=fault==9?0:1;strcpy_s(steps.releases[0].version,64,"1.13.7");return &steps;}
void l4_route_free(L4RoutePlan* p){(void)p;}
static void remove_owner(void){wchar_t dir[MAX_PATH],path[MAX_PATH];wcscpy_s(dir,MAX_PATH,owner->directory);l4_journal_close(owner);owner=NULL;
    swprintf_s(path,MAX_PATH,L"%ls\\journal.bin",dir);CHECK(DeleteFileW(path));CHECK(RemoveDirectoryW(dir));}
static void start(const L4Layout* l,unsigned i){wchar_t id[37];swprintf_s(id,37,L"135a4120-9ba6-4f6c-8cac-%012u",i);CHECK(l4_journal_open(l,id,true,&owner));
    CHECK(l4_journal_append(owner,64,"fixture",7,NULL));memset(&observed,0,sizeof(observed));observed.has_host=true;observed.plan_sequence=7;observed.route_sequence=3;
    observed.recorded_phase=L4_REMOTE_RECORDED_WORKER_STARTING;observed.host.pid=GetCurrentProcessId();FILETIME c,e,k,u;CHECK(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u));
    observed.host.birth=((ULONGLONG)c.dwHighDateTime<<32)|c.dwLowDateTime;strcpy_s(observed.host.arch,8,"x86");strcpy_s(observed.host.source_version,32,"1.13.6");
    observed.request.target=1;strcpy_s(observed.request.version,32,"latest");GetSystemTimeAsFileTime(&c);observed.request.accepted_utc=((ULONGLONG)c.dwHighDateTime<<32)|c.dwLowDateTime;
    CHECK(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,id,-1,observed.operation_id,37,NULL,NULL));
}
int main(void){wchar_t temp[MAX_PATH],root[MAX_PATH],pf[MAX_PATH],pd[MAX_PATH],path[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));
    swprintf_s(root,MAX_PATH,L"%lsL4LaunchFailure-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(root,NULL));
    swprintf_s(pf,MAX_PATH,L"%ls\\PF",root);swprintf_s(pd,MAX_PATH,L"%ls\\PD",root);L4Layout l;CHECK(l4_layout_from_roots(&l,pf,pd,L"1.13.6") && l4_layout_prepare(&l));
    L4RemoteLaunchFailure result;start(&l,1);CHECK(setup_remote_launch_failure_finish(owner,ERROR_TIMEOUT,5,ERROR_ACCESS_DENIED,&result));
    CHECK(owner->sequence==2 && result.plan_sequence==7 && result.stage==5 && result.cleanup_error==ERROR_ACCESS_DENIED && result.result.error==ERROR_TIMEOUT);
    unsigned count=authorizations;ULONGLONG finished=result.result.finished_at;CHECK(setup_remote_launch_failure_finish(owner,ERROR_TIMEOUT,5,ERROR_ACCESS_DENIED,&result));
    CHECK(owner->sequence==2 && authorizations==count && result.result.finished_at==finished);
    CHECK(!setup_remote_launch_failure_finish(owner,ERROR_TIMEOUT,6,ERROR_ACCESS_DENIED,&result) && GetLastError()==ERROR_ALREADY_EXISTS);
    CHECK(!setup_remote_launch_failure_finish(owner,ERROR_TIMEOUT,5,0,&result) && GetLastError()==ERROR_ALREADY_EXISTS);remove_owner();
    for(fault=1;fault<=9;fault++){start(&l,10+fault);if(fault==3 || fault==4){observed.host.pid++;observed.recorded_phase=L4_REMOTE_RECORDED_WORKER;}
        CHECK(!setup_remote_launch_failure_finish(owner,ERROR_TIMEOUT,8,0,&result) && !result.result.operation_id[0] && owner->sequence==1);remove_owner();}fault=0;
    start(&l,30);observed.host.birth++;CHECK(!setup_remote_launch_failure_finish(owner,ERROR_TIMEOUT,8,0,&result));remove_owner();
    start(&l,31);observed.host.pid++;observed.recorded_phase=L4_REMOTE_RECORDED_HANDOFF;CHECK(!setup_remote_launch_failure_finish(owner,ERROR_TIMEOUT,8,0,&result));remove_owner();
    start(&l,32);observed.host.pid++;observed.recorded_phase=L4_REMOTE_RECORDED_WORKER;CHECK(setup_remote_launch_failure_finish(owner,ERROR_CANCELLED,8,0,&result) && result.result.result==L4_REMOTE_RESULT_CANCELLED);remove_owner();
    start(&l,33);observed.recorded_phase=L4_REMOTE_RECORDED_PLAN;CHECK(!setup_remote_launch_failure_finish(owner,ERROR_TIMEOUT,8,0,&result));remove_owner();
    start(&l,34);observed.has_result=true;CHECK(!setup_remote_launch_failure_finish(owner,ERROR_TIMEOUT,8,0,&result));remove_owner();
    CHECK(!setup_remote_launch_failure_finish(NULL,ERROR_TIMEOUT,8,0,&result));CHECK(!setup_remote_launch_failure_finish(NULL,0,0,0,NULL));
    swprintf_s(path,MAX_PATH,L"%ls\\deployment.lock",l.operations);CHECK(DeleteFileW(path));CHECK(RemoveDirectoryW(l.operations));CHECK(RemoveDirectoryW(l.cache));CHECK(RemoveDirectoryW(l.staging));
    swprintf_s(path,MAX_PATH,L"%ls\\update",l.data);CHECK(RemoveDirectoryW(path));CHECK(RemoveDirectoryW(l.config));CHECK(RemoveDirectoryW(l.state));CHECK(RemoveDirectoryW(l.logs));
    swprintf_s(path,MAX_PATH,L"%ls\\releases",l.binaries);CHECK(RemoveDirectoryW(path));CHECK(RemoveDirectoryW(l.launchers));CHECK(RemoveDirectoryW(l.binaries));CHECK(RemoveDirectoryW(l.data));CHECK(RemoveDirectoryW(root));
    printf("Remote launch failure: %u checks, %u failures; actual private journal/95/codec, modeled authority/state/route\n",checks,failures);return failures?1:0;
}
