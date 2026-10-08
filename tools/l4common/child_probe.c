#include <winsock2.h>
#include "child_probe.h"
#include <iphlpapi.h>
#include <stdlib.h>

bool l4_child_listeners(HANDLE process,DWORD pid,WORD first,WORD second) {
    if(!process || !pid || GetProcessId(process)!=pid || !first || !second || first==second ||
       WaitForSingleObject(process,0)!=WAIT_TIMEOUT)return false;
    DWORD size=0,status=GetExtendedTcpTable(NULL,&size,FALSE,AF_INET,TCP_TABLE_OWNER_PID_LISTENER,0);
    if(status!=ERROR_INSUFFICIENT_BUFFER || !size || size>1024*1024)return false;
    MIB_TCPTABLE_OWNER_PID* table=malloc(size);if(!table)return false;
    status=GetExtendedTcpTable(table,&size,FALSE,AF_INET,TCP_TABLE_OWNER_PID_LISTENER,0);
    bool a=false,b=false,valid=status==NO_ERROR;
    if(valid)for(DWORD i=0;i<table->dwNumEntries;i++) {
        MIB_TCPROW_OWNER_PID* row=&table->table[i];WORD port=ntohs((u_short)row->dwLocalPort);
        if(port!=first && port!=second)continue;
        if(row->dwLocalAddr!=htonl(INADDR_LOOPBACK) || row->dwOwningPid!=pid){valid=false;break;}
        if(port==first)a=true;else b=true;
    }
    free(table);return valid && a && b && WaitForSingleObject(process,0)==WAIT_TIMEOUT;
}
static bool child_probe(const L4ServiceToken* token,const wchar_t* exe,const wchar_t* directory,wchar_t* command,
                        DWORD timeout,L4ChildCheck check,void* context) {
    if(!exe || !*exe || !directory || !*directory || !command || !*command || !check ||
       timeout<100 || timeout>300000){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    ULONGLONG deadline=GetTickCount64()+timeout;
    HANDLE job=CreateJobObjectW(NULL,NULL);if(!job)return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    STARTUPINFOW startup={0};startup.cb=sizeof(startup);PROCESS_INFORMATION child={0};bool ok=false,assigned=false;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))goto done;
    if(token) {if(!l4_service_token_create(token,exe,directory,command,&child))goto done;}
    else if(!CreateProcessW(exe,command,NULL,NULL,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,NULL,directory,&startup,&child))goto done;
    if(!AssignProcessToJobObject(job,child.hProcess))goto done;assigned=true;
    if(token && !l4_service_token_verify(token))goto done;
    if(ResumeThread(child.hThread)==(DWORD)-1)goto done;
    if(GetTickCount64()<deadline)ok=check(child.hProcess,child.dwProcessId,deadline,context);
    ok=ok && GetTickCount64()<deadline && WaitForSingleObject(child.hProcess,0)==WAIT_TIMEOUT;
done:;
    DWORD error=GetLastError();
    if(child.hProcess) {
        /* Assignment failure never executes the suspended process. No PID-based kill. */
        BOOL killed=assigned?TerminateJobObject(job,ERROR_CANCELLED):TerminateProcess(child.hProcess,ERROR_CANCELLED);
        if(!killed || WaitForSingleObject(child.hProcess,5000)!=WAIT_OBJECT_0){ok=false;error=ERROR_TIMEOUT;}
        CloseHandle(child.hThread);CloseHandle(child.hProcess);
    }
    CloseHandle(job);if(!ok)SetLastError(error?error:ERROR_INVALID_DATA);return ok;
}
bool l4_child_probe(const wchar_t* exe,const wchar_t* directory,wchar_t* command,DWORD timeout,L4ChildCheck check,void* context) {
    return child_probe(NULL,exe,directory,command,timeout,check,context);
}
bool l4_child_probe_service(const L4ServiceToken* token,const wchar_t* exe,const wchar_t* directory,wchar_t* command,DWORD timeout,L4ChildCheck check,void* context) {
    if(!token){SetLastError(ERROR_INVALID_PARAMETER);return false;}
    return child_probe(token,exe,directory,command,timeout,check,context);
}
