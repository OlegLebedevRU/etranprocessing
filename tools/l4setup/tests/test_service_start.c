#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
static int queries, failed_callbacks;
static ULONGLONG ticks;
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
    bool ok=services_start_single_service((SC_HANDLE)1,SVC_NAME_MOSQUITTO,120,callback,NULL);
    bool passed=!ok && queries==2 && ticks==120000 && failed_callbacks==1;
    printf("Stopped/zero-exit startup regression: %s\n",passed?"PASS":"FAIL");return passed?0:1;
}
