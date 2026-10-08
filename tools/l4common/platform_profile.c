#include "platform_profile.h"
#include <stdio.h>
#include <string.h>
static bool fail(DWORD code){SetLastError(code);return false;}
bool l4_platform_read(L4PlatformFacts* facts){
    if(!facts)return fail(ERROR_INVALID_PARAMETER);memset(facts,0,sizeof(*facts));
    typedef LONG (WINAPI* VersionReader)(OSVERSIONINFOEXW*);
    HMODULE module=GetModuleHandleW(L"ntdll.dll");
    FARPROC symbol=module?GetProcAddress(module,"RtlGetVersion"):NULL;
    if(!symbol)return fail(ERROR_NOT_SUPPORTED);
#pragma warning(suppress:4191) /* Exact OS-owned export signature, both Win32 ABIs. */
    VersionReader reader=(VersionReader)symbol;
    OSVERSIONINFOEXW version={0};version.dwOSVersionInfoSize=sizeof(version);
    if(reader(&version)!=0)return fail(ERROR_NOT_SUPPORTED);
    SYSTEM_INFO machine={0};GetNativeSystemInfo(&machine);
    facts->major=version.dwMajorVersion;facts->minor=version.dwMinorVersion;
    facts->build=version.dwBuildNumber;facts->native_arch=machine.wProcessorArchitecture;
    facts->product_type=version.wProductType;return true;
}
bool l4_platform_format(const L4PlatformFacts* facts,char profile[L4_PLATFORM_PROFILE_SIZE]){
    if(profile)profile[0]=0;if(!facts || !profile)return fail(ERROR_INVALID_PARAMETER);
    const char* arch=facts->native_arch==PROCESSOR_ARCHITECTURE_AMD64?"x64":
        facts->native_arch==PROCESSOR_ARCHITECTURE_INTEL?"x86":NULL;
    bool supported=(facts->major==6 && facts->minor>=1 && facts->minor<=3 && facts->build>=7601) ||
        (facts->major==10 && facts->minor==0 && facts->build>=10240);
    if(!arch || !supported || facts->product_type!=VER_NT_WORKSTATION)return fail(ERROR_NOT_SUPPORTED);
    int count=sprintf_s(profile,L4_PLATFORM_PROFILE_SIZE,"windows-nt-%lu.%lu.%lu-%s-client",
        facts->major,facts->minor,facts->build,arch);
    if(count<=0 || count>=(int)L4_PLATFORM_PROFILE_SIZE){profile[0]=0;return fail(ERROR_INVALID_DATA);}return true;
}
bool l4_platform_current(char profile[L4_PLATFORM_PROFILE_SIZE]){
    if(profile)profile[0]=0;if(!profile)return fail(ERROR_INVALID_PARAMETER);
    L4PlatformFacts facts;return l4_platform_read(&facts) && l4_platform_format(&facts,profile);
}
