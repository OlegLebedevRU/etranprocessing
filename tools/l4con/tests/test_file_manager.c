/* Exercise the real path/handle implementation without MQTT or remote services. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <assert.h>
#include <winioctl.h>
static DWORD opened_access;static wchar_t opened_proxy[64];static unsigned open_count;
static HINTERNET WINAPI fixture_open(LPCWSTR agent,DWORD access,LPCWSTR proxy,LPCWSTR bypass,DWORD flags) {
    (void)agent;(void)bypass;(void)flags;opened_access=access;open_count++;
    wcscpy_s(opened_proxy,64,proxy?proxy:L"");return NULL; /* Prove routing before any network side effect. */
}
#define WinHttpOpen fixture_open
#include "../src/file_manager.c"

int config_query_fm_api_from_proxy(int port,const char* sn,char* output,size_t capacity) {
    (void)port;(void)sn;(void)output;(void)capacity;return -1; /* This fixture exercises filesystem only. */
}

static void close_handles(HANDLE handles[128],unsigned count) {
    for(unsigned i=0;i<count;i++)CloseHandle(handles[i]);
}
static bool junction(const wchar_t* alias,const wchar_t* target) {
    struct {
        DWORD tag;WORD length,reserved,sub_offset,sub_length,print_offset,print_length;
        wchar_t path[2048];
    } data={0};
    data.tag=IO_REPARSE_TAG_MOUNT_POINT;
    swprintf_s(data.path,2048,L"\\??\\%s",target);
    data.sub_length=(WORD)(wcslen(data.path)*sizeof(wchar_t));
    data.print_offset=data.sub_length+2;data.print_length=(WORD)(wcslen(target)*sizeof(wchar_t));
    memcpy((BYTE*)data.path+data.print_offset,target,data.print_length);
    data.length=8+data.sub_length+2+data.print_length+2;
    if(!CreateDirectoryW(alias,NULL))return false;
    HANDLE handle=CreateFileW(alias,GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,NULL);
    DWORD returned=0;bool ok=handle!=INVALID_HANDLE_VALUE && DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,&data,data.length+8,NULL,0,&returned,NULL);
    if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);return ok;
}
int main(void) {
    /* Cross a 64-entry page boundary: directories always precede files,
       regardless of enumeration order; numeric names use Explorer ordering. */
    WIN32_FIND_DATAW sorted[130]={0};
    for(unsigned i=0;i<130;i++) {
        sorted[i].dwFileAttributes=i%2?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL;
        swprintf_s(sorted[i].cFileName,MAX_PATH,L"item%u",64-i/2);
    }
    qsort(sorted,130,sizeof(*sorted),entry_order);
    assert(!wcscmp(sorted[2].cFileName,L"item2"));
    assert(!wcscmp(sorted[10].cFileName,L"item10"));
    assert(sorted[64].dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);
    assert(!(sorted[65].dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY));
    assert(!wcscmp(sorted[65].cFileName,L"item0"));
    assert(!wcscmp(sorted[129].cFileName,L"item64"));
    char* response=NULL;fm.proxy_port=18443;strcpy_s(fm.instance,sizeof(fm.instance),"fixture");
    strcpy_s(fm.api,sizeof(fm.api),"https://pb.example/api/file-manager/v1/agent");
    assert(!api("/hello","{}",&response) && open_count==0);
    strcpy_s(fm.api,sizeof(fm.api),"http://127.0.0.1:18444/api/file-manager/v1/agent");
    assert(!api("/hello","{}",&response) && open_count==0);
    strcpy_s(fm.api,sizeof(fm.api),"http://127.0.0.1:18443/api/file-manager/v1/agent");
    assert(!api("/hello","{}",&response) && open_count==1 && opened_access==WINHTTP_ACCESS_TYPE_NO_PROXY);
    PolicyJson grant;const char* url="{\"url\":\"https://storage.example/file?signature=fixture\"}";
    assert(policy_json_parse(&grant,url,strlen(url)));assert(!storage_io(&grant,NULL,false,0));
    assert(open_count==2 && opened_access==WINHTTP_ACCESS_TYPE_NAMED_PROXY && !wcscmp(opened_proxy,L"127.0.0.1:18443"));
    url="{\"url\":\"http://storage.example/file\"}";
    assert(policy_json_parse(&grant,url,strlen(url)));assert(!storage_io(&grant,NULL,false,0) && open_count==2);
    const wchar_t* rejected[]={L"\\\\server\\share",L"\\\\?\\C:\\fixture",L"C:relative",L"C:\\fixture\\..\\outside",L"C:\\fixture\\file:ads",L"C:\\fixture\\NUL.txt",L"C:\\fixture\\COM1.log",L"C:\\fixture\\trailing.",L"C:\\fixture\\space ",L"C:\\fixture\\.ssh"};
    for(unsigned i=0;i<sizeof(rejected)/sizeof(rejected[0]);i++)assert(!valid_path(rejected[i]));
    assert(valid_path(L"C:\\fixture\\Отчёт.txt"));
    assert(valid_path(L"C:\\"));assert(valid_path(L"C:\\Windows\\System32"));
    wchar_t temp[1024],root[1024],alias[1100];
    assert(GetTempPathW(1024,temp)>0);assert(GetTempFileNameW(temp,L"fm",0,root)>0);
    assert(DeleteFileW(root));assert(CreateDirectoryW(root,NULL));wcscpy_s(fm.root,1024,root);
    HANDLE handles[128];unsigned count=0;
    assert(!open_directory(root,handles,&count));close_handles(handles,count);
    fm.read_root_count=1;wcscpy_s(fm.read_roots[0],1024,root);
    assert(open_directory(root,handles,&count));close_handles(handles,count);
    wchar_t outside[1100];swprintf_s(outside,1100,L"%s-other",root);
    assert(!open_directory(outside,handles,&count));close_handles(handles,count);
    swprintf_s(alias,1100,L"%s\\junction",root);assert(junction(alias,root));
    assert(!open_directory(alias,handles,&count));close_handles(handles,count);
    assert(RemoveDirectoryW(alias));

    wchar_t source[1100],target[1100];swprintf_s(source,1100,L"%s\\source",root);swprintf_s(target,1100,L"%s\\target",root);
    HANDLE file=CreateFileW(source,GENERIC_WRITE|DELETE,0,NULL,CREATE_NEW,0,NULL);assert(file!=INVALID_HANDLE_VALUE);
    HANDLE existing=CreateFileW(target,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);assert(existing!=INVALID_HANDLE_VALUE);DWORD written=0;assert(WriteFile(existing,"safe",4,&written,NULL));CloseHandle(existing);
    assert(open_directory(root,handles,&count));
    struct { FILE_RENAME_INFO info;wchar_t tail[1100]; } rename={0};
    rename.info.ReplaceIfExists=FALSE;rename.info.RootDirectory=NULL;rename.info.FileNameLength=(DWORD)(wcslen(target)*sizeof(wchar_t));memcpy(rename.info.FileName,target,rename.info.FileNameLength);
    assert(!SetFileInformationByHandle(file,FileRenameInfo,&rename,sizeof(rename)));
    existing=CreateFileW(target,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(existing!=INVALID_HANDLE_VALUE);char bytes[4];DWORD read=0;assert(ReadFile(existing,bytes,4,&read,NULL) && read==4 && !memcmp(bytes,"safe",4));CloseHandle(existing);
    assert(DeleteFileW(target));
    if(!SetFileInformationByHandle(file,FileRenameInfo,&rename,sizeof(rename))) {
        fprintf(stderr,"Rename failed: error=%lu root=%ls\n",GetLastError(),root);CloseHandle(file);close_handles(handles,count);DeleteFileW(source);RemoveDirectoryW(root);return 1;
    }
    CloseHandle(file);close_handles(handles,count);assert(DeleteFileW(target));assert(RemoveDirectoryW(root));
    puts("FM: path aliases/ADS/device names/junction/ancestor handles/no-overwrite atomic rename passed");
    return 0;
}
