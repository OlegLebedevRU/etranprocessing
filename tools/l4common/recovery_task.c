#include "recovery_task_internal.h"
#include "journal_internal.h"
#include <objbase.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool fail(DWORD error){SetLastError(error);return false;}
static ULONGLONG now_utc(void){FILETIME t;GetSystemTimeAsFileTime(&t);return ((ULONGLONG)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static bool escaped(const wchar_t* value,wchar_t result[MAX_PATH*5]){
    size_t at=0;for(const wchar_t* p=value;*p;p++){
        const wchar_t* entity=*p==L'&'?L"&amp;":*p==L'<'?L"&lt;":*p==L'>'?L"&gt;":NULL;
        if(*p<32)return false;size_t count=entity?wcslen(entity):1;if(at+count>=MAX_PATH*5)return false;
        if(entity){memcpy(result+at,entity,count*sizeof(wchar_t));at+=count;}else result[at++]=*p;
    }result[at]=0;return true;
}
static bool stamp(ULONGLONG utc,wchar_t text[32]){
    FILETIME ft;memcpy(&ft,&utc,8);SYSTEMTIME time;if(!FileTimeToSystemTime(&ft,&time) || time.wYear>9999)return false;
    return swprintf_s(text,32,L"%04u-%02u-%02uT%02u:%02u:%02uZ",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond)>0;
}
bool l4_recovery_task_spec(const L4Layout* roots,const L4RecoveryPlan* plan,const L4RecoveryHelper* helper,DWORD overhead,L4RecoveryTask* result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);memset(result,0,sizeof(*result));
    if(!roots || !plan || !helper || !helper->size || helper->size>MAXDWORD || overhead<L4_RECOVERY_TASK_MIN_OVERHEAD_MS || overhead>60000)return fail(ERROR_INVALID_PARAMETER);
    BYTE nonzero=0;for(unsigned i=0;i<32;i++)nonzero|=helper->sha256[i];if(!nonzero)return fail(ERROR_INVALID_DATA);
    BYTE* encoded=NULL;DWORD size=0;if(!l4_recovery_encode(roots,plan,&encoded,&size))return false;
    wchar_t guid[40],digest[65],helper_digest[65],image[MAX_PATH],directory[MAX_PATH],image_xml[MAX_PATH*5],directory_xml[MAX_PATH*5],boundary[32],armed[32];
    bool ok=StringFromGUID2(&plan->operation,guid,40)==39;
    L4RecoveryTask task={0};if(ok){wcsncpy_s(task.operation,40,guid+1,36);for(unsigned i=0;i<36;i++)if(task.operation[i]>=L'A' && task.operation[i]<=L'F')task.operation[i]+=32;}
    if(ok)ok=swprintf_s(task.name,64,L"Supervisor.%ls",task.operation)>0 && swprintf_s(directory,MAX_PATH,L"%ls\\recovery",roots->binaries)>0 &&
        swprintf_s(image,MAX_PATH,L"%ls\\l4rollback.exe",directory)>0 && escaped(image,image_xml) && escaped(directory,directory_xml);
    if(ok)ok=swprintf_s(task.path,MAX_PATH,L"%ls\\%ls",L4_RECOVERY_TASK_FOLDER,task.name)>0;
    /* Never schedule before a fractional FILETIME deadline. */
    ULONGLONG seconds=plan->deadline_utc/10000000ull+(plan->deadline_utc%10000000ull?1:0);
    if(ok)ok=seconds<=~0ull/10000000ull && stamp(seconds*10000000ull,boundary) && stamp(plan->armed_utc,armed);
    task.deadline_utc=seconds*10000000ull;task.execution_seconds=(plan->recovery_ms+overhead+999)/1000;
    for(unsigned i=0;i<32;i++){swprintf_s(digest+2*i,65-2*i,L"%02x",encoded[size-32+i]);swprintf_s(helper_digest+2*i,65-2*i,L"%02x",helper->sha256[i]);}
    if(ok)ok=swprintf_s(task.xml,L4_RECOVERY_TASK_XML_LIMIT,
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?><Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
        L"<RegistrationInfo><Date>%ls</Date><Author>Leo4 Tools</Author><Description>L4 supervisor recovery plan SHA256 %ls; bootstrap SHA256 %ls bytes %llu</Description><URI>%ls</URI></RegistrationInfo>"
        L"<Triggers><TimeTrigger id=\"Deadline\"><StartBoundary>%ls</StartBoundary><Enabled>true</Enabled></TimeTrigger><BootTrigger id=\"Boot\"><Enabled>true</Enabled></BootTrigger></Triggers>"
        L"<Principals><Principal id=\"System\"><UserId>S-1-5-18</UserId><RunLevel>HighestAvailable</RunLevel></Principal></Principals>"
        L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
        L"<AllowHardTerminate>true</AllowHardTerminate><StartWhenAvailable>true</StartWhenAvailable><RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>"
        L"<IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings><AllowStartOnDemand>false</AllowStartOnDemand><Enabled>true</Enabled>"
        L"<Hidden>false</Hidden><RunOnlyIfIdle>false</RunOnlyIfIdle><WakeToRun>true</WakeToRun><ExecutionTimeLimit>PT%luS</ExecutionTimeLimit><Priority>7</Priority></Settings>"
        L"<Actions Context=\"System\"><Exec><Command>%ls</Command><Arguments>--operation %ls --boot</Arguments><WorkingDirectory>%ls</WorkingDirectory></Exec></Actions></Task>",
        armed,digest,helper_digest,helper->size,task.path,boundary,task.execution_seconds,image_xml,task.operation,directory_xml)>0;
    free(encoded);if(!ok)return fail(ERROR_INVALID_DATA);*result=task;return true;
}
bool l4_recovery_task_check_acl(const wchar_t* text,bool folder){
    if(!text || wcslen(text)>4096)return fail(ERROR_INVALID_SECURITY_DESCR);
    PSECURITY_DESCRIPTOR sd=NULL;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(text,SDDL_REVISION_1,&sd,NULL))return false;
    PSID owner=NULL,group=NULL;PACL acl=NULL;BOOL present,defaulted;SECURITY_DESCRIPTOR_CONTROL control=0;DWORD revision;
    bool ok=GetSecurityDescriptorOwner(sd,&owner,&defaulted) && owner && IsWellKnownSid(owner,WinLocalSystemSid) &&
        GetSecurityDescriptorGroup(sd,&group,&defaulted) && group && IsWellKnownSid(group,WinLocalSystemSid) && GetSecurityDescriptorControl(sd,&control,&revision) &&
        (control&SE_DACL_PROTECTED) && GetSecurityDescriptorDacl(sd,&present,&acl,&defaulted) && present && acl && acl->AceCount==2;
    bool system=false,admin=false;
    for(WORD i=0;ok && i<acl->AceCount;i++){ACCESS_ALLOWED_ACE* ace=NULL;ok=GetAce(acl,i,(void**)&ace) && ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE &&
        ace->Mask==FILE_ALL_ACCESS && ace->Header.AceFlags==(folder?(OBJECT_INHERIT_ACE|CONTAINER_INHERIT_ACE):0);if(!ok)break;
        PSID sid=&ace->SidStart;if(IsWellKnownSid(sid,WinLocalSystemSid) && !system)system=true;else if(IsWellKnownSid(sid,WinBuiltinAdministratorsSid) && !admin)admin=true;else ok=false;
    }LocalFree(sd);return ok && system && admin?true:fail(ERROR_ACCESS_DENIED);
}
static bool waiting(L4RecoveryGuard* guard,ULONGLONG now){L4RecoveryAction action;return l4_recovery_action(guard,now,false,&action) && action==L4_RECOVERY_WAIT?true:fail(ERROR_INVALID_STATE);}
bool l4_recovery_task_run(L4Journal* j,const L4RecoveryHelper* helper,DWORD overhead,bool arm,const L4RecoveryTaskOps* ops){
    if(!j || j->poisoned || !j->lock || j->lock==INVALID_HANDLE_VALUE || !ops || !ops->helper || !ops->folder || !ops->canonical || !ops->read || !ops->create)return fail(ERROR_INVALID_PARAMETER);
    wchar_t guid[40],operation[40];GUID id;memcpy(&id,j->header+8,16);
    if(StringFromGUID2(&id,guid,40)!=39)return fail(ERROR_INVALID_DATA);wcsncpy_s(operation,40,guid+1,36);
    L4RecoveryGuard* guard=NULL;if(!l4_recovery_open(&j->layout,operation,1000,&guard))return false;
    L4RecoveryTask task;bool ok=waiting(guard,now_utc()) && l4_recovery_task_spec(&j->layout,l4_recovery_plan(guard),helper,overhead,&task);
    l4_recovery_release(guard); /* COM can block; helper must be free to decide. */
    wchar_t *expected=NULL,*actual=NULL,*sd=NULL;bool present=false;
    if(ok)ok=ops->helper(ops->context,&j->layout,helper) && ops->canonical(ops->context,task.xml,&expected) && expected && *expected &&
        wcslen(expected)<L4_RECOVERY_TASK_XML_LIMIT && ops->folder(ops->context,arm) && ops->read(ops->context,&task,&present,&actual,&sd);
    if(ok && !present){
        free(actual);actual=NULL;free(sd);sd=NULL;
        if(!arm)ok=fail(ERROR_FILE_NOT_FOUND);
        else{ok=l4_recovery_relock(guard,1000) && waiting(guard,now_utc());l4_recovery_release(guard);
            if(ok)ok=l4_journal_append(j,67,task.xml,(DWORD)wcslen(task.xml)*2,NULL) && ops->create(ops->context,&task) && ops->read(ops->context,&task,&present,&actual,&sd);}
    }
    if(ok){if(!present || !actual || wcscmp(expected,actual))ok=fail(ERROR_REVISION_MISMATCH);else ok=l4_recovery_task_check_acl(sd,false);}
    if(!ok && !GetLastError())SetLastError(ERROR_REVISION_MISMATCH);
    if(ok){free(actual);actual=NULL;free(sd);sd=NULL;
        ok=ops->helper(ops->context,&j->layout,helper) && ops->folder(ops->context,false) && ops->read(ops->context,&task,&present,&actual,&sd);
        if(ok){if(!present || !actual || wcscmp(expected,actual))ok=fail(ERROR_REVISION_MISMATCH);else ok=l4_recovery_task_check_acl(sd,false);}
        if(ok)ok=l4_recovery_relock(guard,1000) && waiting(guard,now_utc());
    }
    DWORD error=GetLastError();free(expected);free(actual);free(sd);l4_recovery_close(guard);return ok?true:fail(error?error:ERROR_REVISION_MISMATCH);
}
