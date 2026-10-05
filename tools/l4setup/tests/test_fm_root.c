#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <assert.h>
#include <stdio.h>
#include <winioctl.h>
/* Keep the non-elevated test owner able to inspect/remove its temporary fixture.
 * Production uses only SYSTEM/admins; this fixture adds OWNER RIGHTS explicitly. */
static BOOL WINAPI fixture_descriptor(LPCWSTR value,DWORD revision,PSECURITY_DESCRIPTOR* descriptor,PULONG size) {
    assert(!wcscmp(value,L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;IU)"));
    return ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;IU)(A;OICI;FA;;;OW)",revision,descriptor,size);
}
#define ConvertStringSecurityDescriptorToSecurityDescriptorW fixture_descriptor
#include "fm_root.h"
int main(void) {
    wchar_t temp[MAX_PATH],parent[MAX_PATH],root[MAX_PATH],file[MAX_PATH],alias[MAX_PATH];
    assert(GetTempPathW(MAX_PATH,temp));assert(GetTempFileNameW(temp,L"fms",0,parent));
    assert(DeleteFileW(parent));assert(CreateDirectoryW(parent,NULL));
    swprintf_s(root,MAX_PATH,L"%s\\fm",parent);bool ready=setup_prepare_fm_root(root);
    if(!ready) fprintf(stderr,"FM fixture preparation error=%lu\n",GetLastError());assert(ready);
    swprintf_s(file,MAX_PATH,L"%s\\existing",root);
    HANDLE handle=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);assert(handle!=INVALID_HANDLE_VALUE);
    DWORD bytes;assert(WriteFile(handle,"keep",4,&bytes,NULL) && bytes==4);CloseHandle(handle);
    assert(setup_prepare_fm_root(root));assert(GetFileAttributesW(file)!=INVALID_FILE_ATTRIBUTES);
    PACL acl;PSECURITY_DESCRIPTOR sd;assert(GetNamedSecurityInfoW(root,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,NULL,NULL,&acl,NULL,&sd)==ERROR_SUCCESS);
    SECURITY_DESCRIPTOR_CONTROL control;DWORD revision;assert(GetSecurityDescriptorControl(sd,&control,&revision));
    assert(control&SE_DACL_PROTECTED);assert(acl && acl->AceCount==4);LocalFree(sd);
    swprintf_s(alias,MAX_PATH,L"%s\\alias",parent);assert(CreateDirectoryW(alias,NULL));
    struct {DWORD tag;WORD length,reserved,so,sl,po,pl;wchar_t path[512];} data={0};
    data.tag=IO_REPARSE_TAG_MOUNT_POINT;swprintf_s(data.path,512,L"\\??\\%s",root);
    data.sl=(WORD)(wcslen(data.path)*2);data.po=data.sl+2;data.pl=(WORD)(wcslen(root)*2);
    memcpy((BYTE*)data.path+data.po,root,data.pl);data.length=8+data.sl+2+data.pl+2;
    handle=CreateFileW(alias,GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,NULL);
    assert(handle!=INVALID_HANDLE_VALUE);assert(DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,&data,data.length+8,NULL,0,&bytes,NULL));CloseHandle(handle);
    assert(!setup_prepare_fm_root(alias));assert(!setup_prepare_fm_root(L"C:\\..\\escape"));
    assert(!setup_prepare_fm_root(L"C:relative"));assert(!setup_prepare_fm_root(L"\\\\server\\share"));
    assert(RemoveDirectoryW(alias));assert(DeleteFileW(file));assert(RemoveDirectoryW(root));assert(RemoveDirectoryW(parent));
    puts("FM setup: create/repair preserves files, protected fixture ACL, junction/invalid path refusal passed");return 0;
}
