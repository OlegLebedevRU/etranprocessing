/* Owned threads only; backend modeled. No SCM/journal/file/broker mutation. */
#include "../src/update_admission.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#if L4CON_REMOTE_ADMISSION_ENABLED
static HANDLE entered,release,reply_done,callback_release;static unsigned opens,saves,launches,replies,closes;static int last_code;
static bool bad_receipt,launch_fail,hold_callback;static char returned_task[37];
static bool fixture_open(const L4Layout* roots,const wchar_t* operation,bool create,L4Journal** journal){
    assert(roots && create && !wcscmp(operation,L"135a4120-9ba6-4f6c-8cac-4baf5df8f1df"));++opens;*journal=(L4Journal*)1;return true;
}
static bool fixture_save(L4Journal* journal,DWORD target,const char* version,L4RemoteRequest* saved){
    assert(journal==(L4Journal*)1 && target==L4_REMOTE_SUITE && !strcmp(version,"latest"));++saves;
    memset(saved,0,sizeof(*saved));saved->target=target;strcpy_s(saved->version,32,version);saved->accepted_utc=123;return true;
}
static bool fixture_launch(L4Journal** journal,const char* arch,DWORD timeout,L4RemoteHost** host,L4RemoteHostReceipt* receipt){
    assert(*journal==(L4Journal*)1 && (!strcmp(arch,"x86") || !strcmp(arch,"x64")) && timeout==120000);++launches;
    SetEvent(entered);assert(WaitForSingleObject(release,5000)==WAIT_OBJECT_0);
    if(launch_fail){SetLastError(ERROR_ACCESS_DENIED);return false;}
    *journal=NULL;*host=(L4RemoteHost*)1;memset(receipt,0,sizeof(*receipt));receipt->pid=456;receipt->birth=789;receipt->format_version=2;strcpy_s(receipt->updater_version,32,"1.13.5");
    receipt->request.target=L4_REMOTE_SUITE;strcpy_s(receipt->request.version,32,"latest");receipt->request.accepted_utc=bad_receipt?124:123;return true;
}
static void fixture_journal_close(L4Journal* journal){assert(journal==(L4Journal*)1);++closes;}
static void fixture_host_close(L4RemoteHost* host){assert(!host || host==(L4RemoteHost*)1);}
#define l4_journal_open fixture_open
#define l4_remote_request_save fixture_save
#define l4_remote_host_launch fixture_launch
#define l4_journal_close fixture_journal_close
#define l4_remote_host_close fixture_host_close
#endif
#include "../src/update_admission.c"
static void reply(void* context,const char* task,int code,const char* json){
    assert(context==(void*)7 && strstr(json,task));
#if L4CON_REMOTE_ADMISSION_ENABLED
    ++replies;last_code=code;strcpy_s(returned_task,37,task);SetEvent(reply_done);
    if(hold_callback)assert(WaitForSingleObject(callback_release,5000)==WAIT_OBJECT_0);
#else
    (void)code;assert(false);
#endif
}
static RpcCommand request(void){RpcCommand command={0};command.method=7031;
    strcpy_s(command.task_id,40,"135a4120-9ba6-4f6c-8cac-4baf5df8f1df");
    strcpy_s(command.update_operation_id,40,command.task_id);strcpy_s(command.update_version,32,"latest");strcpy_s(command.update_target,8,"suite");return command;
}
int main(void){L4Layout roots={0};HANDLE stop=CreateEventW(NULL,TRUE,FALSE,NULL);L4UpdateAdmission* a=NULL;
    assert(stop && update_admission_start(&roots,stop,reply,(void*)7,&a));RpcCommand command=request(),bad=command;
    bad.update_operation_id[0]='2';assert(!update_admission_submit(a,&bad) && GetLastError()==ERROR_INVALID_PARAMETER);
    bad=command;strcpy_s(bad.update_version,32,"01.2.3");assert(!update_admission_submit(a,&bad));
    bad=command;strcpy_s(bad.update_target,8,"updater");assert(!update_admission_submit(a,&bad) && GetLastError()==ERROR_NOT_SUPPORTED);
#if L4CON_REMOTE_ADMISSION_ENABLED
    entered=CreateEventW(NULL,TRUE,FALSE,NULL);release=CreateEventW(NULL,TRUE,FALSE,NULL);reply_done=CreateEventW(NULL,TRUE,FALSE,NULL);
    callback_release=CreateEventW(NULL,TRUE,FALSE,NULL);hold_callback=true;
    assert(update_admission_enabled() && !update_admission_busy(a) && update_admission_submit(a,&command));
    /* Submit did not wait for backend ACK; original copied request survives caller mutation. */
    strcpy_s(command.update_version,32,"1.2.3");assert(WaitForSingleObject(entered,2000)==WAIT_OBJECT_0);
    assert(update_admission_busy(a));
    assert(!update_admission_submit(a,&command) && GetLastError()==ERROR_BUSY);
    assert(!update_admission_close(&a,1) && GetLastError()==ERROR_TIMEOUT && a);
    assert(update_admission_busy(a));
    assert(!update_admission_submit(a,&command) && GetLastError()==ERROR_BUSY);
    SetEvent(release);assert(WaitForSingleObject(reply_done,2000)==WAIT_OBJECT_0);
    assert(update_admission_busy(a));assert(!update_admission_close(&a,1) && a && GetLastError()==ERROR_TIMEOUT);
    SetEvent(callback_release);
    ULONGLONG end=GetTickCount64()+2000;while(update_admission_busy(a) && GetTickCount64()<end)Sleep(1);
    assert(!update_admission_busy(a));assert(update_admission_close(&a,2000) && !a);
    assert(opens==1 && saves==1 && launches==1 && replies==1 && last_code==202 && !strcmp(returned_task,request().task_id));
    hold_callback=false;bad_receipt=true;ResetEvent(reply_done);command=request();assert(update_admission_start(&roots,stop,reply,(void*)7,&a));
    assert(update_admission_submit(a,&command));assert(WaitForSingleObject(reply_done,2000)==WAIT_OBJECT_0 && last_code==503);
    assert(update_admission_close(&a,2000));bad_receipt=false;launch_fail=true;ResetEvent(reply_done);
    assert(update_admission_start(&roots,stop,reply,(void*)7,&a) && update_admission_submit(a,&command));
    assert(WaitForSingleObject(reply_done,2000)==WAIT_OBJECT_0 && last_code==503 && closes==1);assert(update_admission_close(&a,2000));
    CloseHandle(entered);CloseHandle(release);CloseHandle(reply_done);CloseHandle(callback_release);
#else
    assert(!update_admission_enabled() && !update_admission_busy(a));assert(!update_admission_submit(a,&command) && GetLastError()==ERROR_NOT_SUPPORTED);
    assert(update_admission_close(&a,2000) && !a);
#endif
    assert(update_admission_close(&a,0));CloseHandle(stop);puts("PASS: admission identity, closed gate, bounded async ownership and teardown");return 0;
}
