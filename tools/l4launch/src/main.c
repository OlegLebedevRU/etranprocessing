#include "../../l4common/launcher.h"
#include <stdio.h>
int wmain(void){
    L4Layout roots;wchar_t self[MAX_PATH];const wchar_t* tool=NULL;DWORD code=0;
    DWORD n=GetModuleFileNameW(NULL,self,MAX_PATH);
    /* Roots are OS-owned Known Folders. No env/argv override and no legacy fallback. */
    if(!n || n>=MAX_PATH || !l4_layout_resolve(&roots,L"0.0.0") ||
        !l4_launcher_identity(self,&roots,&tool) || !l4_launcher_run(&roots,tool,GetCommandLineW(),&code)){
        DWORD error=GetLastError();fwprintf(stderr,L"L4 launcher failed (Win32 %lu)\n",error);return (int)(error?error:ERROR_INVALID_DATA);
    }
    return (int)code;
}
