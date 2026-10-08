#include "../src/rollback.h"
#include "../../l4common/tests/update_state_fixture.h"
#include <sddl.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL files/Job %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static bool put(const wchar_t* path,const char* text,PSECURITY_DESCRIPTOR sd){
    HANDLE previous=NULL;if(!l4_layout_owner_begin(&previous))return false;
    SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};HANDLE h=CreateFileW(path,GENERIC_WRITE,0,&sa,CREATE_ALWAYS,0,NULL);
    DWORD count=0;bool ok=h!=INVALID_HANDLE_VALUE && WriteFile(h,text,(DWORD)strlen(text),&count,NULL) && count==strlen(text);
    if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);return l4_layout_owner_end(previous) && ok;
}
static bool child(const wchar_t* input,PROCESS_INFORMATION* pi){
    wchar_t exe[MAX_PATH],command[MAX_PATH+32];DWORD n=GetModuleFileNameW(NULL,exe,MAX_PATH);if(!n || n>=MAX_PATH)return false;
    if(input)wcscpy_s(exe,MAX_PATH,input);
    swprintf_s(command,_countof(command),L"\"%ls\" --child",exe);STARTUPINFOW startup={0};startup.cb=sizeof(startup);
    return CreateProcessW(exe,command,NULL,NULL,FALSE,CREATE_SUSPENDED,NULL,NULL,&startup,pi)!=0;
}
int wmain(int argc,wchar_t** argv){
    if(argc==2 && !wcscmp(argv[1],L"--child")){Sleep(INFINITE);return 0;}
    UpdateFixture f;CHECK(update_fixture_init(&f));if(failures)return 1;CHECK(update_fixture_put(&f,0));
    PSECURITY_DESCRIPTOR sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FR;;;BU)",SDDL_REVISION_1,&sd,NULL));
    L4RecoveryPlan p={0};p.old_exists=true;p.old_config=(BYTE*)"old";p.old_config_size=3;p.new_config=(BYTE*)"candidate";p.new_config_size=9;
    wchar_t config[MAX_PATH];swprintf_s(config,MAX_PATH,L"%ls\\l4superv.json",f.layout.config);CHECK(put(config,"candidate",sd));
    HANDLE h=CreateFileW(config,READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);BYTE* saved=NULL;DWORD saved_size=0;
    CHECK(h!=INVALID_HANDLE_VALUE && l4_store_security(h,false,&saved,&saved_size));CloseHandle(h);p.config_sd=saved;p.config_sd_size=saved_size;
    CHECK(rollback_config(&f.layout,&p));CHECK(rollback_config(&f.layout,&p));
    CHECK(put(config,"operator-change",sd));CHECK(!rollback_config(&f.layout,&p));
    CHECK(put(config,"candidate",sd));wchar_t alias[MAX_PATH];swprintf_s(alias,MAX_PATH,L"%ls\\config-alias",f.layout.config);
    CHECK(CreateHardLinkW(alias,config,NULL));CHECK(!rollback_config(&f.layout,&p));CHECK(DeleteFileW(alias));
    swprintf_s(alias,MAX_PATH,L"%ls:extra",config);h=CreateFileW(alias,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);CHECK(h!=INVALID_HANDLE_VALUE);CloseHandle(h);
    CHECK(!rollback_config(&f.layout,&p));CHECK(DeleteFileW(alias));
    p.old_exists=false;p.old_config_size=0;CHECK(rollback_config(&f.layout,&p));CHECK(GetFileAttributesW(config)==INVALID_FILE_ATTRIBUTES);CHECK(rollback_config(&f.layout,&p));
    L4Layout old;CHECK(l4_layout_from_roots(&old,f.layout.binaries,f.layout.data,L"1.13.2") && l4_layout_prepare(&old));
    wchar_t directory[MAX_PATH],image[MAX_PATH];swprintf_s(directory,MAX_PATH,L"%ls\\l4superv",old.release);
    HANDLE directory_scope=NULL;CHECK(l4_layout_owner_begin(&directory_scope));SECURITY_ATTRIBUTES directory_sa={sizeof(directory_sa),sd,FALSE};
    CHECK(CreateDirectoryW(old.release,&directory_sa));CHECK(CreateDirectoryW(directory,&directory_sa));CHECK(l4_layout_owner_end(directory_scope));
    CHECK(l4_layout_component(&old,L"l4superv",L"l4superv.exe",image));CHECK(put(image,"old-image",sd));swprintf_s(p.before,2048,L"\"%ls\"",image);p.old_size=9;CHECK(l4_recovery_hash("old-image",9,p.old_sha256));
    CHECK(rollback_installed(&f.layout,image));
    L4FileFence fence={0};HANDLE pinned=INVALID_HANDLE_VALUE;wchar_t resolved[MAX_PATH];CHECK(rollback_image(&f.layout,&p,&pinned,&fence,resolved));
    h=CreateFileW(image,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(h==INVALID_HANDLE_VALUE);if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
    CloseHandle(pinned);l4_store_unpin(&fence);p.old_sha256[0]^=1;CHECK(!rollback_image(&f.layout,&p,&pinned,&fence,resolved));p.old_sha256[0]^=1;
    CHECK(SUCCEEDED(CoCreateGuid(&p.operation)));wchar_t uuid[40],name[96];StringFromGUID2(&p.operation,uuid,40);swprintf_s(name,96,L"Global\\L4UpdateWorker.%ls",uuid);
    PROCESS_INFORMATION pi={0};CHECK(child(NULL,&pi));if(!pi.hProcess)return 1;p.worker_pid=pi.dwProcessId;FILETIME e,k,u;CHECK(GetProcessTimes(pi.hProcess,&p.worker_created,&e,&k,&u));
    CHECK(!rollback_worker(&p,GetTickCount64()+1000));CHECK(WaitForSingleObject(pi.hProcess,0)==WAIT_TIMEOUT);
    PSECURITY_DESCRIPTOR private_sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&private_sd,NULL));
    HANDLE previous=NULL;CHECK(l4_layout_owner_begin(&previous));SECURITY_ATTRIBUTES sa={sizeof(sa),private_sd,FALSE};HANDLE job=CreateJobObjectW(&sa,name);CHECK(job);CHECK(l4_layout_owner_end(previous));
    CHECK(!rollback_worker(&p,GetTickCount64()+1000)); /* Missing kill-on-close profile cannot fence descendants. */
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    CHECK(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)));
    CHECK(!rollback_worker(&p,GetTickCount64()+1000));CHECK(WaitForSingleObject(pi.hProcess,0)==WAIT_TIMEOUT); /* Private wrong Job never owns bare PID. */
    CHECK(AssignProcessToJobObject(job,pi.hProcess));CHECK(ResumeThread(pi.hThread)!=MAXDWORD);CHECK(rollback_worker(&p,GetTickCount64()+5000));CHECK(WaitForSingleObject(pi.hProcess,0)==WAIT_OBJECT_0);
    if(WaitForSingleObject(pi.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(pi.hProcess,0);WaitForSingleObject(pi.hProcess,5000);}
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    CHECK(child(NULL,&pi));p.worker_pid=pi.dwProcessId;CHECK(GetProcessTimes(pi.hProcess,&p.worker_created,&e,&k,&u));p.worker_created.dwLowDateTime^=1;
    CHECK(rollback_worker(&p,GetTickCount64()+1000));CHECK(WaitForSingleObject(pi.hProcess,0)==WAIT_TIMEOUT); /* Reused identity remains untouched outside Job. */
    CHECK(TerminateProcess(pi.hProcess,0));CHECK(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(job);
    p.worker_pid=GetCurrentProcessId();CHECK(GetProcessTimes(GetCurrentProcess(),&p.worker_created,&e,&k,&u));CHECK(!rollback_worker(&p,GetTickCount64()+1000));
    wchar_t source[MAX_PATH];CHECK(GetModuleFileNameW(NULL,source,MAX_PATH));CHECK(CopyFileW(source,image,FALSE));
    CHECK(child(image,&pi));CHECK(ResumeThread(pi.hThread)!=MAXDWORD);
    CHECK(!rollback_exclusive(&p,0,GetTickCount64()+30));CHECK(WaitForSingleObject(pi.hProcess,0)==WAIT_TIMEOUT);
    CHECK(rollback_exclusive(&p,pi.dwProcessId,GetTickCount64()+1000));
    CHECK(TerminateProcess(pi.hProcess,0));CHECK(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    CHECK(rollback_exclusive(&p,0,GetTickCount64()+1000));
    LocalFree(private_sd);LocalFree(sd);free(saved);CHECK(update_fixture_dispose(&f));
    printf("Helper real config/image/worker Job: %u passed, %u failed; owned temporary children only, no SCM/tasks/network\n",checks-failures,failures);return failures?1:0;
}
