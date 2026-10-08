#include "../platform_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    L4PlatformFacts f={6,1,7601,PROCESSOR_ARCHITECTURE_INTEL,VER_NT_WORKSTATION};char profile[64];
    assert(l4_platform_format(&f,profile) && !strcmp(profile,"windows-nt-6.1.7601-x86-client"));
    f.major=10;f.minor=0;f.build=19045;f.native_arch=PROCESSOR_ARCHITECTURE_AMD64;
    assert(l4_platform_format(&f,profile) && !strcmp(profile,"windows-nt-10.0.19045-x64-client"));
    char previous[64];strcpy_s(previous,sizeof(previous),profile);++f.build;
    assert(l4_platform_format(&f,profile) && strcmp(previous,profile));
    f.build=22631;assert(l4_platform_format(&f,profile) && !strcmp(profile,"windows-nt-10.0.22631-x64-client"));
    f.product_type=VER_NT_SERVER;assert(!l4_platform_format(&f,profile) && !*profile);
    f.product_type=VER_NT_DOMAIN_CONTROLLER;assert(!l4_platform_format(&f,profile) && !*profile);
    f.product_type=VER_NT_WORKSTATION;f.native_arch=PROCESSOR_ARCHITECTURE_ARM64;
    assert(!l4_platform_format(&f,profile) && !*profile);
    f.native_arch=PROCESSOR_ARCHITECTURE_INTEL;f.major=6;f.minor=1;f.build=7600;
    assert(!l4_platform_format(&f,profile) && !*profile);
    f.major=10;f.minor=1;f.build=19045;assert(!l4_platform_format(&f,profile) && !*profile);
    f.major=11;f.minor=0;assert(!l4_platform_format(&f,profile) && !*profile);
    assert(!l4_platform_format(NULL,profile) && !*profile);assert(!l4_platform_read(NULL));
    assert(!l4_platform_current(NULL));
    L4PlatformFacts actual;char observed[64];assert(l4_platform_read(&actual));
    assert(l4_platform_current(profile) && l4_platform_format(&actual,observed) && !strcmp(profile,observed));
    printf("Platform profile: %s; exact OS build/native architecture, server/unsupported refusal PASS\n",profile);
    return 0;
}
