#include "access.h"
#include <sddl.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static bool failure(DWORD code) { SetLastError(code); return false; }

static bool account_sid(const wchar_t* account, BYTE sid[SECURITY_MAX_SID_SIZE]) {
    DWORD size = SECURITY_MAX_SID_SIZE;
    if (!_wcsicmp(account,L"LocalSystem")) return CreateWellKnownSid(WinLocalSystemSid,NULL,sid,&size) != 0;
    wchar_t domain[256]; DWORD count = _countof(domain); SID_NAME_USE use;
    return LookupAccountNameW(NULL,account,sid,&size,domain,&count,&use) != 0;
}

/* Private thread privilege scope, never process-wide AdjustTokenPrivileges. */
static bool debug_scope(HANDLE* saved) {
    HANDLE source=NULL, temporary=NULL; *saved=NULL;
    if (OpenThreadToken(GetCurrentThread(),TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_IMPERSONATE,TRUE,saved)) source=*saved;
    else {
        if (GetLastError()!=ERROR_NO_TOKEN || !OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE,&source)) return false;
    }
    bool ok=DuplicateTokenEx(source,TOKEN_QUERY|TOKEN_ADJUST_PRIVILEGES|TOKEN_IMPERSONATE,NULL,
        SecurityImpersonation,TokenImpersonation,&temporary)!=0;
    TOKEN_PRIVILEGES privilege={0}; privilege.PrivilegeCount=1;
    privilege.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
    if(ok)ok=LookupPrivilegeValueW(NULL,L"SeDebugPrivilege",&privilege.Privileges[0].Luid)!=0;
    if(ok) {
        SetLastError(ERROR_SUCCESS);
        ok=AdjustTokenPrivileges(temporary,FALSE,&privilege,0,NULL,NULL) && GetLastError()==ERROR_SUCCESS;
    }
    if(ok)ok=SetThreadToken(NULL,temporary)!=0;
    DWORD code=GetLastError();
    if(temporary)CloseHandle(temporary);
    if(source!=*saved)CloseHandle(source);
    if(!ok && *saved){CloseHandle(*saved);*saved=NULL;}
    return ok ? true : failure(code);
}

bool l4_access_service_token(const wchar_t* name, HANDLE* token) {
    if(!name || !*name || !token)return failure(ERROR_INVALID_PARAMETER);
    *token=NULL; L4ServiceInventory inventory;
    if(!l4_service_inventory(name,&inventory))return false;
    if(!inventory.installed)return failure(ERROR_SERVICE_DOES_NOT_EXIST);
    BYTE expected[SECURITY_MAX_SID_SIZE];
    if(!account_sid(inventory.account,expected))return false;
    SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);
    if(!manager)return false;
    SC_HANDLE service=OpenServiceW(manager,name,SERVICE_QUERY_STATUS);
    DWORD code=GetLastError();CloseServiceHandle(manager);
    if(!service)return failure(code);
    SERVICE_STATUS_PROCESS status={0};DWORD bytes;
    bool ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(LPBYTE)&status,sizeof(status),&bytes)!=0;
    if(ok && (status.dwCurrentState!=SERVICE_RUNNING || !status.dwProcessId)) {ok=false;SetLastError(ERROR_SERVICE_NOT_ACTIVE);}
    HANDLE saved=NULL,process=NULL,primary=NULL;
    if(ok)ok=debug_scope(&saved);
    bool scoped=ok;
    if(ok){process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,status.dwProcessId);ok=process!=NULL;}
    if(ok)ok=OpenProcessToken(process,TOKEN_QUERY|TOKEN_DUPLICATE,&primary)!=0;
    BYTE user_buffer[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];
    if(ok)ok=GetTokenInformation(primary,TokenUser,user_buffer,sizeof(user_buffer),&bytes)!=0;
    if(ok && !EqualSid(expected,((TOKEN_USER*)user_buffer)->User.Sid)) {ok=false;SetLastError(ERROR_ACCESS_DENIED);}
    SERVICE_STATUS_PROCESS current={0};
    if(ok)ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(LPBYTE)&current,sizeof(current),&bytes)!=0;
    if(ok && (current.dwCurrentState!=SERVICE_RUNNING || current.dwProcessId!=status.dwProcessId)) {ok=false;SetLastError(ERROR_RETRY);}
    L4ServiceInventory after={0};
    if(ok)ok=l4_service_inventory(name,&after);
    if(ok && (!after.installed || _wcsicmp(after.account,inventory.account) || wcscmp(after.image_path,inventory.image_path))) {ok=false;SetLastError(ERROR_RETRY);}
    DWORD exit_code=0;
    if(ok)ok=GetExitCodeProcess(process,&exit_code)!=0;
    if(ok && exit_code!=STILL_ACTIVE){ok=false;SetLastError(ERROR_RETRY);}
    if(ok)ok=DuplicateTokenEx(primary,TOKEN_QUERY|TOKEN_IMPERSONATE,NULL,SecurityImpersonation,TokenImpersonation,token)!=0;
    code=GetLastError();
    if(primary)CloseHandle(primary);if(process)CloseHandle(process);
    if(scoped && !SetThreadToken(NULL,saved)){ok=false;code=GetLastError();}
    if(saved)CloseHandle(saved);CloseServiceHandle(service);
    if(!ok && *token){CloseHandle(*token);*token=NULL;}
    return ok ? true : failure(code);
}

bool l4_access_private_sid(HANDLE token,const wchar_t* service,BYTE sid[SECURITY_MAX_SID_SIZE]) {
    if(!token || !service || !*service || !sid)return failure(ERROR_INVALID_PARAMETER);
    BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];DWORD size;
    if(!GetTokenInformation(token,TokenUser,user,sizeof(user),&size))return false;
    if(IsWellKnownSid(((TOKEN_USER*)user)->User.Sid,WinLocalSystemSid)) {
        size=SECURITY_MAX_SID_SIZE;return CreateWellKnownSid(WinLocalSystemSid,NULL,sid,&size)!=0;
    }
    size=0;GetTokenInformation(token,TokenGroups,NULL,0,&size);
    if(!size || size>65536)return failure(ERROR_INVALID_DATA);
    TOKEN_GROUPS* groups=(TOKEN_GROUPS*)malloc(size);
    if(!groups)return failure(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=GetTokenInformation(token,TokenGroups,groups,size,&size)!=0,found=false;
    BYTE candidate[SECURITY_MAX_SID_SIZE];wchar_t name[256];
    if(wcslen(service)>240){free(groups);return failure(ERROR_INVALID_NAME);}
    swprintf_s(name,_countof(name),L"NT SERVICE\\%ls",service);
    bool resolved=account_sid(name,candidate);
    for(DWORD i=0;ok && i<groups->GroupCount;i++) {
        SID_AND_ATTRIBUTES* group=&groups->Groups[i];
        if((group->Attributes&(SE_GROUP_ENABLED|SE_GROUP_OWNER|SE_GROUP_USE_FOR_DENY_ONLY))!=(SE_GROUP_ENABLED|SE_GROUP_OWNER))continue;
        if(IsWellKnownSid(group->Sid,WinBuiltinAdministratorsSid) || (resolved && EqualSid(candidate,group->Sid))) {
            ok=CopySid(SECURITY_MAX_SID_SIZE,sid,group->Sid)!=0;found=ok;break;
        }
    }
    DWORD code=GetLastError();free(groups);
    return ok && found ? true : failure(ok?ERROR_NOT_SUPPORTED:code);
}

bool l4_access_private_descriptor(const wchar_t* service,bool directory,PSECURITY_DESCRIPTOR* descriptor) {
    if(!descriptor)return failure(ERROR_INVALID_PARAMETER);*descriptor=NULL;
    HANDLE token=NULL;BYTE sid[SECURITY_MAX_SID_SIZE];LPWSTR text=NULL;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;
    bool ok=l4_access_private_sid(token,service,sid);DWORD code=GetLastError();CloseHandle(token);
    if(!ok)return failure(code);
    if(!ConvertSidToStringSidW(sid,&text))return false;
    wchar_t sddl[1024];const wchar_t* inherit=directory?L"OICI":L"";
    swprintf_s(sddl,_countof(sddl),L"O:%lsD:P(A;%ls;FA;;;SY)(A;%ls;FA;;;BA)(A;%ls;0x1301bf;;;%ls)",text,inherit,inherit,inherit,text);
    LocalFree(text);
    return ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,descriptor,NULL)!=0;
}

static bool probe(const L4Layout* layout,const wchar_t* relative,HANDLE token,bool writable) {
    wchar_t directory[MAX_PATH], first[MAX_PATH], second[MAX_PATH];
    if(!token || !l4_layout_data_path(layout,relative,directory))return failure(ERROR_INVALID_PARAMETER);
    if(wcslen(directory)+56>=MAX_PATH)return failure(ERROR_FILENAME_EXCED_RANGE);
    GUID guid;if(FAILED(CoCreateGuid(&guid)))return failure(ERROR_GEN_FAILURE);
    wchar_t suffix[48];if(!StringFromGUID2(&guid,suffix,_countof(suffix)))return failure(ERROR_GEN_FAILURE);
    swprintf_s(first,MAX_PATH,L"%ls\\.l4acl-%ls.tmp",directory,suffix);
    swprintf_s(second,MAX_PATH,L"%ls\\.l4acl-%ls.done",directory,suffix);
    /* Hold all ancestors before impersonating, so rename/junction substitution
     * cannot redirect the probe or its cleanup into another directory. */
    HANDLE held[MAX_PATH/2];unsigned count=0; bool ok=true;DWORD code=ERROR_SUCCESS;
    wchar_t prefix[MAX_PATH];wcscpy_s(prefix,MAX_PATH,directory);
    for(wchar_t* p=prefix+3;;++p)if(!*p || *p==L'\\') {
        wchar_t saved=*p;*p=0;
        HANDLE h=CreateFileW(prefix,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,
            OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        BY_HANDLE_FILE_INFORMATION info;
        if(h==INVALID_HANDLE_VALUE){ok=false;code=GetLastError();}
        else if(!GetFileInformationByHandle(h,&info)){ok=false;code=GetLastError();}
        else if(!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){ok=false;code=ERROR_ACCESS_DENIED;}
        if(h!=INVALID_HANDLE_VALUE)held[count++]=h;
        *p=saved;if(!ok || !saved)break;
    }
    HANDLE previous=NULL,file=INVALID_HANDLE_VALUE;bool impersonated=false,created=false,renamed=false;
    if(ok && !writable) {
        file=CreateFileW(first,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY,NULL);
        ok=file!=INVALID_HANDLE_VALUE;created=ok;
        if(ok){DWORD bytes;BYTE value=0x4c;ok=WriteFile(file,&value,1,&bytes,NULL) && bytes==1 && FlushFileBuffers(file);}
        if(!ok)code=GetLastError();
        if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);file=INVALID_HANDLE_VALUE;}
    }
    if(ok && !OpenThreadToken(GetCurrentThread(),TOKEN_QUERY|TOKEN_IMPERSONATE,TRUE,&previous) && GetLastError()!=ERROR_NO_TOKEN){ok=false;code=GetLastError();}
    if(ok){ok=SetThreadToken(NULL,token)!=0;impersonated=ok;if(!ok)code=GetLastError();}
    if(ok){file=CreateFileW(first,writable?(GENERIC_READ|GENERIC_WRITE):GENERIC_READ,0,NULL,writable?CREATE_NEW:OPEN_EXISTING,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_OPEN_REPARSE_POINT,NULL);ok=file!=INVALID_HANDLE_VALUE;if(writable)created=ok;if(!ok)code=GetLastError();}
    DWORD bytes=0;BYTE written=0x4c,read=0;
    if(ok && writable && !(WriteFile(file,&written,1,&bytes,NULL) && bytes==1 && FlushFileBuffers(file))){ok=false;code=GetLastError();}
    if(ok){LARGE_INTEGER zero={0};if(!(SetFilePointerEx(file,zero,NULL,FILE_BEGIN) && ReadFile(file,&read,1,&bytes,NULL) && bytes==1 && read==written)){ok=false;code=GetLastError();}}
    if(ok && writable && !(WriteFile(file,&written,1,&bytes,NULL) && bytes==1 && FlushFileBuffers(file))){ok=false;code=GetLastError();}
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(ok && writable){ok=MoveFileExW(first,second,0)!=0;renamed=ok;if(!ok)code=GetLastError();}
    if(ok && writable && !DeleteFileW(second)){ok=false;code=GetLastError();}
    bool restored=!impersonated || SetThreadToken(NULL,previous)!=0;
    if(!restored){ok=false;code=GetLastError();}
    if(previous)CloseHandle(previous);
    /* Controller cleanup cannot make a failed actor probe pass. */
    if(restored && created && (!ok || !writable) && !DeleteFileW(renamed?second:first) && GetLastError()!=ERROR_FILE_NOT_FOUND){ok=false;code=GetLastError();}
    if(!ok && code==ERROR_SUCCESS)code=ERROR_INVALID_DATA;
    while(count)CloseHandle(held[--count]);
    return ok ? true : failure(code);
}

bool l4_access_probe(const L4Layout* layout,const wchar_t* relative,HANDLE token) {
    return probe(layout,relative,token,true);
}
bool l4_access_read_probe(const L4Layout* layout,const wchar_t* relative,HANDLE token) {
    return probe(layout,relative,token,false);
}
static bool denied_access(const wchar_t* path,DWORD rights,HANDLE token) {
    HANDLE previous=NULL;
    if(!OpenThreadToken(GetCurrentThread(),TOKEN_QUERY|TOKEN_IMPERSONATE,TRUE,&previous) && GetLastError()!=ERROR_NO_TOKEN)return false;
    if(!SetThreadToken(NULL,token)){DWORD code=GetLastError();if(previous)CloseHandle(previous);return failure(code);}
    HANDLE file=CreateFileW(path,rights,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    bool denied=file==INVALID_HANDLE_VALUE && GetLastError()==ERROR_ACCESS_DENIED;
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    bool restored=SetThreadToken(NULL,previous)!=0;DWORD code=GetLastError();
    if(previous)CloseHandle(previous);
    return restored && denied ? true : failure(restored?ERROR_ACCESS_DENIED:code);
}
static bool matrix(const L4Layout* layout,const L4AccessActors* actors,bool prepare) {
    if(!layout || !actors || !actors->proxy || !actors->broker || !actors->console ||
        !actors->supervisor || !actors->desktop)return failure(ERROR_NO_TOKEN);
    HANDLE tokens[]={actors->proxy,actors->broker,actors->console,actors->supervisor};
    const wchar_t* names[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};
    BYTE sid[5][SECURITY_MAX_SID_SIZE];DWORD size;
    for(unsigned i=0;i<4;i++)if(!l4_access_private_sid(tokens[i],names[i],sid[i]))return false;
    BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];
    if(!GetTokenInformation(actors->desktop,TokenUser,user,sizeof(user),&size) ||
        !CopySid(SECURITY_MAX_SID_SIZE,sid[4],((TOKEN_USER*)user)->User.Sid))return false;
    if(prepare && !l4_layout_prepare(layout))return false;
    struct Leaf { const wchar_t* path; int reader,writer,second_writer; bool private_leaf; };
    const struct Leaf leaves[]={
        {L"config",-1,3,-1,false},{L"config\\leo4proxy",0,3,-1,true},{L"config\\mosquitto",1,3,-1,true},{L"config\\l4desk",4,3,-1,true},
        {L"state",-1,3,-1,false},{L"state\\leo4proxy",-1,0,-1,true},{L"state\\mosquitto",-1,1,-1,true},
        {L"state\\l4con\\work",-1,4,2,false},{L"state\\l4con\\fm",-1,4,2,false},
        {L"state\\l4con\\fm-state",-1,2,-1,true},{L"state\\l4desk",3,4,-1,false},
        {L"state\\l4capture",2,4,-1,false},
        {L"logs\\leo4proxy",-1,0,-1,false},{L"logs\\mosquitto",-1,1,-1,false},{L"logs\\l4con",-1,2,-1,false},
        {L"logs\\l4superv",-1,3,-1,false},{L"logs\\l4desk",-1,4,-1,false},{L"logs\\l4capture",-1,4,2,false},{L"logs\\ffmpeg",-1,4,-1,false}
    };
    HANDLE all[]={actors->proxy,actors->broker,actors->console,actors->supervisor,actors->desktop};
    for(unsigned i=0;i<_countof(leaves);i++) {
        const struct Leaf* leaf=&leaves[i];
        if(prepare && !l4_layout_prepare_shared_leaf(layout,leaf->path,leaf->reader<0?NULL:sid[leaf->reader],
            leaf->writer<0?NULL:sid[leaf->writer],leaf->second_writer<0?NULL:sid[leaf->second_writer],leaf->private_leaf))return false;
        if(leaf->reader>=0 && !l4_access_read_probe(layout,leaf->path,all[leaf->reader]))return false;
        if(leaf->second_writer>=0 && !l4_access_probe(layout,leaf->path,all[leaf->second_writer]))return false;
        if(leaf->writer>=0 && !l4_access_probe(layout,leaf->path,all[leaf->writer]))return false;
    }
    /* An administrative operator can recover the installation. An ordinary
     * desktop token must neither read private receipts nor replace their parent. */
    BYTE privileged[SECURITY_MAX_SID_SIZE];
    if(!l4_access_private_sid(actors->desktop,L"L4Desk",privileged)) {
        if(GetLastError()!=ERROR_NOT_SUPPORTED)return false;
        wchar_t path[MAX_PATH];
        if(!l4_layout_data_path(layout,L"state\\l4con\\fm-state",path) ||
            !denied_access(path,GENERIC_READ|GENERIC_WRITE,actors->desktop))return false;
        /* Test each right separately: denial of a combined request proves only
         * that at least one right was denied. */
        if(!denied_access(path,GENERIC_READ,actors->desktop) ||
            !denied_access(path,GENERIC_WRITE,actors->desktop) ||
            !denied_access(path,DELETE,actors->desktop) ||
            !denied_access(path,WRITE_DAC,actors->desktop) ||
            !denied_access(path,WRITE_OWNER,actors->desktop))return false;
        if(!l4_layout_data_path(layout,L"state\\l4con",path) ||
            !denied_access(path,FILE_DELETE_CHILD,actors->desktop) ||
            !denied_access(path,FILE_ADD_SUBDIRECTORY,actors->desktop))return false;
        if(!denied_access(layout->binaries,FILE_ADD_FILE,actors->desktop) ||
            !denied_access(layout->binaries,FILE_ADD_SUBDIRECTORY,actors->desktop) ||
            !denied_access(layout->binaries,FILE_DELETE_CHILD,actors->desktop))return false;
    }
    return true;
}

bool l4_access_prepare(const L4Layout* layout,const L4AccessActors* actors) {
    return matrix(layout,actors,true);
}
bool l4_access_verify(const L4Layout* layout,const L4AccessActors* actors) {
    return matrix(layout,actors,false);
}
