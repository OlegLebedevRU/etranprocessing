#include "../registry_http.h"
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char** argv){if(argc!=2)return 1;unsigned long port=strtoul(argv[1],NULL,10);if(!port || port>65535)return 1;
    BYTE* bytes=NULL;DWORD size=0;bool ok=l4_registry_metadata((WORD)port,NULL,"catalog.json",4000,&bytes,&size);DWORD error=GetLastError();
    if(ok || bytes || size || error!=ERROR_WINHTTP_SECURE_FAILURE){free(bytes);printf("Unexpected TLS result: ok=%d size=%lu error=%lu\n",ok,size,error);return 1;}
    puts("Actual WinHTTP TLS PASS: untrusted Registry certificate refused through loopback CONNECT; no direct fallback");return 0;
}
