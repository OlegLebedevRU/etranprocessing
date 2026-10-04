/* Child exit races the last pipe read. No services, certificates or network. */
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "smoke.h"
#include "proxy_probe.h"
static bool exited;
static int errors;
static bool missing, launch_failed;
static size_t offset;
static const char* tail;
static BOOL WINAPI fixture_peek(HANDLE h,LPVOID b,DWORD n,LPDWORD read,LPDWORD available,LPDWORD left) {
    (void)h;(void)b;(void)n;(void)read;(void)left;
    *available=exited?(DWORD)(strlen(tail)-offset):0;return TRUE;
}
static BOOL WINAPI fixture_read(HANDLE h,LPVOID b,DWORD n,LPDWORD read,LPOVERLAPPED o) {
    (void)h;(void)o;size_t count=strlen(tail)-offset;if(count>n)count=n;
    memcpy(b,tail+offset,count);offset+=count;*read=(DWORD)count;return TRUE;
}
static DWORD WINAPI fixture_wait(HANDLE h,DWORD ms) {(void)h;(void)ms;exited=true;return WAIT_OBJECT_0;}
static BOOL WINAPI fixture_process(LPCWSTR app,LPWSTR cmd,LPSECURITY_ATTRIBUTES p,LPSECURITY_ATTRIBUTES t,BOOL inherit,DWORD flags,LPVOID env,LPCWSTR cwd,LPSTARTUPINFOW s,LPPROCESS_INFORMATION info) {
    (void)app;(void)cmd;(void)p;(void)t;(void)inherit;(void)flags;(void)env;(void)cwd;(void)s;
    info->hProcess=(HANDLE)3;info->hThread=(HANDLE)4;return !launch_failed;
}
static BOOL WINAPI fixture_pipe(PHANDLE r,PHANDLE w,LPSECURITY_ATTRIBUTES a,DWORD n) {(void)a;(void)n;*r=(HANDLE)1;*w=(HANDLE)2;return TRUE;}
static BOOL WINAPI fixture_close(HANDLE h) {(void)h;return TRUE;}
static BOOL WINAPI fixture_flags(HANDLE h,DWORD m,DWORD f) {(void)h;(void)m;(void)f;return TRUE;}
static DWORD WINAPI fixture_attributes(LPCWSTR p) {(void)p;return missing?INVALID_FILE_ATTRIBUTES:FILE_ATTRIBUTE_NORMAL;}
static HANDLE WINAPI fixture_file(LPCWSTR p,DWORD a,DWORD s,LPSECURITY_ATTRIBUTES sa,DWORD c,DWORD f,HANDLE t) {(void)p;(void)a;(void)s;(void)sa;(void)c;(void)f;(void)t;return (HANDLE)5;}
bool setup_proxy_probe(int p,bool r,int t) {(void)p;(void)r;(void)t;return true;}
bool services_read_proxy_arguments(const wchar_t* dest,wchar_t* out,size_t capacity) {(void)dest;wcscpy_s(out,capacity,L"--service --rtp-tunnel");return true;}
void log_info(const char* f,...) {(void)f;}
void log_warn(const char* f,...) {(void)f;}
void log_err(const char* f,...) {(void)f;}
#define PeekNamedPipe fixture_peek
#define ReadFile fixture_read
#define WaitForSingleObject fixture_wait
#define CreateProcessW fixture_process
#define CreatePipe fixture_pipe
#define CloseHandle fixture_close
#define SetHandleInformation fixture_flags
#define GetFileAttributesW fixture_attributes
#define CreateFileW fixture_file
#include "../src/smoke.c"
#define CHECK(c) do {if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);errors++;}}while(0)
static void run(const char* verdict,bool warning) {
    char output[512];
    sprintf_s(output,sizeof(output),
      "{\"v\":1,\"channel\":0,\"verdict\":\"valid\"}\n"
      "{\"v\":1,\"channel\":1,\"verdict\":\"valid\"}\n"
      "{\"v\":1,\"channel\":2,\"verdict\":\"skipped\"}\n"
      "{\"v\":1,\"channel\":3,\"verdict\":\"%s\"}\n",verdict);
    tail=output;offset=0;exited=false;SmokeProbesResult result={0};
    smoke_probe_upstream(L"Z:\\nonexistent-upstream-fixture",&result);
    CHECK(!strcmp(result.upstream_tls[0],"valid"));
    CHECK(!strcmp(result.upstream_tls[1],"valid"));
    CHECK(!strcmp(result.upstream_tls[2],"skipped"));
    CHECK(!strcmp(result.upstream_tls[3],verdict));
    CHECK(result.has_warnings==warning);
    CHECK(result.calculated_exit_code==(warning?12:0));
}
int main(void) {
    run("valid",false);run("cert_invalid",true);run("probe_failed",true);
    SmokeProbesResult result={0};missing=true;
    smoke_probe_upstream(L"Z:\\fixture",&result);
    CHECK(result.calculated_exit_code==12 && !strcmp(result.upstream_tls[0],"probe_failed"));
    missing=false;launch_failed=true;memset(&result,0,sizeof(result));
    smoke_probe_upstream(L"Z:\\fixture",&result);CHECK(result.calculated_exit_code==12);
    launch_failed=false;
    char verbose[10000];memset(verbose,'x',9000);verbose[9000]='\n';
    strcpy_s(verbose+9001,sizeof(verbose)-9001,
      "{\"v\":1,\"channel\":0,\"verdict\":\"valid\"}\n"
      "{\"v\":1,\"channel\":1,\"verdict\":\"valid\"}\n"
      "{\"v\":1,\"channel\":2,\"verdict\":\"skipped\"}\n"
      "{\"v\":1,\"channel\":3,\"verdict\":\"valid\"}\n");
    tail=verbose;offset=0;exited=false;memset(&result,0,sizeof(result));smoke_probe_upstream(L"Z:\\fixture",&result);
    CHECK(!strcmp(result.upstream_tls[3],"valid") && result.calculated_exit_code==0);
    tail="{\"v\":1,\"error\":\"no_certificate\"}\n";offset=0;exited=false;memset(&result,0,sizeof(result));
    smoke_probe_upstream(L"Z:\\fixture",&result);CHECK(result.calculated_exit_code==12 && !strcmp(result.upstream_tls[0],"no_certificate"));
    printf("Upstream pipe regression: 7 cases, %d failures\n",errors);return errors?1:0;
}
