#include <windows.h>
#include <stdio.h>
#include <assert.h>
#include <shellapi.h>
#include "../src/fm_process.c"
/* A deliberately blocked child command models an uninterruptible filesystem call. */
bool rpc_uuid(const char* text) {return strlen(text)==36;}
bool fm_child_start(const char* sn,int port,HANDLE stop,FmResult result,void* context) {(void)sn;(void)port;(void)stop;(void)result;(void)context;return true;}
void fm_child_shutdown(void) {}
bool fm_child_enqueue(const RpcCommand* command,bool busy) {(void)command;(void)busy;Sleep(INFINITE);return true;}
bool fm_child_update_quiet(L4UpdateState* observed){(void)observed;return false;}
bool fm_child_busy(void) {return false;}
void fm_child_connection(bool connected) {(void)connected;}
void fm_child_tick(void) {}
void fm_child_set_navigation_result(FmNavigationResult result) {(void)result;}
bool fm_child_navigation(const char* p,size_t n,bool busy) {(void)p;(void)n;(void)busy;return false;}
static unsigned responses;
static void result(void* context,const char* id,int status,const char* body) {(void)context;(void)id;assert(status==200);(void)body;}
static bool navigation_result(void* context,const char* id,const char* body) {(void)context;(void)id;assert(strstr(body,"\"state\":\"completed\""));responses++;return true;}
int main(void) {
    int argc;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>1) {int code=fm_worker_main(argc,argv);LocalFree(argv);return code;}
    LocalFree(argv);
    assert(fm_start("fixture",18443,NULL,result,NULL));fm_set_navigation_result(navigation_result);fm_connection(true);
    assert(host.process && host.shared);
    HANDLE process=NULL;assert(DuplicateHandle(GetCurrentProcess(),host.process,GetCurrentProcess(),&process,SYNCHRONIZE,FALSE,0));
    RpcCommand command={0};command.method=7023;strcpy_s(command.task_id,40,"11111111-1111-4111-8111-111111111111");
    strcpy_s(command.session_id,64,"22222222-2222-4222-8222-222222222222");strcpy_s(command.fm_action,16,"start");command.fm_expires_at=(unsigned long long)time(NULL)+60;
    L4UpdateState expected={0};strcpy_s(expected.owner,40,"17730000-0000-4000-8000-000000000001");expected.window=1;expected.generation=1;expected.plan_sequence=64;expected.deadline_utc=1;
    assert(!fm_update_quiet(&expected));assert(lock_shared(host.mutex));
    host.shared->quiet_state=expected;host.shared->quiet=true;
    assert(fm_update_quiet(&expected));expected.generation++;assert(!fm_update_quiet(&expected));expected.generation--;
    host.shared->write_index=1;
    assert(!fm_update_quiet(&expected)); /* Final RPC/FMR result not published yet. */
    host.shared->read_index=1;host.shared->quiet=false;ReleaseMutex(host.mutex);
    assert(fm_enqueue(&command,false));Sleep(250);assert(fm_busy());
    strcpy_s(command.fm_action,16,"stop");assert(!fm_enqueue(&command,false));
    char stop[512];snprintf(stop,sizeof(stop),"{\"v\":2,\"action\":\"stop\",\"command_id\":\"%s\",\"lease_id\":\"%s\",\"expires_at\":%llu}",command.task_id,command.session_id,(unsigned long long)time(NULL)+7);
    ULONGLONG begin=GetTickCount64();assert(fm_navigation(stop,strlen(stop),false));
    assert(GetTickCount64()-begin<3000);assert(WaitForSingleObject(process,0)==WAIT_OBJECT_0);CloseHandle(process);
    assert(!fm_busy());assert(responses==1);fm_shutdown();
    puts("FM owned Job: blocked worker killed, stop acknowledged after exit, parent remains alive");return 0;
}
