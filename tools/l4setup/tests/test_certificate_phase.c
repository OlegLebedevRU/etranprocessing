/* Real certificate phase and child launch with simulated CA/discovery.
 * No certificate stores, network or Windows services are touched. */
#include "cert_phase.h"
#include "preflight.h"
#include "../../l4superv/src/hardware_fingerprint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int discoveries, invalid_cert, unexpected_service_calls;
void log_info(const char* f, ...) {(void)f;}
void log_warn(const char* f, ...) {(void)f;}
void log_err(const char* f, ...) {(void)f;}
bool install_root_ca_certificate_ex(const wchar_t* p) {(void)p;return true;}
bool hw_get_fingerprint(char* p,size_t n) {if(n)p[0]=0;return false;}
const char* cert_state_to_str(cert_state s) {(void)s;return "fixture";}
cert_state cert_discover(const wchar_t* expected,cert_info* out) {
    (void)expected;memset(out,0,sizeof(*out));
    if(++discoveries==1 || invalid_cert)return CERT_ABSENT;
    strcpy_s(out->sn,sizeof(out->sn),"a4b0000035c66925d210826");
    strcpy_s(out->thumbprint_hex,sizeof(out->thumbprint_hex),"0000000000000000000000000000000000000000");
    return CERT_VALID;
}
void cli_clean_pin(CliOptions* o) {SecureZeroMemory(o->pin,sizeof(o->pin));o->pin_specified=false;}
bool services_control_l4superv(DWORD c) {(void)c;unexpected_service_calls++;return false;}
bool setup_proxy_probe(int p,bool r,int t) {(void)p;(void)r;(void)t;unexpected_service_calls++;return false;}
int wmain(int argc,wchar_t** argv) {
    if(argc>1)return wcscmp(argv[1],L"123456")==0?0:1; /* enrollment child fixture */
    wchar_t temp[MAX_PATH],base[MAX_PATH],dir[MAX_PATH],exe[MAX_PATH],self[MAX_PATH];
    GetTempPathW(MAX_PATH,temp);GetTempFileNameW(temp,L"l4c",0,base);DeleteFileW(base);CreateDirectoryW(base,NULL);
    swprintf_s(dir,MAX_PATH,L"%ls\\l4pin",base);CreateDirectoryW(dir,NULL);
    swprintf_s(exe,MAX_PATH,L"%ls\\l4pin.exe",dir);GetModuleFileNameW(NULL,self,MAX_PATH);
    if(!CopyFileW(self,exe,FALSE))return 1;
    int failures=0;
    for(int i=0;i<2;i++) {
        CliOptions opts={0};CertPhaseResult result={0};opts.pin_specified=true;opts.silent=true;
        wcscpy_s(opts.pin,_countof(opts.pin),L"123456");discoveries=0;invalid_cert=i;
        ULONGLONG start=GetTickCount64();bool ok=cert_phase_execute(base,&opts,NULL,&result);
        if(ok!=(i==0) || result.exit_code!=(i?26:0) || opts.pin_specified || opts.pin[0] ||
           unexpected_service_calls || GetTickCount64()-start>4000 ||
           (i==0 && strcmp(result.sn,"a4b0000035c66925d210826"))) failures++;
    }
    DeleteFileW(exe);RemoveDirectoryW(dir);RemoveDirectoryW(base);
    printf("Certificate phase: 2 cases, %d failures\n",failures);return failures?1:0;
}
