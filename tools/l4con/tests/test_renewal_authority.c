#include "../src/event_ipc.h"
#include <assert.h>
#include <stdio.h>
static bool publish(const UserEvent* event,void* context) {(void)event;(void)context;assert(0);return false;}
static DWORD child(HANDLE job,const wchar_t* name) {
    wchar_t path[MAX_PATH],command[MAX_PATH+4];
    DWORD len=GetModuleFileNameW(NULL,path,MAX_PATH);assert(len && len<MAX_PATH);
    wchar_t* slash=wcsrchr(path,(wchar_t)92);assert(slash);*slash=0;
    slash=wcsrchr(path,(wchar_t)92);assert(slash);*slash=0;
    assert(wcscat_s(path,MAX_PATH,name)==0);
    swprintf_s(command,MAX_PATH+4,L"\"%ls\"",path);
    STARTUPINFOW si={sizeof(si)};PROCESS_INFORMATION pi={0};
    assert(CreateProcessW(path,command,NULL,NULL,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,NULL,NULL,&si,&pi));
    if (job) assert(AssignProcessToJobObject(job,pi.hProcess));
    assert(ResumeThread(pi.hThread)!=(DWORD)-1);
    assert(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0);
    DWORD result=99;assert(GetExitCodeProcess(pi.hProcess,&result));
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return result;
}
int main(void) {
    HANDLE stop=CreateEventW(NULL,TRUE,FALSE,NULL);assert(stop);
    assert(event_ipc_start(stop,publish,NULL));
    HANDLE job=CreateJobObjectW(NULL,NULL);assert(job);
    volatile bool cancelled=false;volatile LONG protection=0;
    event_job_register(job,&cancelled,GetTickCount64()+120000);
    event_job_set_protection(job,&protection);
    assert(child(NULL,L"/l4pin/l4pin.exe")==3 && !protection);
    assert(child(job,L"/l4pin/wrong.exe")==3 && !protection);
    event_job_register(job,&cancelled,GetTickCount64()+30000);
    assert(child(job,L"/l4pin/l4pin.exe")==3 && !protection);
    event_job_register(job,&cancelled,GetTickCount64()+120000);
    assert(child(job,L"/l4pin/l4pin.exe")==0 && protection);
    assert(!event_job_cancel_replacement(&cancelled,&protection) && !cancelled);
    cancelled=true;assert(child(job,L"/l4pin/l4pin.exe")==3);
    event_job_revoke(job);CloseHandle(job);SetEvent(stop);event_ipc_stop();CloseHandle(stop);
    puts("Renewal authority: outside Job / wrong executable / short deadline / protected replacement / explicit cancel passed");return 0;
}
