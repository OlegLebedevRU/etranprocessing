/* Entry guards only: no replacement of admission success, no real service/API. */
#include "../src/remote_entry.h"
#include "../src/acceptance_local.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL remote entry %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static bool unexpectedly_called(void){CHECK(false);SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return false;}
bool setup_installed_source_open(L4Journal* owner,SetupInstalledSource** source){(void)owner;(void)source;return unexpectedly_called();}
bool setup_installed_source_verify(SetupInstalledSource* source){(void)source;return unexpectedly_called();}
void setup_installed_source_close(SetupInstalledSource* source){if(source)unexpectedly_called();}
const char* setup_installed_source_arch(const SetupInstalledSource* source){(void)source;unexpectedly_called();return NULL;}
const char* setup_installed_source_version(const SetupInstalledSource* source){(void)source;unexpectedly_called();return NULL;}
const char* setup_installed_source_updater_version(const SetupInstalledSource* source){(void)source;unexpectedly_called();return NULL;}
const SetupRootManifest* setup_installed_source_updater_root(const SetupInstalledSource* source){(void)source;unexpectedly_called();return NULL;}
const SetupRootManifest* setup_installed_source_root(const SetupInstalledSource* source){(void)source;unexpectedly_called();return NULL;}
const SetupRootAsset* setup_root_installer(const SetupRootManifest* root){(void)root;unexpectedly_called();return NULL;}
const BYTE* setup_root_publisher(const SetupRootManifest* root){(void)root;unexpectedly_called();return NULL;}
bool setup_signed_executable(HANDLE file,const wchar_t* path,const BYTE publisher[32]){(void)file;(void)path;(void)publisher;return unexpectedly_called();}
bool setup_acceptance_open_optional(L4Journal* j,SetupAcceptancePin** out){(void)j;(void)out;return unexpectedly_called();}
bool setup_acceptance_bind(SetupAcceptancePin* p,L4Journal* j,SetupInstalledSource* s){(void)p;(void)j;(void)s;return unexpectedly_called();}
bool setup_acceptance_route(SetupAcceptancePin* p,L4Journal* j,SetupInstalledSource* s){(void)p;(void)j;(void)s;return unexpectedly_called();}
void setup_acceptance_close(SetupAcceptancePin* p){if(p)unexpectedly_called();}
#include "../src/remote_entry.c"
static bool engine_preflight(L4Journal* j,SetupInstalledSource* s,const L4RemoteRequest* r){(void)j;(void)s;(void)r;return unexpectedly_called();}
static DWORD engine_execute(L4Journal** j,SetupInstalledSource* s,const L4RemoteRequest* r,const volatile LONG* c){(void)j;(void)s;(void)r;(void)c;unexpectedly_called();return ERROR_CALL_NOT_IMPLEMENTED;}
int wmain(void){DWORD result=999;wchar_t* plain[]={L"test.exe",L"--fresh-help"};CHECK(!setup_remote_entry(2,plain,NULL,&result) && result==999);
    wchar_t* args[]={L"test.exe",L"--remote-controller",L"--source-version",L"1.13.6",L"--operation",L"17730000-0000-4000-8000-000000000031",L"--arch",L"x86",L"--updater-version",L"1.13.5"};
    CHECK(setup_remote_entry(2,args,NULL,&result) && result==ERROR_INVALID_PARAMETER);
    CHECK(setup_remote_entry(10,args,NULL,&result) && result==ERROR_ACCESS_DENIED); /* actual developer path, never dispatches SCM */
    args[5]=L"17730000-0000-4000-8000-0000000000AB";CHECK(setup_remote_entry(10,args,NULL,&result) && result==ERROR_INVALID_PARAMETER);
    args[5]=L"00000000-0000-0000-0000-000000000000";CHECK(setup_remote_entry(10,args,NULL,&result) && result==ERROR_INVALID_PARAMETER);
    args[5]=L"../escape";CHECK(setup_remote_entry(10,args,NULL,&result) && result==ERROR_INVALID_PARAMETER);
    args[5]=L"17730000-0000-4000-8000-000000000031";args[7]=L"arm64";CHECK(setup_remote_entry(10,args,NULL,&result) && result==ERROR_INVALID_PARAMETER);
    args[7]=L"x86";args[2]=L"--operation";CHECK(setup_remote_entry(10,args,NULL,&result) && result==ERROR_INVALID_PARAMETER);
    /* A registered host with absent/incomplete engine still cannot ACK a no-op. */
    memset(&entry,0,sizeof(entry));CHECK(execute()==ERROR_CALL_NOT_IMPLEMENTED);
    SetupRemoteEngine incomplete={engine_preflight,NULL};entry.engine=&incomplete;CHECK(execute()==ERROR_CALL_NOT_IMPLEMENTED);
    incomplete.preflight=NULL;incomplete.execute=engine_execute;CHECK(execute()==ERROR_CALL_NOT_IMPLEMENTED);
    entry.engine=NULL;
    printf("Remote entry: %u checks, %u failures; missing engine refuses before journal/source/ACK; no live SCM/admission substitution\n",checks,failures);return failures?1:0;
}
