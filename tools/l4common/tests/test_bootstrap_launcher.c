#include "../launcher.h"
#include "../bootstrap.h"
#include "../journal_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deployment_fixture.h"
static unsigned checks,failures,queries;
static bool existing,denied;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line %u: %s (win32=%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static bool mock_inventory(const wchar_t* name,L4ServiceInventory* out){(void)name;++queries;memset(out,0,sizeof(*out));out->installed=existing;if(denied){SetLastError(ERROR_ACCESS_DENIED);return false;}return true;}
#define l4_service_inventory mock_inventory
#include "../bootstrap.c"
#undef l4_service_inventory
static void cleanup(const wchar_t* root){wchar_t pattern[MAX_PATH],child[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",root);WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW(pattern,&d);
    if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L".."))continue;swprintf_s(child,MAX_PATH,L"%ls\\%ls",root,d.cFileName);
        if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))cleanup(child);else RemoveDirectoryW(child);}else DeleteFileW(child);
    }while(FindNextFileW(h,&d));FindClose(h);}RemoveDirectoryW(root);}
static bool write_file(const wchar_t* path,const void* bytes,DWORD size){HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);if(file==INVALID_HANDLE_VALUE)return false;bool ok=l4_store_write(file,bytes,size) && FlushFileBuffers(file);CloseHandle(file);return ok;}
static bool hash_file(const wchar_t* path,ULONGLONG* size,BYTE digest[32]){HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER length={0};bool ok=GetFileSizeEx(file,&length) && length.QuadPart>0 && length.QuadPart<16*1024*1024;BYTE* bytes=ok?(BYTE*)malloc((size_t)length.QuadPart):NULL;DWORD n=0;
    if(ok)ok=bytes && ReadFile(file,bytes,(DWORD)length.QuadPart,&n,NULL) && n==(DWORD)length.QuadPart && l4_store_hash(bytes,n,NULL,0,digest);*size=(ULONGLONG)length.QuadPart;free(bytes);CloseHandle(file);return ok;}
static int ordinary_access(const wchar_t* path,DWORD rights){
    HANDLE token=NULL,limited=NULL;BYTE admin[SECURITY_MAX_SID_SIZE];DWORD size=sizeof(admin);
    if(!CreateWellKnownSid(WinBuiltinAdministratorsSid,NULL,admin,&size))return -1;SID_AND_ATTRIBUTES disabled={admin,0};
    bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE,&token) &&
        CreateRestrictedToken(token,DISABLE_MAX_PRIVILEGE,1,&disabled,0,NULL,0,NULL,&limited) && ImpersonateLoggedOnUser(limited);
    int allowed=-1;if(ok){HANDLE file=CreateFileW(path,rights,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);allowed=file!=INVALID_HANDLE_VALUE;if(allowed)CloseHandle(file);if(!RevertToSelf())allowed=-1;}
    if(limited)CloseHandle(limited);if(token)CloseHandle(token);return allowed;
}
static int child(int argc,wchar_t** argv){wchar_t cwd[MAX_PATH];char input[3];DWORD n=0;
    if(argc!=5 || wcscmp(argv[2],L"a b Ю") || wcscmp(argv[3],L"\"q\"") || !GetCurrentDirectoryW(MAX_PATH,cwd) || _wcsicmp(cwd,argv[4]) ||
        !ReadFile(GetStdHandle(STD_INPUT_HANDLE),input,3,&n,NULL) || n!=3 || memcmp(input,"abc",3))return 91;
    if(!l4_store_write(GetStdHandle(STD_OUTPUT_HANDLE),"child-output",12) || !l4_store_write(GetStdHandle(STD_ERROR_HANDLE),"child-error",11))return 92;
    return 37;
}
int wmain(int argc,wchar_t** argv){
    if(argc>1 && !wcscmp(argv[1],L"--child"))return child(argc,argv);
    wchar_t temp[MAX_PATH],root[MAX_PATH],programs[MAX_PATH],data[MAX_PATH],self[MAX_PATH],expanded[MAX_PATH],source[MAX_PATH],archive[MAX_PATH],work[MAX_PATH],map[MAX_PATH],target[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH,temp)!=0);CHECK(GetModuleFileNameW(NULL,self,MAX_PATH)!=0);
    swprintf_s(root,MAX_PATH,L"%lsl4bootstrap-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(root,NULL));
    swprintf_s(programs,MAX_PATH,L"%ls\\Programs",root);swprintf_s(data,MAX_PATH,L"%ls\\Data",root);L4Layout layout,old;
    CHECK(l4_layout_from_roots(&layout,programs,data,L"1.13.2"));CHECK(l4_layout_from_roots(&old,programs,data,L"1.13.1"));CHECK(l4_layout_prepare(&layout));
    CHECK(l4_layout_prepare_leaf(&layout,L"config\\launchers",NULL,NULL,false));
    CHECK(l4_layout_prepare_leaf(&layout,L"state\\l4con\\work",NULL,NULL,false));CHECK(l4_layout_data_path(&layout,L"state\\l4con\\work",work));
    const wchar_t* names[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv",L"l4launch"};const wchar_t* exes[]={L"leo4proxy.exe",L"mosquitto.exe",L"l4con.exe",L"l4superv.exe",L"l4launch.exe"};
    L4ReleaseFile files[5]={0};ULONGLONG size=0;BYTE digest[32];CHECK(hash_file(self,&size,digest));
    for(unsigned i=0;i<5;i++){
        wchar_t leaf[MAX_PATH];swprintf_s(leaf,MAX_PATH,L"update\\staging\\fixture\\%ls",names[i]);CHECK(l4_layout_prepare_leaf(&layout,leaf,NULL,NULL,false));
        swprintf_s(source,MAX_PATH,L"%ls\\update\\staging\\fixture\\%ls\\%ls",data,names[i],exes[i]);CHECK(CopyFileW(self,source,TRUE));
        files[i].component=names[i];files[i].file=exes[i];files[i].size=size;memcpy(files[i].sha256,digest,32);
    }
    CHECK(l4_layout_data_path(&layout,L"update\\staging\\fixture",expanded));CHECK(l4_layout_data_path(&layout,L"update\\cache\\fixture.zip",archive));CHECK(write_file(archive,new_archive,sizeof(new_archive)));
    BYTE archive_hash[32];CHECK(l4_store_hash(new_archive,sizeof(new_archive),NULL,0,archive_hash));CHECK(l4_release_publish(&layout,archive,archive_hash,expanded,files,5));CHECK(l4_release_publish(&old,archive,archive_hash,expanded,files,5));
    L4BootstrapProfile profiles[]={ {L"Leo4Proxy",L"LocalSystem",L"--service",SERVICE_AUTO_START}, {L"mosquitto",L"LocalSystem",L"-c \"C:\\config with spaces\\mosquitto.conf\"",SERVICE_AUTO_START},
        {L"L4Con",L"LocalSystem",L"--service",SERVICE_AUTO_START}, {L"L4Superv",L"LocalSystem",L"--service",SERVICE_AUTO_START} };
    L4BootstrapPlan plan;CHECK(l4_bootstrap_plan(&layout,profiles,4,files,5,&plan));CHECK(queries==4);CHECK(wcsstr(plan.commands[1],L"config with spaces")!=NULL);
    existing=true;CHECK(!l4_bootstrap_plan(&layout,profiles,4,files,5,&plan));CHECK(GetLastError()==ERROR_SERVICE_EXISTS);existing=false;
    denied=true;CHECK(!l4_bootstrap_plan(&layout,profiles,4,files,5,&plan));CHECK(GetLastError()==ERROR_ACCESS_DENIED);denied=false;
    profiles[2].account=L"SomeoneElse";CHECK(!l4_bootstrap_plan(&layout,profiles,4,files,5,&plan));profiles[2].account=L"LocalSystem";
    profiles[2].service=L"mosquitto";CHECK(!l4_bootstrap_plan(&layout,profiles,4,files,5,&plan));profiles[2].service=L"L4Con";
    profiles[2].start_type=SERVICE_BOOT_START;CHECK(!l4_bootstrap_plan(&layout,profiles,4,files,5,&plan));profiles[2].start_type=SERVICE_AUTO_START;
    profiles[2].arguments=L"--service\r\n";CHECK(!l4_bootstrap_plan(&layout,profiles,4,files,5,&plan));profiles[2].arguments=L"--service";
    CHECK(!l4_bootstrap_plan(&layout,profiles,3,files,5,&plan));
    L4Journal* j=NULL;CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000003",true,&j));if(!j){cleanup(root);return 1;}
    CHECK(l4_launcher_install(j,&layout,L"l4con",files,5));CHECK(l4_launcher_install(j,&layout,L"l4con",files,5));
    wchar_t stable[MAX_PATH];swprintf_s(stable,MAX_PATH,L"%ls\\l4con.exe",layout.launchers);
    ULONGLONG stable_size=0;BYTE stable_hash[32];CHECK(hash_file(stable,&stable_size,stable_hash));CHECK(stable_size==size && !memcmp(stable_hash,digest,32));
    CHECK(!l4_launcher_install(j,&layout,L"../escape",files,5));
    CHECK(!l4_release_copy_launcher(&layout,NULL,L"l4con.exe"));
    CHECK(!l4_release_copy_launcher(&layout,&files[4],L"../escape"));
    CHECK(ordinary_access(stable,GENERIC_READ)==1);CHECK(ordinary_access(stable,GENERIC_WRITE)==0);
    CHECK(write_file(stable,"tamper",6));CHECK(!l4_launcher_install(j,&layout,L"l4con",files,5));
    CHECK(CopyFileW(self,stable,FALSE));
    ULONGLONG pointer=0;L4ReleaseFence* fence=NULL;CHECK(!l4_launcher_resolve(&layout,L"l4con",&fence,target));
    CHECK(!l4_launcher_prepare(j,&layout,L"unknown",files,5,NULL));CHECK(l4_launcher_prepare(j,&old,L"l4con",files,5,&pointer));CHECK(l4_config_apply(j,pointer));
    CHECK(l4_launcher_resolve(&layout,L"l4con",&fence,target));CHECK(wcsstr(target,L"1.13.1")!=NULL);
    HANDLE overwrite=CreateFileW(target,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);CHECK(overwrite==INVALID_HANDLE_VALUE);if(overwrite!=INVALID_HANDLE_VALUE)CloseHandle(overwrite);l4_release_unpin(fence);fence=NULL;
    ULONGLONG next=0;CHECK(l4_launcher_prepare(j,&layout,L"l4con",files,5,&next));CHECK(l4_config_apply(j,next));CHECK(l4_launcher_resolve(&layout,L"l4con",&fence,target));CHECK(wcsstr(target,L"1.13.2")!=NULL);l4_release_unpin(fence);fence=NULL;
    CHECK(l4_config_rollback(j,next));CHECK(l4_launcher_resolve(&layout,L"l4con",&fence,target));CHECK(wcsstr(target,L"1.13.1")!=NULL);l4_release_unpin(fence);fence=NULL;
    wchar_t installed[MAX_PATH];const wchar_t* name=NULL;swprintf_s(installed,MAX_PATH,L"%ls\\l4con.exe",layout.launchers);CHECK(l4_launcher_identity(installed,&layout,&name));CHECK(!wcscmp(name,L"l4con"));CHECK(!l4_launcher_identity(self,&layout,&name));
    wchar_t* command=NULL;CHECK(l4_launcher_command(L"C:\\Program Files\\test.exe",L"\"a b.exe\"  --flag \"a b\"",&command));CHECK(!wcscmp(command,L"\"C:\\Program Files\\test.exe\"  --flag \"a b\""));free(command);
    CHECK(!l4_launcher_command(L"C:\\test.exe",L"\"unclosed",&command));
    wchar_t journal_file[MAX_PATH];swprintf_s(journal_file,MAX_PATH,L"%ls\\journal.bin",j->directory);
    CHECK(ordinary_access(journal_file,GENERIC_READ)==0);
    /* Actual child, redirected stdin/out/err, quotes/spaces, CWD and exit code. */
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};HANDLE in_read=NULL,in_write=NULL,out_read=NULL,out_write=NULL;
    CHECK(CreatePipe(&in_read,&in_write,&sa,0));CHECK(CreatePipe(&out_read,&out_write,&sa,0));CHECK(SetHandleInformation(in_write,HANDLE_FLAG_INHERIT,0));CHECK(SetHandleInformation(out_read,HANDLE_FLAG_INHERIT,0));
    CHECK(l4_store_write(in_write,"abc",3));CloseHandle(in_write);
    HANDLE save_in=GetStdHandle(STD_INPUT_HANDLE),save_out=GetStdHandle(STD_OUTPUT_HANDLE),save_err=GetStdHandle(STD_ERROR_HANDLE);
    CHECK(SetStdHandle(STD_INPUT_HANDLE,in_read));CHECK(SetStdHandle(STD_OUTPUT_HANDLE,out_write));CHECK(SetStdHandle(STD_ERROR_HANDLE,out_write));
    wchar_t original[1024];swprintf_s(original,1024,L"l4con.exe --child \"a b Ю\" \"\\\"q\\\"\" \"%ls\"",work);DWORD exit_code=0;
    bool ran=l4_launcher_run(&layout,L"l4con",original,&exit_code);
    SetStdHandle(STD_INPUT_HANDLE,save_in);SetStdHandle(STD_OUTPUT_HANDLE,save_out);SetStdHandle(STD_ERROR_HANDLE,save_err);CloseHandle(in_read);CloseHandle(out_write);
    CHECK(ran);CHECK(exit_code==37);char output[64]={0};DWORD n=0;CHECK(ReadFile(out_read,output,sizeof(output),&n,NULL));CHECK(n==23 && !memcmp(output,"child-outputchild-error",23));CloseHandle(out_read);
    CHECK(l4_layout_data_path(&layout,L"config\\launchers\\l4con.target",map));
    CHECK(ordinary_access(map,GENERIC_READ)==1);CHECK(ordinary_access(map,GENERIC_WRITE)==0);
    wchar_t extra[MAX_PATH];swprintf_s(extra,MAX_PATH,L"%ls:extra",map);CHECK(write_file(extra,"ads",3));CHECK(!l4_launcher_resolve(&layout,L"l4con",&fence,target));CHECK(DeleteFileW(extra));
    swprintf_s(extra,MAX_PATH,L"%ls\\config\\launchers\\alias.target",data);CHECK(CreateHardLinkW(extra,map,NULL));CHECK(!l4_launcher_resolve(&layout,L"l4con",&fence,target));CHECK(DeleteFileW(extra));
    char saved[160]={0};DWORD saved_size=0;HANDLE read=CreateFileW(map,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    CHECK(read!=INVALID_HANDLE_VALUE);CHECK(ReadFile(read,saved,sizeof(saved),&saved_size,NULL));CloseHandle(read);
    char wrong[160];memcpy(wrong,saved,saved_size);wrong[saved_size-65]=wrong[saved_size-65]=='0'?'1':'0';
    CHECK(write_file(map,wrong,saved_size));CHECK(!l4_launcher_resolve(&layout,L"l4con",&fence,target));CHECK(fence==NULL);CHECK(write_file(map,saved,saved_size));
    CHECK(write_file(map,"L4CLI1\n../x\n1\n0\n",17));CHECK(!l4_launcher_resolve(&layout,L"l4con",&fence,target));CHECK(fence==NULL);
    l4_journal_close(j);cleanup(root);CHECK(GetFileAttributesW(root)==INVALID_FILE_ATTRIBUTES);
    printf("bootstrap/launcher: %u checks, %u failures (SCM reads mocked, real child/file I/O)\n",checks,failures);return failures?1:0;
}
