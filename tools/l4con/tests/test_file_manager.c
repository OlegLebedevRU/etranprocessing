/* Exercise the real path/handle implementation without MQTT or remote services. */
#include "../src/file_manager.c"
#include <assert.h>
#include <winioctl.h>

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
    const wchar_t* rejected[]={L"\\\\server\\share",L"\\\\?\\C:\\fixture",L"C:relative",L"C:\\fixture\\..\\outside",L"C:\\fixture\\file:ads",L"C:\\fixture\\NUL.txt",L"C:\\fixture\\COM1.log",L"C:\\fixture\\trailing.",L"C:\\fixture\\space ",L"C:\\Windows\\System32",L"C:\\fixture\\.ssh"};
    for(unsigned i=0;i<sizeof(rejected)/sizeof(rejected[0]);i++)assert(!valid_path(rejected[i]));
    assert(valid_path(L"C:\\fixture\\Отчёт.txt"));
    wchar_t temp[1024],root[1024],alias[1100];
    assert(GetTempPathW(1024,temp)>0);assert(GetTempFileNameW(temp,L"fm",0,root)>0);
    assert(DeleteFileW(root));assert(CreateDirectoryW(root,NULL));wcscpy_s(fm.root,1024,root);
    HANDLE handles[128];unsigned count=0;
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
