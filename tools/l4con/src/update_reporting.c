#include "update_reporting.h"
#include "rpc_contract.h"
#include "../../l4common/journal_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <objbase.h>
#define REPORT_QUEUE 8u
#define REPORT_HISTORY 64u
typedef struct {char task[37];wchar_t operation[37];} Request;
typedef struct {wchar_t operation[37];char correlation[37];unsigned id,attempts;bool sent;ULONGLONG retry,finished;} Delivery;
struct L4UpdateReporting {
    L4Layout roots;HANDLE stop,thread;CRITICAL_SECTION lock;Request requests[REPORT_QUEUE];unsigned first,count;
    L4ReportingReady ready;L4ReportingReply reply;L4ReportingEvent event;void* context;
    Delivery deliveries[REPORT_HISTORY];unsigned used;ULONGLONG history_floor;HANDLE cursor;L4FileFence fence;unsigned seen;bool first_entry;WIN32_FIND_DATAW entry;
};
static bool uuid(const char* value){return rpc_uuid(value) && strcmp(value,"00000000-0000-0000-0000-000000000000");}
static bool wide_uuid(const wchar_t* value){char text[37];return WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,text,37,NULL,NULL) && uuid(text);}
static void reset_scan(L4UpdateReporting* r){if(r->cursor!=INVALID_HANDLE_VALUE)FindClose(r->cursor);r->cursor=INVALID_HANDLE_VALUE;l4_store_unpin(&r->fence);r->seen=0;}
static Delivery* delivery(L4UpdateReporting* r,const wchar_t* operation,ULONGLONG finished){
    for(unsigned i=0;i<r->used;i++)if(!_wcsicmp(operation,r->deliveries[i].operation))return r->deliveries+i;
    /* Bounded newest history: an evicted old result cannot be resent every scan. */
    if(finished<=r->history_floor)return NULL;unsigned slot=r->used;
    if(slot==REPORT_HISTORY){slot=0;for(unsigned i=1;i<REPORT_HISTORY;i++)if(r->deliveries[i].finished<r->deliveries[slot].finished)slot=i;
        if(finished<=r->deliveries[slot].finished)return NULL;}
    GUID g;if(FAILED(CoCreateGuid(&g)))return NULL;Delivery* d=r->deliveries+slot;
    if(r->used==REPORT_HISTORY)r->history_floor=d->finished;else ++r->used;
    memset(d,0,sizeof(*d));d->finished=finished;wcscpy_s(d->operation,37,operation);d->id=g.Data1&0x7fffffffU;if(!d->id)d->id=1;
    snprintf(d->correlation,37,"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",g.Data1,g.Data2,g.Data3,g.Data4[0],g.Data4[1],g.Data4[2],g.Data4[3],g.Data4[4],g.Data4[5],g.Data4[6],g.Data4[7]);return d;
}
static void send_result(L4UpdateReporting* r,const wchar_t* operation,const L4RemoteStatus* status){
    if(status->has_outcome){if(status->outcome.result.result!=L4_REMOTE_OUTCOME_RECOVERY_REQUIRED && !status->outcome_cleared)return;}
    else if(!status->has_result)return;
    ULONGLONG finished=status->has_outcome?status->outcome.result.finished_at:status->result.finished_at;
    Delivery* d=delivery(r,operation,finished);
    if(!d || d->sent || d->attempts>=2 || GetTickCount64()<d->retry || !r->ready(r->context))return;
    char json[2048];bool ok=update_status_result_json(status,d->id,d->correlation,json,sizeof(json));
    if(!ok)return;++d->attempts;d->sent=r->event(r->context,json,d->id,d->correlation);d->retry=GetTickCount64()+5000;
}
static bool take(L4UpdateReporting* r,Request* request){EnterCriticalSection(&r->lock);bool ok=r->count!=0;
    if(ok){*request=r->requests[r->first];r->first=(r->first+1)%REPORT_QUEUE;--r->count;}LeaveCriticalSection(&r->lock);return ok;}
static void status_request(L4UpdateReporting* r,const Request* request){
    L4RemoteStatus status;char json[2048];int code=200;DWORD error=0;
    if(!l4_remote_status_observe(&r->roots,request->operation,&status) || !update_status_rpc_json(&status,request->task,json,sizeof(json))){
        error=GetLastError();code=error==ERROR_IO_PENDING?503:(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND || error==ERROR_NOT_FOUND)?404:409;
        snprintf(json,sizeof(json),"{\"correlationData\":\"%s\",\"status_code\":%d,\"status\":\"%s\",\"error\":%lu}",request->task,code,code==503?"snapshot_pending":code==404?"operation_not_found":"snapshot_refused",error);
    }
    r->reply(r->context,request->task,code,json);if(code==200)send_result(r,request->operation,&status);
}
static void scan(L4UpdateReporting* r){
    if(r->cursor==INVALID_HANDLE_VALUE){wchar_t pattern[MAX_PATH];
        if(!l4_store_pin(r->roots.operations,r->roots.data,false,&r->fence))return;
        if(swprintf_s(pattern,MAX_PATH,L"%ls\\*",r->roots.operations)<0){reset_scan(r);return;}
        r->cursor=FindFirstFileW(pattern,&r->entry);r->first_entry=true;if(r->cursor==INVALID_HANDLE_VALUE){reset_scan(r);return;}}
    unsigned reads=0;while(reads<4 && r->seen<256 && WaitForSingleObject(r->stop,0)==WAIT_TIMEOUT && r->ready(r->context)){
        if(!r->first_entry && !FindNextFileW(r->cursor,&r->entry)){reset_scan(r);return;}r->first_entry=false;++r->seen;
        if(!(r->entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || r->entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT || !wide_uuid(r->entry.cFileName))continue;
        ++reads;L4RemoteStatus status;if(l4_remote_status_observe(&r->roots,r->entry.cFileName,&status))send_result(r,r->entry.cFileName,&status);
    }
    /* Resume the same cursor next cycle; old history cannot starve newer dirs. */
    if(r->seen>=256)r->seen=0;
}
static DWORD WINAPI run(void* context){L4UpdateReporting* r=context;
    while(WaitForSingleObject(r->stop,0)==WAIT_TIMEOUT){if(r->ready(r->context)){Request request;
            if(take(r,&request))status_request(r,&request);else scan(r);}
        if(WaitForSingleObject(r->stop,1000)!=WAIT_TIMEOUT)break;}
    reset_scan(r);return 0;
}
bool update_reporting_start(const L4Layout* roots,HANDLE stop,L4ReportingReady ready,L4ReportingReply reply,L4ReportingEvent event,void* context,L4UpdateReporting** output){
    if(!output)return false;*output=NULL;if(!roots || !stop || !ready || !reply || !event){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    L4UpdateReporting* r=calloc(1,sizeof(*r));if(!r){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return false;}
    r->roots=*roots;r->stop=stop;r->ready=ready;r->reply=reply;r->event=event;r->context=context;r->cursor=INVALID_HANDLE_VALUE;InitializeCriticalSection(&r->lock);
    r->thread=CreateThread(NULL,0,run,r,0,NULL);if(!r->thread){DWORD error=GetLastError();DeleteCriticalSection(&r->lock);free(r);SetLastError(error);return false;}*output=r;return true;
}
bool update_reporting_status(L4UpdateReporting* r,const char* task,const char* operation){
    if(!r || !uuid(task) || !uuid(operation)){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    Request request={0};strcpy_s(request.task,37,task);if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,operation,-1,request.operation,37))return false;
    EnterCriticalSection(&r->lock);bool ok=r->count<REPORT_QUEUE && WaitForSingleObject(r->stop,0)==WAIT_TIMEOUT;
    if(ok){r->requests[(r->first+r->count)%REPORT_QUEUE]=request;++r->count;}LeaveCriticalSection(&r->lock);
    if(!ok)SetLastError(ERROR_BUSY);return ok;
}
bool update_reporting_close(L4UpdateReporting** output,DWORD timeout){
    if(!output || !timeout || timeout>300000){SetLastError(ERROR_INVALID_PARAMETER);return false;}L4UpdateReporting* r=*output;if(!r)return true;
    /* Shared stop is owned/signaled by Con. Never free callback context early. */
    if(WaitForSingleObject(r->thread,timeout)!=WAIT_OBJECT_0){SetLastError(ERROR_TIMEOUT);return false;}
    CloseHandle(r->thread);DeleteCriticalSection(&r->lock);free(r);*output=NULL;return true;
}
