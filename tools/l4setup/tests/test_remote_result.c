#include "../src/remote_result.h"
#include "../src/update_metadata.h"
#include "../../l4common/remote_host.h"
#include "../../l4common/journal_internal.h"
#include <stdio.h>
#include <string.h>
static unsigned checks,failures,identity_calls;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL line%d error%lu: %s\n",__LINE__,GetLastError(),#x);}}while(0)
static L4RemoteHostReceipt receipt;
static bool self_ok=true,trusted=true;
static int route_mode;
struct L4RoutePlan {int unused;};static L4RoutePlan modeled_route;
bool l4_remote_host_load(L4Journal* j,L4RemoteHostReceipt* out){(void)j;*out=receipt;return true;}
bool l4_remote_host_self(const L4Layout* l,const wchar_t* operation,const char* arch){(void)l;(void)operation;(void)arch;identity_calls++;if(!self_ok){SetLastError(ERROR_ACCESS_DENIED);return false;}return true;}
bool l4_route_load_trusted(L4Journal* j,ULONGLONG sequence,L4RoutePlan** out){(void)j;(void)sequence;*out=&modeled_route;return true;}
bool l4_route_is_owner_trusted(const L4RoutePlan* p){(void)p;return trusted;}
bool l4_route_source(const L4RoutePlan* p,L4CatalogRelease* out){(void)p;memset(out,0,sizeof(*out));strcpy_s(out->version,64,route_mode==1?"1.13.5":"1.13.6");return true;}
const char* l4_route_requested(const L4RoutePlan* p){(void)p;return route_mode==2?"1.13.8":"latest";}
const char* l4_route_arch(const L4RoutePlan* p){(void)p;return route_mode==3?"x64":"x86";}
const L4CatalogRoute* l4_route_steps(const L4RoutePlan* p){(void)p;static L4CatalogRoute r;memset(&r,0,sizeof(r));r.count=route_mode==4?0:1;strcpy_s(r.releases[0].version,64,"1.13.7");return &r;}
void l4_route_free(L4RoutePlan* p){(void)p;}
static bool start(const L4Layout* layout,unsigned number,bool ack,L4Journal** j){
    wchar_t id[37];swprintf_s(id,37,L"135a4120-9ba6-4f6c-8cac-%012u",number);
    if(!l4_journal_open(layout,id,true,j))return false;memset(&receipt,0,sizeof(receipt));
    if(!l4_remote_request_save(*j,L4_REMOTE_SUITE,"latest",&receipt.request))return false;
    FILETIME b,e,k,u;if(!GetProcessTimes(GetCurrentProcess(),&b,&e,&k,&u))return false;
    receipt.pid=GetCurrentProcessId();receipt.birth=((ULONGLONG)b.dwHighDateTime<<32)|b.dwLowDateTime;
    receipt.executable_size=17;memset(receipt.executable_sha256,1,32);strcpy_s(receipt.source_version,32,"1.13.6");strcpy_s(receipt.arch,8,"x86");
    BYTE bytes[112]={0};memcpy(bytes,"L4RHST01",8);l4_store_u32(bytes+8,1);l4_store_u32(bytes+12,receipt.pid);l4_store_u64(bytes+16,receipt.birth);
    l4_store_u64(bytes+24,receipt.executable_size);memcpy(bytes+32,receipt.executable_sha256,32);l4_store_u64(bytes+64,receipt.request.accepted_utc);
    memcpy(bytes+72,receipt.source_version,strlen(receipt.source_version));memcpy(bytes+104,receipt.arch,strlen(receipt.arch));
    return !ack || l4_journal_append(*j,L4_RECORD_REMOTE_HOST_ACK,bytes,sizeof(bytes),NULL);
}
static void finish(L4Journal** j){wchar_t directory[MAX_PATH],file[MAX_PATH];wcscpy_s(directory,MAX_PATH,(*j)->directory);l4_journal_close(*j);*j=NULL;
    swprintf_s(file,MAX_PATH,L"%ls\\journal.bin",directory);CHECK(DeleteFileW(file));CHECK(RemoveDirectoryW(directory));}
static void codec(L4RemoteResult good){
    BYTE bytes[L4_REMOTE_RESULT_BYTES],changed[L4_REMOTE_RESULT_BYTES];L4RemoteResult out;
    CHECK(l4_remote_result_encode(&good,bytes));CHECK(l4_remote_result_decode(bytes,sizeof(bytes),&out));CHECK(!strcmp(out.operation_id,good.operation_id) && out.error==good.error);
    for(unsigned i=76;i<80;i++){memcpy(changed,bytes,sizeof(bytes));changed[i]=1;CHECK(!l4_remote_result_decode(changed,sizeof(changed),&out) && !out.operation_id[0]);}
    for(unsigned i=176;i<sizeof(bytes);i++){memcpy(changed,bytes,sizeof(bytes));changed[i]=1;CHECK(!l4_remote_result_decode(changed,sizeof(changed),&out));}
    memcpy(changed,bytes,sizeof(bytes));changed[8]=2;CHECK(!l4_remote_result_decode(changed,sizeof(changed),&out));
    CHECK(!l4_remote_result_decode(bytes,sizeof(bytes)-1,&out));
    L4RemoteResult bad=good;bad.error=0;CHECK(!l4_remote_result_encode(&bad,bytes) && !bytes[0]);
    bad=good;bad.result=3;CHECK(!l4_remote_result_encode(&bad,bytes));bad=good;bad.target=2;CHECK(!l4_remote_result_encode(&bad,bytes));
    bad=good;bad.result=L4_REMOTE_RESULT_CANCELLED;CHECK(!l4_remote_result_encode(&bad,bytes));
    bad.error=ERROR_CANCELLED;CHECK(l4_remote_result_encode(&bad,bytes));bad.result=L4_REMOTE_RESULT_FAILED;CHECK(!l4_remote_result_encode(&bad,bytes));
    bad=good;bad.finished_at=bad.started_at-1;CHECK(!l4_remote_result_encode(&bad,bytes));
    bad=good;memset(bad.operation_id,'0',36);bad.operation_id[8]=bad.operation_id[13]=bad.operation_id[18]=bad.operation_id[23]='-';CHECK(!l4_remote_result_encode(&bad,bytes));
    const char* invalid[]={"01.13.7","1.13.7\"","1.13","LATEST","1000000000.1.1"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){bad=good;strcpy_s(bad.requested_version,32,invalid[i]);CHECK(!l4_remote_result_encode(&bad,bytes));}
    bad=good;strcpy_s(bad.previous_version,32,"latest");CHECK(!l4_remote_result_encode(&bad,bytes));
    bad=good;strcpy_s(bad.resolved_version,32,"latest");CHECK(!l4_remote_result_encode(&bad,bytes));
}
int wmain(void){
    wchar_t temp[MAX_PATH],root[MAX_PATH],pf[MAX_PATH],pd[MAX_PATH],path[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));
    swprintf_s(root,MAX_PATH,L"%lsL4RemoteResult-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(root,NULL));
    swprintf_s(pf,MAX_PATH,L"%ls\\PF",root);swprintf_s(pd,MAX_PATH,L"%ls\\PD",root);L4Layout layout;
    CHECK(l4_layout_from_roots(&layout,pf,pd,L"1.13.6") && l4_layout_prepare(&layout));L4Journal* j=NULL;L4RemoteResult value,again;
    CHECK(start(&layout,1,true,&j));CHECK(!setup_remote_preparation_finish(j,0,&value) && GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(setup_remote_preparation_finish(j,ERROR_TIMEOUT,&value) && j->sequence==3 && value.result==L4_REMOTE_RESULT_FAILED && !value.resolved_version[0]);
    codec(value);ULONGLONG finished=value.finished_at;
    CHECK(setup_remote_preparation_finish(j,ERROR_TIMEOUT,&again) && again.finished_at==finished && j->sequence==3);
    CHECK(!setup_remote_preparation_finish(j,ERROR_ACCESS_DENIED,&again) && GetLastError()==ERROR_ALREADY_EXISTS && !again.operation_id[0]);
    wchar_t operation[37];wcscpy_s(operation,37,wcsrchr(j->directory,L'\\')+1);l4_journal_close(j);j=NULL;
    CHECK(l4_journal_open(&layout,operation,false,&j));self_ok=false;unsigned old_calls=identity_calls;
    CHECK(setup_remote_preparation_result(j,&again) && again.finished_at==finished && identity_calls==old_calls);self_ok=true;
    BYTE encoded[L4_REMOTE_RESULT_BYTES];CHECK(l4_remote_result_encode(&again,encoded));CHECK(l4_journal_append(j,L4_RECORD_REMOTE_PREPARATION_RESULT,encoded,sizeof(encoded),NULL));
    CHECK(!setup_remote_preparation_result(j,&again) && GetLastError()==ERROR_INVALID_STATE && !again.operation_id[0]);finish(&j);
    CHECK(start(&layout,2,true,&j));CHECK(setup_remote_preparation_finish(j,ERROR_CANCELLED,&value) && value.result==L4_REMOTE_RESULT_CANCELLED);finish(&j);
    CHECK(start(&layout,3,false,&j));CHECK(!setup_remote_preparation_finish(j,5,&value) && GetLastError()==ERROR_INVALID_DATA);finish(&j);
    const DWORD advanced[]={10,63,64,68,69,70,90,96};
    for(unsigned i=0;i<sizeof(advanced)/sizeof(advanced[0]);i++){CHECK(start(&layout,10+i,true,&j));CHECK(l4_journal_append(j,advanced[i],"x",1,NULL));CHECK(!setup_remote_preparation_finish(j,5,&value) && GetLastError()==ERROR_INVALID_STATE && j->sequence==3);finish(&j);}
    CHECK(start(&layout,30,true,&j));receipt.pid++;CHECK(!setup_remote_preparation_finish(j,5,&value) && GetLastError()==ERROR_ACCESS_DENIED && j->sequence==2);receipt.pid--;
    receipt.birth++;CHECK(!setup_remote_preparation_finish(j,5,&value) && GetLastError()==ERROR_ACCESS_DENIED);receipt.birth--;
    self_ok=false;CHECK(!setup_remote_preparation_finish(j,5,&value) && j->sequence==2);self_ok=true;finish(&j);
    for(int mode=0;mode<=4;mode++){route_mode=mode;CHECK(start(&layout,40+mode,true,&j));CHECK(l4_journal_append(j,L4_RECORD_ROUTE_PLAN,"x",1,NULL));
        if(mode==0 || mode==4){CHECK(setup_remote_preparation_finish(j,5,&value));CHECK(!strcmp(value.resolved_version,mode==4?"1.13.6":"1.13.7"));}
        else CHECK(!setup_remote_preparation_finish(j,5,&value) && GetLastError()==ERROR_REVISION_MISMATCH && j->sequence==3);
        finish(&j);}
    route_mode=0;trusted=false;CHECK(start(&layout,50,true,&j));CHECK(l4_journal_append(j,L4_RECORD_ROUTE_PLAN,"x",1,NULL));CHECK(!setup_remote_preparation_finish(j,5,&value));trusted=true;finish(&j);
    CHECK(start(&layout,51,true,&j));strcpy_s(receipt.request.version,32,"1.13.7");CHECK(!setup_remote_preparation_finish(j,5,&value) && GetLastError()==ERROR_INVALID_DATA);finish(&j);
    /* Shape-valid forged outcome must still bind to original private history. */
    for(unsigned mismatch=0;mismatch<5;mismatch++){
        CHECK(start(&layout,60+mismatch,true,&j));memset(&value,0,sizeof(value));
        CHECK(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wcsrchr(j->directory,L'\\')+1,-1,value.operation_id,37,NULL,NULL));
        value.target=1;value.result=L4_REMOTE_RESULT_FAILED;value.error=5;value.started_at=receipt.request.accepted_utc;value.finished_at=value.started_at+100000000;
        strcpy_s(value.requested_version,32,"latest");strcpy_s(value.previous_version,32,"1.13.6");
        if(mismatch==0)value.operation_id[0]='2';if(mismatch==1)value.started_at++;
        if(mismatch==2)strcpy_s(value.requested_version,32,"1.13.7");
        if(mismatch==3)strcpy_s(value.previous_version,32,"1.13.5");if(mismatch==4)strcpy_s(value.resolved_version,32,"1.13.7");
        CHECK(l4_remote_result_encode(&value,encoded));CHECK(l4_journal_append(j,L4_RECORD_REMOTE_PREPARATION_RESULT,encoded,sizeof(encoded),NULL));
        CHECK(!setup_remote_preparation_result(j,&again) && GetLastError()==ERROR_REVISION_MISMATCH && !again.operation_id[0]);finish(&j);
    }
    CHECK(start(&layout,70,true,&j));CHECK(l4_journal_append(j,L4_RECORD_ROUTE_PLAN,"x",1,NULL));
    CHECK(l4_journal_append(j,L4_RECORD_PACKAGES_COMPLETE,"x",1,NULL));
    const DWORD proposals[]={L4_RECORD_CONFIG_PLAN,L4_RECORD_INSTALLED_SOURCE,L4_RECORD_SWITCH_PLAN,L4_RECORD_OPERATION_PLAN};
    for(unsigned i=0;i<sizeof(proposals)/sizeof(proposals[0]);i++)CHECK(l4_journal_append(j,proposals[i],"x",1,NULL));
    CHECK(setup_remote_preparation_finish(j,ERROR_CANCELLED,&value) && value.result==L4_REMOTE_RESULT_CANCELLED && !strcmp(value.resolved_version,"1.13.7"));finish(&j);
    CHECK(start(&layout,71,true,&j));CHECK(l4_journal_append(j,L4_RECORD_ROUTE_PLAN,"x",1,NULL));CHECK(l4_journal_append(j,L4_RECORD_PACKAGES_COMPLETE,"x",1,NULL));
    CHECK(l4_journal_append(j,L4_RECORD_CONFIG_INTENT,"x",1,NULL));CHECK(!setup_remote_preparation_finish(j,5,&value) && GetLastError()==ERROR_INVALID_STATE);finish(&j);
    swprintf_s(path,MAX_PATH,L"%ls\\deployment.lock",layout.operations);CHECK(DeleteFileW(path));CHECK(RemoveDirectoryW(layout.operations));CHECK(RemoveDirectoryW(layout.cache));CHECK(RemoveDirectoryW(layout.staging));
    swprintf_s(path,MAX_PATH,L"%ls\\update",layout.data);CHECK(RemoveDirectoryW(path));CHECK(RemoveDirectoryW(layout.config));CHECK(RemoveDirectoryW(layout.state));CHECK(RemoveDirectoryW(layout.logs));
    swprintf_s(path,MAX_PATH,L"%ls\\releases",layout.binaries);CHECK(RemoveDirectoryW(path));CHECK(RemoveDirectoryW(layout.launchers));CHECK(RemoveDirectoryW(layout.binaries));CHECK(RemoveDirectoryW(layout.data));CHECK(RemoveDirectoryW(root));
    printf("Remote preparation result: %u checks, %u failures; actual journal/codec, host/route modeled, no delivery claim\n",checks,failures);return failures?1:0;
}
