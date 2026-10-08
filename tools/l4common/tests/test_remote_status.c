#include "../remote_status.h"
#include "../route_plan.h"
#include "../journal_internal.h"
#include "../recovery_plan.h"
#include <sddl.h>
#include "../journal_reader.c" /* Actual snapshot storage; native SYSTEM gate is never bypassed in production. */
#include <stdio.h>
#include <string.h>
static unsigned checks,failures;static int route_fault;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL line%d err%lu: %s\n",__LINE__,GetLastError(),#x);}}while(0)
struct L4RoutePlan{int unused;};static L4RoutePlan modeled_route;
bool l4_route_decode_trusted(const void* bytes,DWORD size,L4RoutePlan** result){(void)bytes;(void)size;
    if(route_fault==1){SetLastError(ERROR_CRC);return false;}*result=&modeled_route;return true;}
bool l4_route_is_owner_trusted(const L4RoutePlan* r){(void)r;return route_fault!=2;}
bool l4_route_source(const L4RoutePlan* r,L4CatalogRelease* source){(void)r;memset(source,0,sizeof(*source));strcpy_s(source->version,64,route_fault==3?"1.13.5":"1.13.6");return true;}
const char* l4_route_requested(const L4RoutePlan* r){(void)r;return route_fault==4?"1.13.8":"latest";}
const char* l4_route_arch(const L4RoutePlan* r){(void)r;return route_fault==5?"x64":"x86";}
const L4CatalogRoute* l4_route_steps(const L4RoutePlan* r){(void)r;static L4CatalogRoute steps;memset(&steps,0,sizeof(steps));steps.count=1;strcpy_s(steps.releases[0].version,64,"1.13.7");return &steps;}
void l4_route_free(L4RoutePlan* r){(void)r;}
static bool read(L4Journal* j,const L4Layout* roots,L4RemoteStatus* status){
    const wchar_t* operation=wcsrchr(j->directory,L'\\')+1;GUID id;L4JournalReader* r=NULL;
    if(!uuid(operation,&id) || !snapshot(roots,operation,&id,INVALID_HANDLE_VALUE,true,&r))return false;
    CHECK(l4_journal_reader_codec_view(r)==NULL);
    bool ok=l4_remote_status_snapshot(r,roots,status);DWORD error=GetLastError();l4_journal_reader_close(r);SetLastError(error);return ok;
}
static bool ack(L4Journal* j,const L4RemoteRequest* request,bool malformed){
    BYTE b[112]={0};memcpy(b,"L4RHST01",8);l4_store_u32(b+8,1);l4_store_u32(b+12,17);l4_store_u64(b+16,100);
    l4_store_u64(b+24,20);memset(b+32,1,32);l4_store_u64(b+64,request->accepted_utc);strcpy_s((char*)b+72,32,"1.13.6");strcpy_s((char*)b+104,8,"x86");if(malformed)b[104]='z';
    return l4_journal_append(j,L4_RECORD_REMOTE_HOST_ACK,b,sizeof(b),NULL);
}
static bool start(const L4Layout* layout,unsigned number,L4Journal** j,L4RemoteRequest* request){wchar_t id[37];swprintf_s(id,37,L"17730000-0000-4000-8000-%012u",number);
    return l4_journal_open(layout,id,true,j) && l4_remote_request_save(*j,L4_REMOTE_SUITE,"latest",request);}
static void remove_journal(L4Journal** j){wchar_t directory[MAX_PATH],file[MAX_PATH];wcscpy_s(directory,MAX_PATH,(*j)->directory);l4_journal_close(*j);*j=NULL;
    swprintf_s(file,MAX_PATH,L"%ls\\journal.bin",directory);CHECK(DeleteFileW(file));CHECK(RemoveDirectoryW(directory));}
static bool terminal(L4Journal* j,const L4RemoteRequest* request,bool resolved,unsigned mismatch){
    L4RemoteResult value={0};value.target=1;value.result=L4_REMOTE_RESULT_FAILED;value.error=ERROR_TIMEOUT;
    CHECK(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wcsrchr(j->directory,L'\\')+1,-1,value.operation_id,37,NULL,NULL));
    value.started_at=request->accepted_utc;value.finished_at=value.started_at+100000000;
    strcpy_s(value.requested_version,32,"latest");strcpy_s(value.previous_version,32,"1.13.6");if(resolved)strcpy_s(value.resolved_version,32,"1.13.7");
    if(mismatch==1)value.operation_id[0]='2';if(mismatch==2)value.started_at++;
    if(mismatch==3)strcpy_s(value.previous_version,32,"1.13.5");if(mismatch==4)strcpy_s(value.requested_version,32,"1.13.8");
    if(mismatch==5)strcpy_s(value.resolved_version,32,"1.13.8");BYTE b[L4_REMOTE_RESULT_BYTES];
    return l4_remote_result_encode(&value,b) && l4_journal_append(j,L4_RECORD_REMOTE_PREPARATION_RESULT,b,sizeof(b),NULL);
}
static bool recovery(L4Journal* j,ULONGLONG sequence,bool foreign){
    L4RecoveryPlan p={0};memcpy(&p.operation,j->header+8,16);if(foreign)p.operation.Data1++;
    p.sequence=sequence;p.worker_pid=100;p.worker_created.dwLowDateTime=1;p.supervisor_pid=101;p.supervisor_created.dwLowDateTime=2;
    FILETIME t;GetSystemTimeAsFileTime(&t);p.armed_utc=((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;p.deadline_utc=p.armed_utc+600000000ull;
    p.recovery_ms=1000;p.start_type=SERVICE_AUTO_START;p.old_size=1;p.new_size=2;memset(p.old_sha256,1,32);memset(p.new_sha256,2,32);
    p.old_exists=true;p.old_config=(BYTE*)"old";p.old_config_size=3;p.new_config=(BYTE*)"new";p.new_config_size=3;
    L4Layout next;wchar_t image[MAX_PATH];if(!l4_layout_component(&j->layout,L"l4superv",L"l4superv.exe",image))return false;
    swprintf_s(p.before,2048,L"\"%ls\" --service",image);
    if(!l4_layout_from_roots(&next,j->layout.binaries,j->layout.data,L"1.13.7") || !l4_layout_component(&next,L"l4superv",L"l4superv.exe",image))return false;
    swprintf_s(p.after,2048,L"\"%ls\" --service",image);PSECURITY_DESCRIPTOR sd=NULL;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FR;;;BU)",SDDL_REVISION_1,&sd,NULL))return false;
    p.config_sd=sd;p.config_sd_size=GetSecurityDescriptorLength(sd);BYTE* b=NULL;DWORD size=0;
    bool ok=l4_recovery_encode(&j->layout,&p,&b,&size) && l4_journal_append(j,66,b,size,NULL);free(b);LocalFree(sd);return ok;
}
static bool planned(L4Journal* j){return l4_journal_append(j,60,"x",1,NULL) && l4_journal_append(j,62,"x",1,NULL) && l4_journal_append(j,64,"x",1,NULL);}
static bool failed_launch(L4Journal* j,const L4RemoteRequest* request,unsigned mismatch){
    L4RemoteLaunchFailure f={0};f.plan_sequence=mismatch==1?4:5;f.stage=5;f.cleanup_error=ERROR_TIMEOUT;
    L4RemoteResult* r=&f.result;r->target=1;r->result=L4_REMOTE_RESULT_FAILED;r->error=ERROR_ACCESS_DENIED;
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wcsrchr(j->directory,L'\\')+1,-1,r->operation_id,37,NULL,NULL))return false;
    r->started_at=request->accepted_utc;r->finished_at=r->started_at+100000000ull;
    strcpy_s(r->requested_version,32,"latest");strcpy_s(r->previous_version,32,"1.13.6");strcpy_s(r->resolved_version,32,mismatch==2?"1.13.8":"1.13.7");
    BYTE b[L4_REMOTE_LAUNCH_FAILURE_BYTES];CHECK(l4_remote_launch_failure_encode(&f,b));L4RemoteLaunchFailure decoded;
    CHECK(l4_remote_launch_failure_decode(b,sizeof(b),&decoded));CHECK(decoded.cleanup_error==ERROR_TIMEOUT && decoded.stage==5);
    for(unsigned i=224;i<sizeof(b);i++){b[i]=1;CHECK(!l4_remote_launch_failure_decode(b,sizeof(b),&decoded) && !decoded.result.operation_id[0]);b[i]=0;}
    return l4_journal_append(j,95,b,sizeof(b),NULL);
}
static int native(void){
    /* Explicit isolated SYSTEM acceptance on the authorized local stand only.
     * Real KnownFolders/private storage/live reader; modeled controller outcome.
     * Never create/prepare installed roots, change SCM/marker or delete shared lock. */
    L4Layout original,current;CHECK(l4_layout_resolve(&original,L"1.13.6"));CHECK(l4_layout_resolve(&current,L"1.13.9"));
    GUID id;wchar_t text[40],operation[37],directory[MAX_PATH],file[MAX_PATH];CHECK(SUCCEEDED(CoCreateGuid(&id)));
    CHECK(StringFromGUID2(&id,text,40)==39);for(unsigned i=0;i<38;i++)if(text[i]>=L'A' && text[i]<=L'F')text[i]+=L'a'-L'A';memcpy(operation,text+1,36*sizeof(wchar_t));operation[36]=0;
    swprintf_s(directory,MAX_PATH,L"%ls\\%ls",original.operations,operation);CHECK(GetFileAttributesW(directory)==INVALID_FILE_ATTRIBUTES);
    L4Journal* j=NULL;CHECK(l4_journal_open(&original,operation,true,&j));if(!j)return 1;
    L4RemoteRequest request;CHECK(l4_remote_request_save(j,L4_REMOTE_SUITE,"latest",&request));CHECK(ack(j,&request,false));CHECK(terminal(j,&request,false,0));
    l4_journal_close(j);j=NULL;L4RemoteStatus status;
    CHECK(l4_remote_status_observe(&current,operation,&status) && status.has_host && status.has_result &&
        status.recorded_phase==L4_REMOTE_RECORDED_FINISHED && status.result.error==ERROR_TIMEOUT &&
        !strcmp(status.host.source_version,"1.13.6") && status.last_sequence==3);
    swprintf_s(file,MAX_PATH,L"%ls\\journal.bin",directory);CHECK(DeleteFileW(file));CHECK(RemoveDirectoryW(directory));
    CHECK(GetFileAttributesW(directory)==INVALID_FILE_ATTRIBUTES);
    printf("Native SYSTEM status: %u checks, %u failures; KnownFolders/private snapshot actual, controller outcome modeled, no SCM/MQTT\n",checks,failures);return failures?1:0;
}
int wmain(int argc,wchar_t** argv){
    if(argc==2 && !wcscmp(argv[1],L"--native"))return native();
    wchar_t temp[MAX_PATH],root[MAX_PATH],pf[MAX_PATH],pd[MAX_PATH],path[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));
    swprintf_s(root,MAX_PATH,L"%lsL4RemoteStatus-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(root,NULL));
    swprintf_s(pf,MAX_PATH,L"%ls\\PF",root);swprintf_s(pd,MAX_PATH,L"%ls\\PD",root);L4Layout original,current;
    CHECK(l4_layout_from_roots(&original,pf,pd,L"1.13.6") && l4_layout_prepare(&original));CHECK(l4_layout_from_roots(&current,pf,pd,L"1.13.9"));
    L4Journal* j=NULL;L4RemoteRequest request;L4RemoteStatus status;
    CHECK(start(&original,1,&j,&request));CHECK(read(j,&current,&status) && status.recorded_phase==L4_REMOTE_RECORDED_REQUEST && !status.has_host && !status.has_result);
    CHECK(ack(j,&request,false));CHECK(read(j,&current,&status) && status.has_host && !strcmp(status.host.source_version,"1.13.6") && status.recorded_phase==L4_REMOTE_RECORDED_CONTROLLER);
    CHECK(l4_journal_append(j,60,"x",1,NULL));CHECK(read(j,&current,&status) && status.route_sequence==3 && status.recorded_phase==L4_REMOTE_RECORDED_ROUTE);
    CHECK(l4_journal_append(j,61,"x",1,NULL));CHECK(read(j,&current,&status) && status.recorded_phase==L4_REMOTE_RECORDED_PACKAGES_PROGRESS);
    CHECK(l4_journal_append(j,62,"x",1,NULL));CHECK(read(j,&current,&status) && status.packages_sequence==5 && status.recorded_phase==L4_REMOTE_RECORDED_PACKAGES);
    CHECK(l4_journal_append(j,20,"x",1,NULL));CHECK(read(j,&current,&status) && status.recorded_phase==L4_REMOTE_RECORDED_PLANNING);
    CHECK(l4_journal_append(j,64,"x",1,NULL));CHECK(read(j,&current,&status) && status.plan_sequence==7 && status.recorded_phase==L4_REMOTE_RECORDED_PLAN);
    CHECK(terminal(j,&request,true,0));CHECK(read(j,&current,&status) && status.has_result && status.result.error==ERROR_TIMEOUT && status.recorded_phase==L4_REMOTE_RECORDED_FINISHED);
    L4RemoteStatus saved=status;CHECK(read(j,&current,&status) && !memcmp(&status,&saved,sizeof(status)) && j->sequence==8);
    CHECK(l4_journal_append(j,94,"x",1,NULL));CHECK(!read(j,&current,&status) && GetLastError()==ERROR_INVALID_STATE && !status.operation_id[0]);remove_journal(&j);
    for(unsigned mismatch=0;mismatch<=5;mismatch++){CHECK(start(&original,10+mismatch,&j,&request));CHECK(ack(j,&request,false));CHECK(terminal(j,&request,false,mismatch));
        if(!mismatch)CHECK(read(j,&current,&status) && status.has_result && !status.result.resolved_version[0]);
        else CHECK(!read(j,&current,&status) && GetLastError()==ERROR_REVISION_MISMATCH && !status.operation_id[0]);remove_journal(&j);}
    for(int fault=1;fault<=5;fault++){route_fault=fault;CHECK(start(&original,30+fault,&j,&request));CHECK(ack(j,&request,false));CHECK(l4_journal_append(j,60,"x",1,NULL));CHECK(!read(j,&current,&status) && !status.operation_id[0]);remove_journal(&j);}route_fault=0;
    CHECK(start(&original,40,&j,&request));CHECK(ack(j,&request,true));CHECK(!read(j,&current,&status) && GetLastError()==ERROR_INVALID_DATA);remove_journal(&j);
    const DWORD unsupported[]={21,22,65};
    for(unsigned i=0;i<sizeof(unsupported)/sizeof(unsupported[0]);i++){CHECK(start(&original,50+i,&j,&request));CHECK(ack(j,&request,false));CHECK(l4_journal_append(j,unsupported[i],"x",1,NULL));CHECK(!read(j,&current,&status) && GetLastError()==ERROR_NOT_SUPPORTED);remove_journal(&j);}
    for(unsigned mismatch=0;mismatch<3;mismatch++){
        CHECK(start(&original,60+mismatch,&j,&request));CHECK(ack(j,&request,false));CHECK(planned(j));CHECK(recovery(j,5,false));
        CHECK(read(j,&current,&status) && status.recorded_phase==L4_REMOTE_RECORDED_WORKER_STARTING && !status.has_result);
        CHECK(l4_journal_append(j,67,L"<Task/>",14,NULL));CHECK(failed_launch(j,&request,mismatch));
        if(!mismatch){CHECK(read(j,&current,&status) && status.has_launch_failure && status.has_result && status.recorded_phase==L4_REMOTE_RECORDED_RECOVERY_REQUIRED);
            CHECK(status.result.error==ERROR_ACCESS_DENIED && status.launch_failure.cleanup_error==ERROR_TIMEOUT);
            CHECK(l4_journal_append(j,67,L"<Task/>",14,NULL));CHECK(!read(j,&current,&status) && GetLastError()==ERROR_INVALID_STATE);
        }else CHECK(!read(j,&current,&status) && !status.operation_id[0]);remove_journal(&j);
    }
    CHECK(start(&original,65,&j,&request));CHECK(ack(j,&request,false));CHECK(planned(j));CHECK(recovery(j,5,true));CHECK(!read(j,&current,&status));remove_journal(&j);
    CHECK(start(&original,66,&j,&request));CHECK(ack(j,&request,false));CHECK(planned(j));CHECK(recovery(j,4,false));CHECK(!read(j,&current,&status));remove_journal(&j);
    CHECK(start(&original,67,&j,&request));CHECK(ack(j,&request,false));CHECK(planned(j));CHECK(recovery(j,5,false));CHECK(recovery(j,5,false));CHECK(!read(j,&current,&status));remove_journal(&j);
    const DWORD bad_order[]={67,70,68,69,95};for(unsigned i=0;i<5;i++){
        CHECK(start(&original,70+i,&j,&request));CHECK(ack(j,&request,false));CHECK(planned(j));CHECK(l4_journal_append(j,bad_order[i],"x",1,NULL));CHECK(!read(j,&current,&status));remove_journal(&j);
    }
    CHECK(start(&original,60,&j,&request));CHECK(ack(j,&request,false));CHECK(l4_journal_append(j,62,"x",1,NULL));CHECK(!read(j,&current,&status) && GetLastError()==ERROR_INVALID_DATA);remove_journal(&j);
    CHECK(start(&original,61,&j,&request));CHECK(ack(j,&request,false));CHECK(l4_journal_append(j,60,"x",1,NULL));CHECK(l4_journal_append(j,60,"x",1,NULL));CHECK(!read(j,&current,&status) && GetLastError()==ERROR_INVALID_DATA);remove_journal(&j);
    /* Private live snapshot is stable while writer appends; partial tail never
     * exposes old status as current and is never truncated by this reader. */
    CHECK(start(&original,70,&j,&request));GUID id;const wchar_t* operation=wcsrchr(j->directory,L'\\')+1;CHECK(uuid(operation,&id));L4JournalReader* r=NULL;
    CHECK(snapshot(&current,operation,&id,INVALID_HANDLE_VALUE,true,&r));CHECK(ack(j,&request,false));
    CHECK(l4_remote_status_snapshot(r,&current,&status) && !status.has_host && status.last_sequence==1);l4_journal_reader_close(r);
    LARGE_INTEGER end;end.QuadPart=(LONGLONG)j->end;BYTE tail=0x7f;DWORD written;
    CHECK(SetFilePointerEx(j->file,end,NULL,FILE_BEGIN) && WriteFile(j->file,&tail,1,&written,NULL) && written==1 && FlushFileBuffers(j->file));
    CHECK(!read(j,&current,&status) && GetLastError()==ERROR_IO_PENDING);
    LARGE_INTEGER size;CHECK(GetFileSizeEx(j->file,&size) && size.QuadPart==end.QuadPart+1);
    CHECK(SetFilePointerEx(j->file,end,NULL,FILE_BEGIN) && SetEndOfFile(j->file) && FlushFileBuffers(j->file));
    CHECK(read(j,&current,&status) && status.has_host);
    CHECK(!l4_remote_status_observe(&current,operation,&status) && GetLastError()==ERROR_ACCESS_DENIED);remove_journal(&j);
    swprintf_s(path,MAX_PATH,L"%ls\\deployment.lock",original.operations);CHECK(DeleteFileW(path));CHECK(RemoveDirectoryW(original.operations));CHECK(RemoveDirectoryW(original.cache));CHECK(RemoveDirectoryW(original.staging));
    swprintf_s(path,MAX_PATH,L"%ls\\update",original.data);CHECK(RemoveDirectoryW(path));CHECK(RemoveDirectoryW(original.config));CHECK(RemoveDirectoryW(original.state));CHECK(RemoveDirectoryW(original.logs));
    swprintf_s(path,MAX_PATH,L"%ls\\releases",original.binaries);CHECK(RemoveDirectoryW(path));CHECK(RemoveDirectoryW(original.launchers));CHECK(RemoveDirectoryW(original.binaries));CHECK(RemoveDirectoryW(original.data));CHECK(RemoveDirectoryW(root));
    printf("Remote live status: %u checks, %u failures; actual journal/snapshot/92/93/94, signed route modeled, no ready/live-host proof\n",checks,failures);return failures?1:0;
}
