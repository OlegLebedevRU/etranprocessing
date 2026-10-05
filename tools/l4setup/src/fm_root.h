#ifndef L4SETUP_FM_ROOT_H
#define L4SETUP_FM_ROOT_H
#include <windows.h>
#include <stdbool.h>
#include <wchar.h>
#include <aclapi.h>
#include <sddl.h>

/* Hold every ancestor against rename; never follow reparse points. Repair keeps
 * existing files. Interactive desktop users may modify this dedicated exchange
 * directory; service receipts retain their own protected SYSTEM/admin DACL. */
static bool setup_prepare_fm_root(const wchar_t* root) {
    if (!root || wcslen(root)<4 || wcslen(root)>=MAX_PATH || root[1]!=L':' || root[2]!=L'\\' ||
        !((root[0]>=L'A' && root[0]<=L'Z') || (root[0]>=L'a' && root[0]<=L'z'))) return false;
    wchar_t path[MAX_PATH];wcscpy_s(path,MAX_PATH,root);
    HANDLE ancestors[MAX_PATH];unsigned count=0;bool ok=true;
    PSECURITY_DESCRIPTOR descriptor=NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;IU)",SDDL_REVISION_1,&descriptor,NULL)) return false;
    SECURITY_ATTRIBUTES security={sizeof(security),descriptor,FALSE};
    for(size_t i=3;ok;i++) {
        if(i!=3 && path[i] && path[i]!=L'\\') continue;
        wchar_t saved=path[i];path[i]=0;
        const wchar_t* part=wcsrchr(path,L'\\');part=part?part+1:path;
        if(i>3 && (!*part || part[wcslen(part)-1]==L'.' || part[wcslen(part)-1]==L' ' || wcspbrk(part,L":/*?\"<>|"))) {ok=false;break;}
        if(i>3 && GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES &&
            !CreateDirectoryW(path,!saved?&security:NULL) && GetLastError()!=ERROR_ALREADY_EXISTS) {ok=false;break;}
        HANDLE handle=CreateFileW(path,FILE_READ_ATTRIBUTES|(!saved?(WRITE_DAC|READ_CONTROL):0),FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,
            OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        if(handle==INVALID_HANDLE_VALUE) {ok=false;break;}
        ancestors[count++]=handle;BY_HANDLE_FILE_INFORMATION info;
        ok=GetFileInformationByHandle(handle,&info) && (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
        if(ok && !saved) {
            PACL dacl=NULL;BOOL present=FALSE,defaulted=FALSE;
            ok=GetSecurityDescriptorDacl(descriptor,&present,&dacl,&defaulted) && present &&
                SetSecurityInfo(handle,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,NULL,NULL,dacl,NULL)==ERROR_SUCCESS;
        }
        path[i]=saved;if(!saved) break;
    }
    for(unsigned i=0;i<count;i++)CloseHandle(ancestors[i]);LocalFree(descriptor);return ok;
}
#endif
