#include "rollback.h"
#include <stdio.h>
#include <string.h>
static bool system_user(void){
    HANDLE token=NULL;BYTE bytes[512];DWORD size=0;bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) &&
        GetTokenInformation(token,TokenUser,bytes,sizeof(bytes),&size) && IsWellKnownSid(((TOKEN_USER*)bytes)->User.Sid,WinLocalSystemSid);
    if(token)CloseHandle(token);return ok;
}
int wmain(int argc,wchar_t** argv){
    if(argc==2 && !wcscmp(argv[1],L"--help")){puts("l4rollback: fixed supervisor recovery; --operation <original UUID> [--boot]; installed SYSTEM only");return 0;}
    if((argc!=3 && argc!=4) || wcscmp(argv[1],L"--operation") || (argc==4 && wcscmp(argv[3],L"--boot")))return ERROR_INVALID_PARAMETER;
    if(!system_user())return ERROR_ACCESS_DENIED;
    L4Layout roots;wchar_t actual[MAX_PATH],expected[MAX_PATH];
    DWORD size=GetModuleFileNameW(NULL,actual,MAX_PATH);
    if(!size || size>=MAX_PATH || !l4_layout_resolve(&roots,L"0.0.0") ||
       swprintf_s(expected,MAX_PATH,L"%ls\\recovery\\l4rollback.exe",roots.binaries)<0 || _wcsicmp(actual,expected) || !rollback_installed(&roots,actual))return ERROR_INVALID_NAME;
    if(rollback_execute(&roots,argv[2],argc==4))return 0;
    DWORD error=GetLastError();fprintf(stderr,"l4rollback failed: %lu\n",error);return error?(int)error:ERROR_GEN_FAILURE;
}
