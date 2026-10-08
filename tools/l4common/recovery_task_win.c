#define COBJMACROS
#include "recovery_task_internal.h"
#include "journal_internal.h"
#include <taskschd.h>
#include <objbase.h>
#include <sddl.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib,"taskschd.lib")
#pragma comment(lib,"oleaut32.lib")
typedef struct {ITaskService* service;ITaskFolder* folder;HANDLE file;L4FileFence parents;bool apartment;} Scheduler;
static bool fail(DWORD error){SetLastError(error);return false;}
static bool hr_ok(HRESULT hr){return SUCCEEDED(hr)?true:fail((DWORD)hr);}
static bool missing(HRESULT hr){return hr==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr==HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);}
bool l4_recovery_task_system_account(const wchar_t* user){
    if(!user || !*user || wcslen(user)>512)return fail(ERROR_ACCESS_DENIED);
    PSID parsed=NULL;
    if(ConvertStringSidToSidW(user,&parsed)){
        bool ok=IsWellKnownSid(parsed,WinLocalSystemSid)!=0;LocalFree(parsed);
        return ok?true:fail(ERROR_ACCESS_DENIED);
    }
    BYTE sid[SECURITY_MAX_SID_SIZE];wchar_t domain[256];DWORD size=sizeof(sid),length=256;SID_NAME_USE use;
    bool ok=LookupAccountNameW(NULL,user,sid,&size,domain,&length,&use) && IsValidSid(sid) && IsWellKnownSid(sid,WinLocalSystemSid);
    return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool principal(IRegisteredTask* task){
    ITaskDefinition* definition=NULL;IPrincipal* identity=NULL;BSTR user=NULL;TASK_LOGON_TYPE logon=TASK_LOGON_NONE;TASK_RUNLEVEL_TYPE level=TASK_RUNLEVEL_LUA;
    bool ok=hr_ok(IRegisteredTask_get_Definition(task,&definition)) && hr_ok(ITaskDefinition_get_Principal(definition,&identity)) &&
        hr_ok(IPrincipal_get_UserId(identity,&user)) && hr_ok(IPrincipal_get_LogonType(identity,&logon)) && hr_ok(IPrincipal_get_RunLevel(identity,&level));
    if(ok)ok=l4_recovery_task_system_account(user) && logon==TASK_LOGON_SERVICE_ACCOUNT && level==TASK_RUNLEVEL_HIGHEST;
    DWORD error=GetLastError();SysFreeString(user);if(identity)IPrincipal_Release(identity);if(definition)ITaskDefinition_Release(definition);return ok?true:fail(error?error:ERROR_ACCESS_DENIED);
}
static bool system_user(void){HANDLE token=NULL;BYTE user[512];DWORD count=0;
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,user,sizeof(user),&count) && IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid);
    if(token)CloseHandle(token);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool text_copy(BSTR value,wchar_t** result){
    *result=NULL;if(!value || SysStringLen(value)>=L4_RECOVERY_TASK_XML_LIMIT || wcslen(value)!=SysStringLen(value))return fail(ERROR_INVALID_DATA);
    *result=_wcsdup(value);return *result?true:fail(ERROR_NOT_ENOUGH_MEMORY);
}
static bool duration(const wchar_t* text,wchar_t out[32]){
    if(!text || wcslen(text)>32 || text[0]!=L'P' || text[1]!=L'T')return fail(ERROR_INVALID_DATA);
    DWORD total=0,previous=4;const wchar_t* p=text+2;bool any=false;
    while(*p){DWORD value=0,rank=0,multiplier=0;if(*p<L'0'||*p>L'9')return fail(ERROR_INVALID_DATA);
        do{value=value*10+(DWORD)(*p++-L'0');if(value>360)return fail(ERROR_INVALID_DATA);}while(*p>=L'0'&&*p<=L'9');
        if(*p==L'H'){rank=3;multiplier=3600;}else if(*p==L'M'){rank=2;multiplier=60;}else if(*p==L'S'){rank=1;multiplier=1;}else return fail(ERROR_INVALID_DATA);
        if(rank>=previous || value*multiplier>360-total)return fail(ERROR_INVALID_DATA);previous=rank;total+=value*multiplier;++p;any=true;
    }
    return any && total && swprintf_s(out,32,L"PT%luS",total)>0?true:fail(ERROR_INVALID_DATA);
}
static bool digits(const wchar_t* p,unsigned count,WORD* result){
    WORD value=0;for(unsigned i=0;i<count;i++){if(p[i]<L'0'||p[i]>L'9')return false;value=(WORD)(value*10+p[i]-L'0');}*result=value;return true;
}
static bool boundary(const wchar_t* text,wchar_t out[32]){
    size_t n=text?wcslen(text):0;SYSTEMTIME time={0};FILETIME file;WORD hours=0,minutes=0;
    if((n!=20 && n!=25) || text[4]!=L'-'||text[7]!=L'-'||text[10]!=L'T'||text[13]!=L':'||text[16]!=L':' ||
        !digits(text,4,&time.wYear)||!digits(text+5,2,&time.wMonth)||!digits(text+8,2,&time.wDay)||!digits(text+11,2,&time.wHour)||!digits(text+14,2,&time.wMinute)||!digits(text+17,2,&time.wSecond) || time.wYear<2000 || !SystemTimeToFileTime(&time,&file))return fail(ERROR_INVALID_DATA);
    LONGLONG offset=0;
    if(n==20){if(text[19]!=L'Z')return fail(ERROR_INVALID_DATA);}
    else{if((text[19]!=L'+'&&text[19]!=L'-')||text[22]!=L':'||!digits(text+20,2,&hours)||!digits(text+23,2,&minutes)||hours>14||minutes>59||(hours==14&&minutes))return fail(ERROR_INVALID_DATA);offset=((LONGLONG)hours*60+minutes)*600000000LL;if(text[19]==L'-')offset=-offset;}
    LONGLONG utc=(LONGLONG)(((ULONGLONG)file.dwHighDateTime<<32)|file.dwLowDateTime)-offset;if(utc<0)return fail(ERROR_INVALID_DATA);file.dwLowDateTime=(DWORD)utc;file.dwHighDateTime=(DWORD)((ULONGLONG)utc>>32);
    if(!FileTimeToSystemTime(&file,&time)||time.wYear<2000||time.wYear>9999)return fail(ERROR_INVALID_DATA);
    return swprintf_s(out,32,L"%04u-%02u-%02uT%02u:%02u:%02uZ",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond)>0?true:fail(ERROR_INVALID_DATA);
}
/* Scheduler registration rewrites duration/UTC spelling and inserts its SD.
 * Normalize typed COM properties; the actual private task DACL is audited too. */
static bool normalize(ITaskDefinition* definition){
    ITaskSettings* settings=NULL;ITriggerCollection* triggers=NULL;IRegistrationInfo* registration=NULL;BSTR raw=NULL,value=NULL;VARIANT policy;VariantInit(&policy);wchar_t text[32];LONG count=0;bool ok=hr_ok(ITaskDefinition_get_Settings(definition,&settings))&&hr_ok(ITaskSettings_get_ExecutionTimeLimit(settings,&raw))&&duration(raw,text);
    if(ok){value=SysAllocString(text);ok=value&&hr_ok(ITaskSettings_put_ExecutionTimeLimit(settings,value));}SysFreeString(raw);raw=NULL;SysFreeString(value);value=NULL;
    if(ok)ok=hr_ok(ITaskDefinition_get_Triggers(definition,&triggers))&&hr_ok(ITriggerCollection_get_Count(triggers,&count))&&count>=1&&count<=8;
    for(LONG i=1;ok&&i<=count;i++){ITrigger* trigger=NULL;ok=hr_ok(ITriggerCollection_get_Item(triggers,i,&trigger))&&hr_ok(ITrigger_get_StartBoundary(trigger,&raw));
        if(ok&&raw&&*raw){ok=boundary(raw,text);if(ok){value=SysAllocString(text);ok=value&&hr_ok(ITrigger_put_StartBoundary(trigger,value));}}
        SysFreeString(raw);raw=NULL;SysFreeString(value);value=NULL;if(trigger)ITrigger_Release(trigger);
    }
    if(ok)ok=hr_ok(ITaskDefinition_get_RegistrationInfo(definition,&registration))&&hr_ok(IRegistrationInfo_get_SecurityDescriptor(registration,&policy));
    if(ok&&V_VT(&policy)!=VT_EMPTY&&V_VT(&policy)!=VT_NULL)ok=V_VT(&policy)==VT_BSTR&&V_BSTR(&policy)&&l4_recovery_task_check_acl(V_BSTR(&policy),false);
    VariantClear(&policy);V_VT(&policy)=VT_BSTR;V_BSTR(&policy)=SysAllocString(L4_RECOVERY_TASK_SDDL);
    if(ok)ok=V_BSTR(&policy)&&hr_ok(IRegistrationInfo_put_SecurityDescriptor(registration,policy));
    DWORD error=GetLastError();VariantClear(&policy);if(registration)IRegistrationInfo_Release(registration);if(triggers)ITriggerCollection_Release(triggers);if(settings)ITaskSettings_Release(settings);return ok?true:fail(error?error:ERROR_INVALID_DATA);
}
static bool canonical(void* context,const wchar_t* xml,wchar_t** result){
    Scheduler* s=context;*result=NULL;if(!xml || wcslen(xml)>=L4_RECOVERY_TASK_XML_LIMIT)return fail(ERROR_INVALID_DATA);
    ITaskDefinition* definition=NULL;BSTR input=SysAllocString(xml),output=NULL;bool ok=input && hr_ok(ITaskService_NewTask(s->service,0,&definition));
    if(ok)ok=hr_ok(ITaskDefinition_put_XmlText(definition,input)) && normalize(definition) && hr_ok(ITaskDefinition_get_XmlText(definition,&output)) && text_copy(output,result);
    DWORD error=GetLastError();if(definition)ITaskDefinition_Release(definition);SysFreeString(input);SysFreeString(output);return ok?true:fail(error?error:ERROR_INVALID_DATA);
}
static bool folder(void* context,bool create){
    Scheduler* s=context;BSTR name=SysAllocString(L4_RECOVERY_TASK_FOLDER),policy=NULL;ITaskFolder* found=NULL;
    if(!name)return fail(ERROR_NOT_ENOUGH_MEMORY);HRESULT hr=ITaskService_GetFolder(s->service,name,&found);
    if(missing(hr) && create){
        ITaskFolder* root=NULL;BSTR root_name=SysAllocString(L"\\");VARIANT sd;VariantInit(&sd);V_VT(&sd)=VT_BSTR;V_BSTR(&sd)=SysAllocString(L4_RECOVERY_FOLDER_SDDL);
        if(!root_name || !V_BSTR(&sd))hr=E_OUTOFMEMORY;
        else{hr=ITaskService_GetFolder(s->service,root_name,&root);if(SUCCEEDED(hr))hr=ITaskFolder_CreateFolder(root,name,sd,&found);}
        if(root)ITaskFolder_Release(root);SysFreeString(root_name);VariantClear(&sd);
        /* A simultaneous owner create is never overwritten; audit readback. */
        if(FAILED(hr) && !found)hr=ITaskService_GetFolder(s->service,name,&found);
    }
    bool ok=hr_ok(hr);if(ok)ok=hr_ok(ITaskFolder_GetSecurityDescriptor(found,OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&policy)) &&
        policy && l4_recovery_task_check_acl(policy,true);
    DWORD error=GetLastError();SysFreeString(name);SysFreeString(policy);
    if(ok){if(s->folder)ITaskFolder_Release(s->folder);s->folder=found;}else if(found)ITaskFolder_Release(found);
    return ok?true:fail(error?error:ERROR_ACCESS_DENIED);
}
static bool read_task(void* context,const L4RecoveryTask* spec,bool* present,wchar_t** xml,wchar_t** sd){
    Scheduler* s=context;*present=false;*xml=*sd=NULL;if(!s->folder)return fail(ERROR_INVALID_STATE);
    BSTR name=SysAllocString(spec->name),raw=NULL,policy=NULL;IRegisteredTask* task=NULL;VARIANT_BOOL enabled=VARIANT_FALSE;
    if(!name)return fail(ERROR_NOT_ENOUGH_MEMORY);HRESULT hr=ITaskFolder_GetTask(s->folder,name,&task);SysFreeString(name);
    if(missing(hr))return true;bool ok=hr_ok(hr);
    if(ok){*present=true;ok=principal(task) && hr_ok(IRegisteredTask_get_Enabled(task,&enabled)) && enabled==VARIANT_TRUE && hr_ok(IRegisteredTask_get_Xml(task,&raw)) && raw &&
        SysStringLen(raw)<L4_RECOVERY_TASK_XML_LIMIT && canonical(s,raw,xml) &&
        hr_ok(IRegisteredTask_GetSecurityDescriptor(task,OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&policy)) && text_copy(policy,sd);}
    DWORD error=GetLastError();if(task)IRegisteredTask_Release(task);SysFreeString(raw);SysFreeString(policy);return ok?true:fail(error?error:ERROR_INVALID_DATA);
}
static bool create_task(void* context,const L4RecoveryTask* spec){
    Scheduler* s=context;if(!s->folder)return fail(ERROR_INVALID_STATE);
    ITaskDefinition* definition=NULL;IRegisteredTask* task=NULL;BSTR name=SysAllocString(spec->name),xml=SysAllocString(spec->xml);
    VARIANT user,password,sd;VariantInit(&user);VariantInit(&password);VariantInit(&sd);
    V_VT(&user)=VT_BSTR;V_BSTR(&user)=SysAllocString(L"S-1-5-18");V_VT(&sd)=VT_BSTR;V_BSTR(&sd)=SysAllocString(L4_RECOVERY_TASK_SDDL);
    bool ok=name && xml && V_BSTR(&user) && V_BSTR(&sd) && hr_ok(ITaskService_NewTask(s->service,0,&definition));
    if(ok)ok=hr_ok(ITaskDefinition_put_XmlText(definition,xml)) && hr_ok(ITaskFolder_RegisterTaskDefinition(s->folder,name,definition,
        TASK_CREATE|TASK_DONT_ADD_PRINCIPAL_ACE|TASK_IGNORE_REGISTRATION_TRIGGERS,user,password,TASK_LOGON_SERVICE_ACCOUNT,sd,&task));
    DWORD error=GetLastError();if(task)IRegisteredTask_Release(task);if(definition)ITaskDefinition_Release(definition);
    SysFreeString(name);SysFreeString(xml);VariantClear(&user);VariantClear(&password);VariantClear(&sd);return ok?true:fail(error?error:ERROR_NOT_ENOUGH_MEMORY);
}
static bool readonly_policy(HANDLE file){
    BYTE* sd=NULL;DWORD size;bool ok=l4_store_security(file,false,&sd,&size);PACL acl=NULL;BOOL present,defaulted;
    if(ok)ok=GetSecurityDescriptorDacl(sd,&present,&acl,&defaulted) && present && acl;
    for(WORD i=0;ok && i<acl->AceCount;i++){ACCESS_ALLOWED_ACE* ace=NULL;ok=GetAce(acl,i,(void**)&ace)!=0;if(!ok)break;
        PSID sid=&ace->SidStart;if(!IsWellKnownSid(sid,WinLocalSystemSid) && !IsWellKnownSid(sid,WinBuiltinAdministratorsSid))
            ok=!(ace->Mask&~(FILE_GENERIC_READ|FILE_GENERIC_EXECUTE|GENERIC_READ|GENERIC_EXECUTE));}
    free(sd);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool streams(const wchar_t* name){WIN32_FIND_STREAM_DATA data;HANDLE h=FindFirstStreamW(name,FindStreamInfoStandard,&data,0);
    if(h==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_HANDLE_EOF;bool ok=true;
    do{if(wcscmp(data.cStreamName,L"::$DATA")){ok=false;break;}}while(FindNextStreamW(h,&data));DWORD error=GetLastError();FindClose(h);return ok && error==ERROR_HANDLE_EOF;
}
static bool helper(void* context,const L4Layout* roots,const L4RecoveryHelper* identity){
    Scheduler* s=context;wchar_t directory[MAX_PATH],name[MAX_PATH];
    if(swprintf_s(directory,MAX_PATH,L"%ls\\recovery",roots->binaries)<0 || swprintf_s(name,MAX_PATH,L"%ls\\l4rollback.exe",directory)<0)return fail(ERROR_FILENAME_EXCED_RANGE);
    bool ok=true;if(s->file==INVALID_HANDLE_VALUE){
        ok=l4_store_pin(directory,roots->binaries,false,&s->parents) && s->parents.count>=2;
        for(unsigned i=ok?s->parents.count-2:0;ok && i<s->parents.count;i++)ok=readonly_policy(s->parents.handles[i]);
        if(ok){s->file=CreateFileW(name,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);ok=s->file!=INVALID_HANDLE_VALUE;}
    }
    BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER length={0},start={0};
    if(ok)ok=GetFileInformationByHandle(s->file,&info) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks==1 &&
        GetFileSizeEx(s->file,&length) && length.QuadPart>0 && (ULONGLONG)length.QuadPart==identity->size && readonly_policy(s->file) && streams(name) && SetFilePointerEx(s->file,start,NULL,FILE_BEGIN);
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;BYTE buffer[65536],digest[32];ULONGLONG total=0;
    if(ok)ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0 && BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)>=0;
    while(ok && total<identity->size){ULONGLONG remaining=identity->size-total;DWORD count=0,want=remaining>sizeof(buffer)?sizeof(buffer):(DWORD)remaining;
        ok=ReadFile(s->file,buffer,want,&count,NULL) && count==want && BCryptHashData(hash,buffer,count,0)>=0;total+=count;}
    if(ok)ok=BCryptFinishHash(hash,digest,32,0)>=0 && !memcmp(digest,identity->sha256,32);
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok?true:fail(ERROR_CRC);
}
/* Read-only definition parsing gate for tests: creates only an in-memory COM
 * definition. Not part of any CLI; never gets/creates a folder or task. */
bool l4_recovery_task_canonical_local(const wchar_t* xml,wchar_t** result){
    Scheduler s={0};s.file=INVALID_HANDLE_VALUE;HRESULT hr=CoInitializeEx(NULL,COINIT_MULTITHREADED);s.apartment=SUCCEEDED(hr);
    bool ok=(s.apartment || hr==RPC_E_CHANGED_MODE) && hr_ok(CoCreateInstance(&CLSID_TaskScheduler,NULL,CLSCTX_INPROC_SERVER,&IID_ITaskService,(void**)&s.service));
    VARIANT empty;VariantInit(&empty);if(ok)ok=hr_ok(ITaskService_Connect(s.service,empty,empty,empty,empty)) && canonical(&s,xml,result);
    DWORD error=GetLastError();if(s.service)ITaskService_Release(s.service);if(s.apartment)CoUninitialize();return ok?true:fail(error?error:ERROR_GEN_FAILURE);
}
static bool run(L4Journal* journal,const L4RecoveryHelper* identity,DWORD overhead,bool arm){
    if(!system_user())return false;
    Scheduler s={0};s.file=INVALID_HANDLE_VALUE;HRESULT hr=CoInitializeEx(NULL,COINIT_MULTITHREADED);s.apartment=SUCCEEDED(hr);
    bool ok=(s.apartment || hr==RPC_E_CHANGED_MODE) && hr_ok(CoCreateInstance(&CLSID_TaskScheduler,NULL,CLSCTX_INPROC_SERVER,&IID_ITaskService,(void**)&s.service));
    VARIANT empty;VariantInit(&empty);if(ok)ok=hr_ok(ITaskService_Connect(s.service,empty,empty,empty,empty));
    L4RecoveryTaskOps ops={&s,helper,folder,canonical,read_task,create_task};if(ok)ok=l4_recovery_task_run(journal,identity,overhead,arm,&ops);
    DWORD error=GetLastError();if(s.folder)ITaskFolder_Release(s.folder);if(s.service)ITaskService_Release(s.service);
    if(s.file!=INVALID_HANDLE_VALUE)CloseHandle(s.file);l4_store_unpin(&s.parents);if(s.apartment)CoUninitialize();return ok?true:fail(error?error:ERROR_GEN_FAILURE);
}
bool l4_recovery_task_arm(L4Journal* j,const L4RecoveryHelper* helper_identity,DWORD overhead){return run(j,helper_identity,overhead,true);}
bool l4_recovery_task_audit(L4Journal* j,const L4RecoveryHelper* helper_identity,DWORD overhead){return run(j,helper_identity,overhead,false);}
