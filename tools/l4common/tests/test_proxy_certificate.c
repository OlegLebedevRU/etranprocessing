#include "../proxy_certificate.h"
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL cert-profile %u %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static void roundtrip(const L4ProxyCertificate* p) {
    wchar_t command[2048];CHECK(l4_proxy_certificate_command(L"C:\\Program Files\\fixture\\leo4proxy.exe",p,59471,59472,1000,command));
    int argc=0;wchar_t** wide=CommandLineToArgvW(command,&argc);CHECK(wide!=NULL);if(!wide)return;
    char values[16][512];char* args[16];CHECK(argc<=16);
    for(int i=0;i<argc && i<16;i++){CHECK(WideCharToMultiByte(CP_UTF8,0,wide[i],-1,values[i],512,NULL,NULL));args[i]=values[i];}
    L4ProxyCertificate q;CHECK(l4_proxy_certificate_probe(argc,args,&q));CHECK(!strcmp(p->email,q.email) && !strcmp(p->thumbprint,q.thumbprint) && p->machine==q.machine);
    CHECK(!strcmp(args[argc-1],"--version"));LocalFree(wide);
}
int main(void) {
    L4ProxyCertificate p={0};CHECK(l4_proxy_certificate_source(L"\"C:\\fixture.exe\" --service",&p));CHECK(p.machine && !*p.email && !*p.thumbprint);roundtrip(&p);
    CHECK(l4_proxy_certificate_source(L"\"C:\\fixture.exe\" --CERT-EMAIL first --mqtt-remote --user-store --cert-email \"space \\\"quote\\\" trail\\\\\" --cert-thumbprint 0123456789ABCDEF0123456789abcdef01234567 --user-store",&p));
    CHECK(!p.machine && !strcmp(p.email,"space \"quote\" trail\\") && strlen(p.thumbprint)==40);roundtrip(&p);
    CHECK(l4_proxy_certificate_source(L"C:\\fixture.exe --cert-email old --cert-email \"\" --cert-thumbprint \"\" --domain --cert-email --service",&p));
    CHECK(p.machine && !*p.email && !*p.thumbprint);roundtrip(&p);
    const wchar_t* bad[]={L"C:\\fixture.exe --future-option",L"C:\\fixture.exe --install",L"C:\\fixture.exe --cert-email",L"C:\\fixture.exe --http-remote",
        L"C:\\fixture.exe --cert-thumbprint 1234",L"C:\\fixture.exe --cert-thumbprint G123456789ABCDEF0123456789abcdef01234567",L"C:\\fixture.exe --cert-email кириллица"};
    for(unsigned i=0;i<_countof(bad);i++)CHECK(!l4_proxy_certificate_source(bad[i],&p));
    wchar_t long_value[600];wcscpy_s(long_value,600,L"C:\\fixture.exe --cert-email ");size_t at=wcslen(long_value);for(unsigned i=0;i<256;i++)long_value[at+i]=L'a';long_value[at+256]=0;
    CHECK(!l4_proxy_certificate_source(long_value,&p));
    char* args[]={"exe","--update-probe","59471","59472","1000","--service","--version"};CHECK(!l4_proxy_certificate_probe(7,args,&p));
    char* duplicate[]={"exe","--update-probe","59471","59472","1000","--user-store","--user-store","--version"};CHECK(!l4_proxy_certificate_probe(8,duplicate,&p));
    char* missing[]={"exe","--update-probe","59471","59472","1000","--cert-email","--version"};CHECK(!l4_proxy_certificate_probe(7,missing,&p));
    memset(&p,0,sizeof(p));p.machine=true;memset(p.email,'a',255);p.email[255]=0;roundtrip(&p);
    strcpy_s(p.email,256,"--install --stop");roundtrip(&p);
    wchar_t output[2048];CHECK(!l4_proxy_certificate_command(L"C:\\fixture.exe",&p,18443,59472,1000,output));
    printf("proxy certificate profile: %u passed, %u failed\n",checks-failures,failures);return failures?1:0;
}
