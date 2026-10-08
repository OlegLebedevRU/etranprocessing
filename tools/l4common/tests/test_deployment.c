#include "../service_switch.h"
#include <bcrypt.h>
#include <sddl.h>
#include <aclapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deployment_fixture.h"
static unsigned checks, failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line %u: %s (win32=%lu)\n",__LINE__,#x,GetLastError());}}while(0)

/* SCM mutation tests use this in-process fake. No real service is changed. */
static L4ServiceInventory fake_config;
static DWORD fake_state=SERVICE_STOPPED;
static unsigned writes;
static bool reject_write,query_failure_after_write,changed_account_after_write;
static SC_HANDLE WINAPI mock_manager(LPCWSTR a,LPCWSTR b,DWORD rights){(void)a;(void)b;CHECK(rights==SC_MANAGER_CONNECT);return (SC_HANDLE)(ULONG_PTR)1;}
static SC_HANDLE WINAPI mock_service(SC_HANDLE h,LPCWSTR name,DWORD rights){(void)h;CHECK(!_wcsicmp(name,L"L4Con"));CHECK(rights==(SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_CHANGE_CONFIG));return (SC_HANDLE)(ULONG_PTR)2;}
static BOOL WINAPI mock_close(SC_HANDLE h){(void)h;return TRUE;}
static BOOL WINAPI mock_config(SC_HANDLE h,LPQUERY_SERVICE_CONFIGW cfg,DWORD size,LPDWORD required){
    (void)h;
    if(query_failure_after_write && writes){query_failure_after_write=false;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    *required=sizeof(*cfg);
    if(!cfg || size<sizeof(*cfg)){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    memset(cfg,0,sizeof(*cfg));cfg->lpBinaryPathName=fake_config.image_path;cfg->lpServiceStartName=fake_config.account;cfg->dwStartType=fake_config.start_type;return TRUE;
}
static BOOL WINAPI mock_status(SC_HANDLE h,SC_STATUS_TYPE type,LPBYTE buffer,DWORD size,LPDWORD required){
    (void)h;(void)type;CHECK(size>=sizeof(SERVICE_STATUS_PROCESS));SERVICE_STATUS_PROCESS* status=(SERVICE_STATUS_PROCESS*)buffer;
    memset(status,0,sizeof(*status));status->dwCurrentState=fake_state;*required=sizeof(*status);return TRUE;
}
static BOOL WINAPI mock_change(SC_HANDLE h,DWORD type,DWORD start,DWORD error,LPCWSTR image,LPCWSTR group,LPDWORD tag,LPCWSTR dependencies,LPCWSTR account,LPCWSTR password,LPCWSTR display){
    (void)h;CHECK(type==SERVICE_NO_CHANGE && start==SERVICE_NO_CHANGE && error==SERVICE_NO_CHANGE);
    CHECK(!group && !tag && !dependencies && !account && !password && !display);
    if(reject_write){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    wcscpy_s(fake_config.image_path,_countof(fake_config.image_path),image);++writes;
    if(changed_account_after_write)wcscpy_s(fake_config.account,_countof(fake_config.account),L"ChangedByOperator");
    return TRUE;
}
#define OpenSCManagerW mock_manager
#define OpenServiceW mock_service
#define CloseServiceHandle mock_close
#define QueryServiceConfigW mock_config
#define QueryServiceStatusEx mock_status
#define ChangeServiceConfigW mock_change
#include "../service_switch.c"
#undef OpenSCManagerW
#undef OpenServiceW
#undef CloseServiceHandle
#undef QueryServiceConfigW
#undef QueryServiceStatusEx
#undef ChangeServiceConfigW

static bool fixture_hash(const void* bytes,DWORD size,BYTE hash[32]){
    BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE object=NULL;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0;
    if(ok)ok=BCryptCreateHash(algorithm,&object,NULL,0,NULL,0,0)>=0;
    if(ok)ok=BCryptHashData(object,(PUCHAR)bytes,size,0)>=0;
    if(ok)ok=BCryptFinishHash(object,hash,32,0)>=0;
    if(object)BCryptDestroyHash(object);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}
static bool fixture_write(const wchar_t* path,const void* bytes,DWORD size){
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=WriteFile(file,bytes,size,&written,NULL)!=0 && written==size && FlushFileBuffers(file);CloseHandle(file);return ok;
}
static void fixture_cleanup(const wchar_t* root){
    wchar_t pattern[MAX_PATH],child[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",root);WIN32_FIND_DATAW data;HANDLE search=FindFirstFileW(pattern,&data);
    if(search!=INVALID_HANDLE_VALUE){do{
        if(!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L".."))continue;
        swprintf_s(child,MAX_PATH,L"%ls\\%ls",root,data.cFileName);
        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))fixture_cleanup(child);else RemoveDirectoryW(child);}
        else DeleteFileW(child);
    }while(FindNextFileW(search,&data));FindClose(search);}RemoveDirectoryW(root);
}
static bool ordinary_access(const wchar_t* path,DWORD rights){
    HANDLE process=NULL,limited=NULL,actor=NULL;BYTE sid[SECURITY_MAX_SID_SIZE];DWORD size=sizeof(sid);
    if(!CreateWellKnownSid(WinBuiltinAdministratorsSid,NULL,sid,&size))return true;
    SID_AND_ATTRIBUTES disabled={sid,0};
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE,&process)!=0;
    if(ok)ok=CreateRestrictedToken(process,DISABLE_MAX_PRIVILEGE,1,&disabled,0,NULL,0,NULL,&limited)!=0;
    if(ok)ok=DuplicateTokenEx(limited,TOKEN_QUERY|TOKEN_IMPERSONATE,NULL,SecurityImpersonation,TokenImpersonation,&actor)!=0;
    if(ok)ok=SetThreadToken(NULL,actor)!=0;
    HANDLE file=INVALID_HANDLE_VALUE;if(ok)file=CreateFileW(path,rights,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    bool allowed=file!=INVALID_HANDLE_VALUE;if(allowed)CloseHandle(file);
    bool restored=RevertToSelf()!=0;if(actor)CloseHandle(actor);if(limited)CloseHandle(limited);if(process)CloseHandle(process);
    return !ok || !restored || allowed;
}
static unsigned admission_calls;
static bool admission_allowed;
static bool fixture_admission(HANDLE file,const wchar_t* path,void* context){
    CHECK(context==&admission_allowed);++admission_calls;char bytes[3];DWORD read=0;
    CHECK(ReadFile(file,bytes,3,&read,NULL) && read==3 && !memcmp(bytes,"new",3));
    CHECK(!fixture_write(path,"bad",3)); /* Checked file cannot be rewritten during admission. */
    if(!admission_allowed){SetLastError(ERROR_ACCESS_DENIED);return false;}return true;
}
int wmain(void){
    wchar_t temp[MAX_PATH],root[MAX_PATH],programs[MAX_PATH],data[MAX_PATH],expanded[MAX_PATH],source[MAX_PATH],archive[MAX_PATH],target[MAX_PATH],other[MAX_PATH];
    DWORD n=GetTempPathW(MAX_PATH,temp);if(!n || n>=MAX_PATH)return 1;
    swprintf_s(root,MAX_PATH,L"%lsl4deployment-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());if(!CreateDirectoryW(root,NULL))return 1;
    swprintf_s(programs,MAX_PATH,L"%ls\\Programs",root);swprintf_s(data,MAX_PATH,L"%ls\\Data",root);
    L4Layout layout,previous;CHECK(l4_layout_from_roots(&layout,programs,data,L"1.13.2"));CHECK(l4_layout_prepare(&layout));
    CHECK(GetFileAttributesW(layout.release)==INVALID_FILE_ATTRIBUTES);
    CHECK(l4_layout_prepare_leaf(&layout,L"update\\staging\\expanded\\l4con",NULL,NULL,false));
    CHECK(l4_layout_data_path(&layout,L"update\\staging\\expanded",expanded));
    CHECK(l4_layout_data_path(&layout,L"update\\staging\\expanded\\l4con\\l4con.exe",source));
    CHECK(l4_layout_data_path(&layout,L"update\\cache\\new.zip",archive));
    CHECK(fixture_write(source,"new",3));CHECK(fixture_write(archive,new_archive,sizeof(new_archive)));
    BYTE archive_hash[32],bad_hash[32]={0};CHECK(fixture_hash(new_archive,sizeof(new_archive),archive_hash));
    L4ReleaseFile file={L"l4con",L"l4con.exe",3,{0}};CHECK(fixture_hash("new",3,file.sha256));
    CHECK(!l4_release_publish(&layout,archive,bad_hash,expanded,&file,1));CHECK(GetFileAttributesW(layout.release)==INVALID_FILE_ATTRIBUTES);
    L4ReleaseFile bad=file;memset(bad.sha256,0,32);
    CHECK(!l4_release_publish(&layout,archive,archive_hash,expanded,&bad,1));CHECK(GetFileAttributesW(layout.release)==INVALID_FILE_ATTRIBUTES);
    CHECK(l4_release_publish(&layout,archive,archive_hash,expanded,&file,1));CHECK(l4_release_verify(&layout,&file,1));
    CHECK(l4_layout_component(&layout,file.component,file.file,target));
    CHECK(ordinary_access(target,GENERIC_READ));CHECK(!ordinary_access(target,GENERIC_WRITE));
    CHECK(!ordinary_access(layout.release,FILE_ADD_FILE));CHECK(!ordinary_access(target,WRITE_DAC));
    L4ReleaseFence* held=NULL;wchar_t pinned[MAX_PATH];CHECK(l4_release_pin(&layout,&file,&held,pinned));
    CHECK(!fixture_write(target,"bad",3));swprintf_s(other,MAX_PATH,L"%ls\\pinned-move-target",root);CHECK(!MoveFileExW(layout.release,other,0));l4_release_unpin(held);
    HANDLE inspected=CreateFileW(target,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);BY_HANDLE_FILE_INFORMATION before_info={0},after_info={0};
    CHECK(inspected!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(inspected,&before_info));if(inspected!=INVALID_HANDLE_VALUE)CloseHandle(inspected);
    CHECK(l4_release_publish(&layout,archive,archive_hash,expanded,&file,1));
    inspected=CreateFileW(target,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(inspected!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(inspected,&after_info));if(inspected!=INVALID_HANDLE_VALUE)CloseHandle(inspected);
    CHECK(before_info.nFileIndexLow==after_info.nFileIndexLow && before_info.nFileIndexHigh==after_info.nFileIndexHigh &&
        before_info.ftLastWriteTime.dwHighDateTime==after_info.ftLastWriteTime.dwHighDateTime && before_info.ftLastWriteTime.dwLowDateTime==after_info.ftLastWriteTime.dwLowDateTime);
    swprintf_s(other,MAX_PATH,L"%ls\\unlisted",layout.release);CHECK(fixture_write(other,"x",1));CHECK(!l4_release_verify(&layout,&file,1));CHECK(!l4_release_publish(&layout,archive,archive_hash,expanded,&file,1));CHECK(DeleteFileW(other));
    CHECK(CreateDirectoryW(other,NULL));CHECK(!l4_release_verify(&layout,&file,1));CHECK(RemoveDirectoryW(other));
    CHECK(fixture_write(target,"bad",3));CHECK(!l4_release_verify(&layout,&file,1));CHECK(!l4_release_publish(&layout,archive,archive_hash,expanded,&file,1));CHECK(fixture_write(target,"new",3));
    swprintf_s(other,MAX_PATH,L"%ls:unlisted",target);CHECK(fixture_write(other,"x",1));CHECK(!l4_release_verify(&layout,&file,1));CHECK(DeleteFileW(other));
    L4ReleaseFile duplicates[]={file,file};CHECK(!l4_release_verify(&layout,duplicates,2));
    duplicates[1].component=L"L4CON";CHECK(!l4_release_verify(&layout,duplicates,2));
    duplicates[1].file=L"l4con.exe\\child";CHECK(!l4_release_verify(&layout,duplicates,2));
    bad=file;bad.file=L"..\\escape";CHECK(!l4_release_publish(&layout,archive,archive_hash,expanded,&bad,1));
    bad=file;bad.size=4;CHECK(!l4_release_verify(&layout,&bad,1));
    swprintf_s(other,MAX_PATH,L"%ls\\hardlink",layout.release);CHECK(CreateHardLinkW(other,target,NULL));CHECK(!l4_release_verify(&layout,&file,1));CHECK(DeleteFileW(other));
    swprintf_s(other,MAX_PATH,L"%ls\\source-link",expanded);CHECK(CreateHardLinkW(other,source,NULL));
    CHECK(l4_layout_from_roots(&previous,programs,data,L"1.13.1"));CHECK(!l4_release_publish(&previous,archive,archive_hash,expanded,&file,1));CHECK(DeleteFileW(other));
    CHECK(!l4_release_publish(&previous,source,archive_hash,expanded,&file,1));
    L4Layout invalid=layout;wcscpy_s(invalid.cache,MAX_PATH,L"C:\\outside");CHECK(!l4_release_publish(&invalid,archive,archive_hash,expanded,&file,1));
    L4ReleaseFile old_file=file;CHECK(fixture_hash("old",3,old_file.sha256));
    CHECK(fixture_write(source,"old",3));CHECK(fixture_write(archive,old_archive,sizeof(old_archive)));CHECK(fixture_hash(old_archive,sizeof(old_archive),archive_hash));
    CHECK(l4_release_publish(&previous,archive,archive_hash,expanded,&old_file,1));
    /* Actual STORE/DEFLATE archives, Unicode outer path and malicious names. */
    L4Layout zip_layout;BYTE zip_hash[32];wchar_t zip_path[MAX_PATH];
    CHECK(l4_layout_data_path(&layout,L"update\\cache\\архив.zip",zip_path));
    CHECK(l4_layout_from_roots(&zip_layout,programs,data,L"1.13.3"));
    CHECK(fixture_write(zip_path,new_archive,sizeof(new_archive)));CHECK(fixture_hash(new_archive,sizeof(new_archive),zip_hash));
    CHECK(!l4_release_unpack_publish(&zip_layout,zip_path,bad_hash,&file,1));CHECK(GetFileAttributesW(zip_layout.release)==INVALID_FILE_ATTRIBUTES);
    CHECK(!l4_release_unpack_publish_checked(&zip_layout,zip_path,zip_hash,&file,1,NULL,NULL));
    CHECK(!l4_release_unpack_publish_checked(&zip_layout,zip_path,bad_hash,&file,1,fixture_admission,&admission_allowed));CHECK(!admission_calls);
    CHECK(!l4_release_unpack_publish_checked(&zip_layout,zip_path,zip_hash,&file,1,fixture_admission,&admission_allowed));
    CHECK(admission_calls==1 && GetFileAttributesW(zip_layout.release)==INVALID_FILE_ATTRIBUTES);
    admission_allowed=true;CHECK(l4_release_unpack_publish_checked(&zip_layout,zip_path,zip_hash,&file,1,fixture_admission,&admission_allowed));
    CHECK(admission_calls==2 && l4_release_verify(&zip_layout,&file,1));
    CHECK(l4_release_verify_checked(&zip_layout,&file,1,fixture_admission,&admission_allowed));CHECK(admission_calls==3);
    admission_allowed=false;CHECK(!l4_release_unpack_publish_checked(&zip_layout,zip_path,zip_hash,&file,1,fixture_admission,&admission_allowed));
    CHECK(admission_calls==4 && l4_release_verify(&zip_layout,&file,1));
    CHECK(!l4_release_verify_checked(&zip_layout,&file,1,fixture_admission,&admission_allowed));CHECK(admission_calls==5);
    CHECK(!l4_release_verify_checked(&zip_layout,&file,1,NULL,NULL));
    CHECK(l4_layout_from_roots(&zip_layout,programs,data,L"1.13.4"));
    CHECK(fixture_write(zip_path,compressed_archive,sizeof(compressed_archive)));CHECK(fixture_hash(compressed_archive,sizeof(compressed_archive),zip_hash));
    CHECK(l4_release_unpack_publish(&zip_layout,zip_path,zip_hash,&file,1));CHECK(l4_release_verify(&zip_layout,&file,1));
    CHECK(l4_layout_from_roots(&zip_layout,programs,data,L"1.13.5"));
    const BYTE* rejected_archives[]={traversal_archive,extra_archive,duplicate_archive};
    const DWORD rejected_sizes[]={sizeof(traversal_archive),sizeof(extra_archive),sizeof(duplicate_archive)};
    for(unsigned i=0;i<3;i++){
        CHECK(fixture_write(zip_path,rejected_archives[i],rejected_sizes[i]));CHECK(fixture_hash(rejected_archives[i],rejected_sizes[i],zip_hash));
        CHECK(!l4_release_unpack_publish(&zip_layout,zip_path,zip_hash,&file,1));CHECK(GetFileAttributesW(zip_layout.release)==INVALID_FILE_ATTRIBUTES);
    }
    CHECK(fixture_write(zip_path,"not a zip but long enough for header",35));CHECK(fixture_hash("not a zip but long enough for header",35,zip_hash));
    CHECK(!l4_release_unpack_publish(&zip_layout,zip_path,zip_hash,&file,1));CHECK(GetFileAttributesW(zip_layout.release)==INVALID_FILE_ATTRIBUTES);
    CHECK(fixture_write(zip_path,empty_archive,sizeof(empty_archive)));CHECK(fixture_hash(empty_archive,sizeof(empty_archive),zip_hash));
    L4ReleaseFile empty=file;empty.size=0;CHECK(fixture_hash("",0,empty.sha256));
    CHECK(l4_release_unpack_publish(&zip_layout,zip_path,zip_hash,&empty,1));CHECK(l4_release_verify(&zip_layout,&empty,1));
    CHECK(l4_layout_from_roots(&zip_layout,programs,data,L"1.13.6"));
    CHECK(l4_layout_data_path(&layout,L"update\\cache\\archive-link.zip",other));
    CHECK(CreateSymbolicLinkW(other,zip_path,0));CHECK(!l4_release_unpack_publish(&zip_layout,other,zip_hash,&empty,1));CHECK(DeleteFileW(other));
    CHECK(CreateSymbolicLinkW(zip_layout.release,layout.release,SYMBOLIC_LINK_FLAG_DIRECTORY));
    CHECK(!l4_release_unpack_publish(&zip_layout,zip_path,zip_hash,&empty,1));CHECK(RemoveDirectoryW(zip_layout.release));
    L4ServiceInventory saved={0};saved.installed=true;saved.start_type=SERVICE_AUTO_START;wcscpy_s(saved.account,_countof(saved.account),L"LocalSystem");
    wchar_t old_target[MAX_PATH];CHECK(l4_layout_component(&previous,L"l4con",L"l4con.exe",old_target));
    swprintf_s(saved.image_path,_countof(saved.image_path),L"\"%ls\" --service --proxy-port 18443",old_target);
    L4ServiceSwitch plan;CHECK(l4_service_switch_plan(&layout,L"L4Con",&saved,NULL,&file,1,&old_file,1,&plan));
    wchar_t expected[2048];swprintf_s(expected,_countof(expected),L"\"%ls\" --service --proxy-port 18443",target);CHECK(!wcscmp(plan.after,expected));
    L4ServiceSwitch explicit_plan;CHECK(l4_service_switch_plan(&layout,L"L4Con",&saved,L"--service --proxy-port 18444",&file,1,&old_file,1,&explicit_plan));
    CHECK(wcsstr(explicit_plan.after,L"18444")!=NULL);
    L4ServiceInventory legacy=saved;wcscpy_s(legacy.image_path,_countof(legacy.image_path),L"\"C:\\l4tools\\l4con\\l4con.exe\" --service");
    CHECK(!l4_service_switch_plan(&layout,L"L4Con",&legacy,NULL,&file,1,&old_file,1,&explicit_plan));
    CHECK(!l4_service_switch_plan(&layout,L"Unknown",&saved,NULL,&file,1,&old_file,1,&explicit_plan));
    CHECK(!l4_service_switch_plan(&layout,L"L4Con",&saved,L"--service\nunsafe",&file,1,&old_file,1,&explicit_plan));
    fake_config=saved;fake_state=SERVICE_RUNNING;CHECK(!l4_service_switch_apply(&plan));CHECK(writes==0);
    fake_state=SERVICE_STOP_PENDING;CHECK(!l4_service_switch_apply(&plan));CHECK(writes==0);
    fake_state=SERVICE_STOPPED;fake_config.start_type=SERVICE_DISABLED;CHECK(!l4_service_switch_apply(&plan));CHECK(writes==0);
    fake_config=saved;wcscpy_s(fake_config.account,_countof(fake_config.account),L"ExternalAccount");CHECK(!l4_service_switch_apply(&plan));CHECK(writes==0);
    fake_config=saved;wcscpy_s(fake_config.image_path,_countof(fake_config.image_path),L"external");CHECK(!l4_service_switch_apply(&plan));CHECK(writes==0);
    fake_config=saved;CHECK(l4_service_switch_apply(&plan));CHECK(!wcscmp(fake_config.image_path,plan.after) && !wcscmp(fake_config.account,saved.account));
    unsigned applied=writes;CHECK(l4_service_switch_apply(&plan));CHECK(writes==applied);
    CHECK(fixture_write(old_target,"bad",3));CHECK(!l4_service_switch_rollback(&plan));CHECK(writes==applied);CHECK(fixture_write(old_target,"old",3));
    CHECK(l4_service_switch_rollback(&plan));CHECK(!wcscmp(fake_config.image_path,saved.image_path));
    applied=writes;CHECK(l4_service_switch_rollback(&plan));CHECK(writes==applied);
    fake_config=saved;reject_write=true;CHECK(!l4_service_switch_apply(&plan));CHECK(!wcscmp(fake_config.image_path,saved.image_path));reject_write=false;
    writes=0;query_failure_after_write=true;CHECK(!l4_service_switch_apply(&plan));CHECK(!wcscmp(fake_config.image_path,plan.after));CHECK(l4_service_switch_rollback(&plan));
    fake_config=saved;CHECK(l4_service_switch_apply(&plan));wcscpy_s(fake_config.image_path,_countof(fake_config.image_path),L"operator-edit");applied=writes;
    CHECK(!l4_service_switch_rollback(&plan));CHECK(writes==applied && !wcscmp(fake_config.image_path,L"operator-edit"));
    fake_config=saved;CHECK(fixture_write(target,"bad",3));applied=writes;CHECK(!l4_service_switch_apply(&plan));CHECK(writes==applied);CHECK(fixture_write(target,"new",3));
    L4ServiceSwitch tampered=plan;wcscpy_s(tampered.after,_countof(tampered.after),L"\"C:\\outside.exe\"");CHECK(!l4_service_switch_apply(&tampered));CHECK(writes==applied);
    fake_config=saved;changed_account_after_write=true;CHECK(!l4_service_switch_apply(&plan));applied=writes;
    CHECK(!l4_service_switch_rollback(&plan));CHECK(writes==applied && !wcscmp(fake_config.account,L"ChangedByOperator"));changed_account_after_write=false;
    puts("SCM switch/rollback exercised only against fake SCM; no real service mutations");
    fixture_cleanup(root);CHECK(GetFileAttributesW(root)==INVALID_FILE_ATTRIBUTES);
    printf("deployment: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
