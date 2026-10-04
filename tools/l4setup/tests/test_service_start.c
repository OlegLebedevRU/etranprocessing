#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
static int queries, failed_callbacks;
static ULONGLONG ticks;
static const wchar_t* fixture_path=L"\"C:\\Tools Space\\leo4proxy\\leo4proxy.exe\" --service --rtp-tunnel --http-remote new.example:443 --log \"C:\\Logs Space\\\\\"";
static SC_HANDLE WINAPI test_scm(LPCWSTR host,LPCWSTR database,DWORD access) {(void)host;(void)database;(void)access;return (SC_HANDLE)1;}
static BOOL WINAPI test_config(SC_HANDLE h,LPQUERY_SERVICE_CONFIGW config,DWORD bytes,LPDWORD needed) {
    (void)h;*needed=sizeof(*config);if(!config || bytes<sizeof(*config))return FALSE;
    ZeroMemory(config,sizeof(*config));config->lpBinaryPathName=(LPWSTR)fixture_path;return TRUE;
}
static SC_HANDLE WINAPI test_open(SC_HANDLE scm,LPCWSTR name,DWORD access) {
    (void)scm;(void)name;(void)access;return (SC_HANDLE)2;
}
static BOOL WINAPI test_close(SC_HANDLE handle) {(void)handle;return TRUE;}
static BOOL WINAPI test_start(SC_HANDLE handle,DWORD argc,LPCWSTR* args) {
    (void)handle;(void)argc;(void)args;return TRUE;
}
static BOOL WINAPI test_query(SC_HANDLE handle,SC_STATUS_TYPE level,LPBYTE output,DWORD size,LPDWORD needed) {
    (void)handle;(void)level;(void)size;*needed=sizeof(SERVICE_STATUS_PROCESS);
    SERVICE_STATUS_PROCESS* s=(SERVICE_STATUS_PROCESS*)output;ZeroMemory(s,sizeof(*s));
    s->dwCurrentState=SERVICE_STOPPED;queries++;return TRUE;
}
static ULONGLONG WINAPI test_ticks(void) {ticks+=60000;return ticks;}
#define OpenServiceW test_open
#define OpenSCManagerW test_scm
#define QueryServiceConfigW test_config
#define CloseServiceHandle test_close
#define StartServiceW test_start
#define QueryServiceStatusEx test_query
#define GetTickCount64 test_ticks
#include "../src/services.c"
void log_info(const char* f, ...) {(void)f;}
void log_warn(const char* f, ...) {(void)f;}
void log_err(const char* f, ...) {(void)f;}
static void callback(const wchar_t* name,ServiceLifecycleStatus status,DWORD elapsed,const char* note,void* data) {
    (void)name;(void)elapsed;(void)note;(void)data;
    if(status==SVC_STATUS_FAILED)failed_callbacks++;
}
int main(void) {
    wchar_t retained[2048];
    assert(retain_non_network_arguments(retained,2048,L"\"C:\\Tools Space\\leo4proxy.exe\" --service --mqtt-local 127.0.0.1:1999 --http-remote old.example:443 --policy-bootstrap-ip 8.8.8.8 --no-srv --log \"C:\\Logs Space\\\\\""));
    int argc=0; LPWSTR* argv=CommandLineToArgvW(retained,&argc);
    assert(argv && argc==6 && !wcscmp(argv[2],L"--mqtt-local") && !wcscmp(argv[3],L"127.0.0.1:1999") && !wcscmp(argv[5],L"C:\\Logs Space\\"));
    LocalFree(argv); assert(!wcsstr(retained,L"http-remote") && !wcsstr(retained,L"no-srv"));
    puts("Service arguments: custom options and quoted paths preserved");
    assert(services_read_proxy_arguments(L"C:\\Tools Space",retained,2048));
    assert(services_read_proxy_arguments(L"C:/Tools Space/",retained,2048));
    argv=CommandLineToArgvW(retained,&argc);assert(argv && argc==6 && !wcscmp(argv[1],L"--rtp-tunnel") && !wcscmp(argv[3],L"new.example:443") && !wcscmp(argv[5],L"C:\\Logs Space\\"));LocalFree(argv);
    assert(!services_read_proxy_arguments(L"C:\\Wrong",retained,2048));
    assert(!services_read_proxy_arguments(L"C:\\Tools Space",retained,8));
    puts("SCM diagnostic arguments: authoritative path, quoted arguments, overflow PASS");
    CliOptions options={0};options.pin_specified=true;options.smoke_only=true;
    assert(services_read_network_options(L"C:\\Tools Space",&options));
    assert(!wcscmp(options.remote_endpoints[1],L"new.example:443") && !options.remote_endpoints[0][0]);
    assert(!options.policy_bootstrap_ip[0] && !options.no_srv && !options.network_specified && options.pin_specified && options.smoke_only);
    fixture_path=L"\"C:\\Tools Space\\leo4proxy\\leo4proxy.exe\" --service --mqtt-remote mq.example:8883 --http-remote api.example:9443 --policy-bootstrap-ip 8.8.8.8 --no-srv";
    assert(services_read_network_options(L"C:\\Tools Space",&options));
    assert(!wcscmp(options.remote_endpoints[0],L"mq.example:8883") && !wcscmp(options.remote_endpoints[1],L"api.example:9443") && options.no_srv && !wcscmp(options.policy_bootstrap_ip,L"8.8.8.8"));
    puts("Network form: current SCM values, Auto fields and unrelated options preserved PASS");
    bool ok=services_start_single_service((SC_HANDLE)1,SVC_NAME_MOSQUITTO,120,callback,NULL);
    bool passed=!ok && queries==2 && ticks==120000 && failed_callbacks==1;
    printf("Stopped/zero-exit startup regression: %s\n",passed?"PASS":"FAIL");return passed?0:1;
}
