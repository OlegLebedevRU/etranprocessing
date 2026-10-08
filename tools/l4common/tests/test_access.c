#include "../access.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
static unsigned checks, failures;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL %u: %s (win32=%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static bool denied(const wchar_t* path,DWORD rights,HANDLE token) {
    HANDLE saved=NULL;bool prior=OpenThreadToken(GetCurrentThread(),TOKEN_QUERY|TOKEN_IMPERSONATE,TRUE,&saved)!=0;
    if(!prior && GetLastError()!=ERROR_NO_TOKEN)return false;
    if(!SetThreadToken(NULL,token)){if(saved)CloseHandle(saved);return false;}
    HANDLE file=CreateFileW(path,rights,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    bool result=file==INVALID_HANDLE_VALUE && GetLastError()==ERROR_ACCESS_DENIED;
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    bool restored=SetThreadToken(NULL,saved)!=0;if(saved)CloseHandle(saved);
    return restored && result;
}
static void cleanup(const wchar_t* root) {
    wchar_t pattern[MAX_PATH],child[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",root);
    WIN32_FIND_DATAW entry;HANDLE find=FindFirstFileW(pattern,&entry);
    if(find!=INVALID_HANDLE_VALUE){do {
        if(!wcscmp(entry.cFileName,L".") || !wcscmp(entry.cFileName,L".."))continue;
        swprintf_s(child,MAX_PATH,L"%ls\\%ls",root,entry.cFileName);
        if(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!(entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))cleanup(child);else RemoveDirectoryW(child);}
        else DeleteFileW(child);
    }while(FindNextFileW(find,&entry));FindClose(find);}RemoveDirectoryW(root);
}
int wmain(void) {
    wchar_t temp[MAX_PATH],root[MAX_PATH],programs[MAX_PATH],data[MAX_PATH],path[MAX_PATH];
    DWORD n=GetTempPathW(MAX_PATH,temp);if(!n || n>=MAX_PATH)return 1;
    swprintf_s(root,MAX_PATH,L"%lsl4access-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());
    if(!CreateDirectoryW(root,NULL))return 1;
    swprintf_s(programs,MAX_PATH,L"%ls\\Programs",root);swprintf_s(data,MAX_PATH,L"%ls\\Data",root);
    L4Layout layout;CHECK(l4_layout_from_roots(&layout,programs,data,L"1.13.2"));CHECK(l4_layout_prepare(&layout));
    HANDLE primary=NULL,restricted=NULL,actor=NULL;
    CHECK(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE,&primary));
    BYTE admin[SECURITY_MAX_SID_SIZE];DWORD bytes=sizeof(admin);CHECK(CreateWellKnownSid(WinBuiltinAdministratorsSid,NULL,admin,&bytes));
    SID_AND_ATTRIBUTES disabled={admin,0};
    CHECK(CreateRestrictedToken(primary,DISABLE_MAX_PRIVILEGE,1,&disabled,0,NULL,0,NULL,&restricted));
    CHECK(DuplicateTokenEx(restricted,TOKEN_QUERY|TOKEN_IMPERSONATE,NULL,SecurityImpersonation,TokenImpersonation,&actor));
    BYTE user[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];
    CHECK(GetTokenInformation(actor,TokenUser,user,sizeof(user),&bytes));PSID sid=((TOKEN_USER*)user)->User.Sid;
    BYTE private_sid[SECURITY_MAX_SID_SIZE];
    CHECK(!l4_access_private_sid(actor,L"L4Con",private_sid));
    CHECK(GetLastError()==ERROR_NOT_SUPPORTED);
    CHECK(l4_access_private_sid(primary,L"L4Con",private_sid));
    CHECK(l4_layout_prepare_leaf(&layout,L"logs\\broker",NULL,sid,false));
    CHECK(l4_access_probe(&layout,L"logs\\broker",actor));
    CHECK(l4_layout_data_path(&layout,L"logs\\broker",path));CHECK(denied(path,WRITE_DAC|WRITE_OWNER,actor));
    CHECK(l4_layout_prepare_leaf(&layout,L"state\\control\\private",NULL,sid,true));
    CHECK(l4_access_probe(&layout,L"state\\control\\private",actor));
    CHECK(l4_layout_data_path(&layout,L"state\\control",path));CHECK(denied(path,FILE_DELETE_CHILD|FILE_ADD_FILE,actor));
    CHECK(!l4_access_probe(&layout,L"config",actor));CHECK(GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(l4_layout_prepare_leaf(&layout,L"config\\desktop",sid,NULL,true));
    CHECK(l4_access_read_probe(&layout,L"config\\desktop",actor));
    CHECK(!l4_access_probe(&layout,L"config\\desktop",actor));
    BYTE users[SECURITY_MAX_SID_SIZE];bytes=sizeof(users);CHECK(CreateWellKnownSid(WinBuiltinUsersSid,NULL,users,&bytes));
    CHECK(!l4_layout_prepare_leaf(&layout,L"state\\unsafe",NULL,users,false));
    BYTE local_service[SECURITY_MAX_SID_SIZE];bytes=sizeof(local_service);
    CHECK(CreateWellKnownSid(WinLocalServiceSid,NULL,local_service,&bytes));
    CHECK(l4_layout_prepare_shared_leaf(&layout,L"state\\shared",NULL,local_service,sid,false));
    CHECK(l4_access_probe(&layout,L"state\\shared",actor));
    CHECK(!l4_layout_prepare_shared_leaf(&layout,L"state\\unsafe",NULL,sid,users,false));
    CHECK(!l4_layout_prepare_leaf(&layout,L"..\\escape",NULL,sid,false));
    HANDLE saved=NULL;CHECK(!OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&saved) && GetLastError()==ERROR_NO_TOKEN);
    if(saved)CloseHandle(saved);
    CHECK(SetThreadToken(NULL,actor));CHECK(l4_access_probe(&layout,L"logs\\broker",actor));
    CHECK(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&saved));if(saved)CloseHandle(saved);
    CHECK(!l4_access_probe(&layout,L"config",actor));
    CHECK(GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(RevertToSelf());
    HANDLE service=NULL;CHECK(!l4_access_service_token(L"L4Access_Missing_Service_763184",&service));CHECK(service==NULL);
    /* Optional live evidence is explicit, never a synthetic account token. */
    wchar_t name[256];n=GetEnvironmentVariableW(L"L4_ACCESS_TEST_SERVICE",name,_countof(name));
    if(n && n<_countof(name)) {
        CHECK(l4_access_service_token(name,&service));
        if(service){CHECK(l4_access_probe(&layout,L"logs\\broker",service));CloseHandle(service);}
        printf("Actual service token gate executed\n");
    }
    HANDLE controller=NULL;
    CHECK(DuplicateTokenEx(primary,TOKEN_QUERY|TOKEN_IMPERSONATE,NULL,SecurityImpersonation,TokenImpersonation,&controller));
    L4AccessActors actors={controller,controller,controller,controller,actor};
    L4AccessActors incomplete=actors;incomplete.supervisor=NULL;
    CHECK(!l4_access_prepare(&layout,&incomplete));CHECK(GetLastError()==ERROR_NO_TOKEN);
    CHECK(l4_access_prepare(&layout,&actors));
    CHECK(l4_layout_data_path(&layout,L"state\\l4con\\fm-state",path));
    CHECK(denied(path,GENERIC_READ,actor));
    CHECK(denied(path,DELETE|WRITE_DAC|WRITE_OWNER,actor));
    CHECK(l4_layout_data_path(&layout,L"state\\l4con",path));
    CHECK(denied(path,FILE_DELETE_CHILD|FILE_ADD_FILE,actor));
    CHECK(l4_layout_data_path(&layout,L"config\\l4desk",path));
    CHECK(denied(path,FILE_ADD_FILE,actor));
    BYTE before[4096],after[4096];DWORD length_before=0,length_after=0;
    CHECK(GetFileSecurityW(path,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,before,sizeof(before),&length_before));
    CHECK(l4_access_verify(&layout,&actors));
    CHECK(GetFileSecurityW(path,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,after,sizeof(after),&length_after));
    CHECK(length_before==length_after && !memcmp(before,after,length_before));
    CHECK(l4_layout_prepare_leaf(&layout,L"state\\l4con\\fm-state",NULL,sid,true));
    CHECK(!l4_access_verify(&layout,&actors)); /* Writable tests alone must not admit exposed receipts. */
    CHECK(l4_access_prepare(&layout,&actors));
    CHECK(l4_access_probe(&layout,L"state\\l4capture",actor));
    CHECK(l4_layout_data_path(&layout,L"state\\l4capture",path));
    CHECK(denied(path,WRITE_DAC|WRITE_OWNER,actor));
    CHECK(RemoveDirectoryW(path));
    CHECK(!l4_access_verify(&layout,&actors)); /* Optional runtime cache does not waive provisioning. */
    CHECK(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES);
    CHECK(l4_access_prepare(&layout,&actors));
    CHECK(l4_layout_data_path(&layout,L"logs\\ffmpeg",path));
    CHECK(RemoveDirectoryW(path));
    CHECK(!l4_access_verify(&layout,&actors));
    CHECK(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES);
    if(controller)CloseHandle(controller);
    if(actor)CloseHandle(actor);if(restricted)CloseHandle(restricted);if(primary)CloseHandle(primary);
    cleanup(root);CHECK(GetFileAttributesW(root)==INVALID_FILE_ATTRIBUTES);
    printf("access: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
