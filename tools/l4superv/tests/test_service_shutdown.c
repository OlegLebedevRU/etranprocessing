#include <windows.h>
#include <stdio.h>
#include <stdbool.h>
#include "service_mgr.h"

enum { NORMAL, STALLED, REPLACED, SHARED, REFUSED, START_STALLED };
static int mode, queries, sleeps, killed, started, errors;
static bool stop_sent;
static SC_HANDLE WINAPI fake_manager(LPCWSTR machine, LPCWSTR database, DWORD access) {
    (void)machine; (void)database; (void)access; return (SC_HANDLE)1;
}
static SC_HANDLE WINAPI fake_service(SC_HANDLE manager, LPCWSTR name, DWORD access) {
    (void)manager; (void)name; (void)access; return (SC_HANDLE)2;
}
static BOOL WINAPI fake_close_service(SC_HANDLE handle) { (void)handle; return TRUE; }
static BOOL WINAPI fake_query(SC_HANDLE service, SC_STATUS_TYPE info, LPBYTE buffer, DWORD length, LPDWORD needed) {
    (void)service; (void)info; (void)length; (void)needed;
    SERVICE_STATUS_PROCESS* s = (SERVICE_STATUS_PROCESS*)buffer;
    ZeroMemory(s, sizeof(*s));
    queries++;
    s->dwServiceType = mode == SHARED ? SERVICE_WIN32_SHARE_PROCESS : SERVICE_WIN32_OWN_PROCESS;
    s->dwProcessId = mode == REPLACED && queries >= 43 ? 456 : 123;
    s->dwCurrentState = SERVICE_RUNNING;
    if (mode == START_STALLED) s->dwCurrentState = SERVICE_START_PENDING;
    else if (killed || (mode == NORMAL && stop_sent && sleeps >= 2)) s->dwCurrentState = SERVICE_STOPPED;
    else if (stop_sent) s->dwCurrentState = SERVICE_STOP_PENDING;
    return TRUE;
}
static BOOL WINAPI fake_control(SC_HANDLE handle, DWORD control, LPSERVICE_STATUS status) {
    (void)handle; (void)control; (void)status;
    if (mode == REFUSED) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    stop_sent = true;
    return TRUE;
}
static HANDLE WINAPI fake_process(DWORD access, BOOL inherit, DWORD pid) {
    (void)access; (void)inherit; (void)pid; return (HANDLE)3;
}
static DWORD WINAPI fake_wait(HANDLE handle, DWORD timeout) {
    (void)handle; (void)timeout; return killed ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}
static BOOL WINAPI fake_kill(HANDLE process, UINT code) { (void)process; (void)code; killed++; return TRUE; }
static BOOL WINAPI fake_close(HANDLE handle) { (void)handle; return TRUE; }
static VOID WINAPI fake_sleep(DWORD ms) { (void)ms; sleeps++; }
static BOOL WINAPI fake_start(SC_HANDLE handle, DWORD count, LPCWSTR* args) {
    (void)handle; (void)count; (void)args; started++; return TRUE;
}

#define OpenSCManagerW fake_manager
#define OpenServiceW fake_service
#define CloseServiceHandle fake_close_service
#define QueryServiceStatusEx fake_query
#define ControlService fake_control
#define OpenProcess fake_process
#define WaitForSingleObject fake_wait
#define TerminateProcess fake_kill
#define CloseHandle fake_close
#define Sleep fake_sleep
#define StartServiceW fake_start
#include "../src/service_mgr.c"

#define CHECK(x) do { if (!(x)) { printf("FAIL shutdown %d: %s\n", __LINE__, #x); errors++; } } while (0)
static void reset(int scenario) { mode = scenario; queries = sleeps = killed = started = 0; stop_sent = false; }
int main(void) {
    reset(NORMAL);
    CHECK(svc_stop_and_kill(L"IsolatedTest")); CHECK(!killed); CHECK(sleeps == 2);
    reset(STALLED);
    CHECK(svc_stop_and_kill(L"IsolatedTest")); CHECK(killed == 1); CHECK(sleeps == 40);
    reset(REPLACED);
    CHECK(!svc_stop_and_kill(L"IsolatedTest")); CHECK(!killed); CHECK(sleeps == 40);
    reset(SHARED);
    CHECK(!svc_stop_and_kill(L"IsolatedTest")); CHECK(!killed);
    reset(REFUSED);
    CHECK(!svc_restart(L"IsolatedTest")); CHECK(!killed); CHECK(!started);
    reset(START_STALLED);
    CHECK(!svc_start(L"IsolatedTest")); CHECK(started == 1);
    printf("Service shutdown failures: %d\n", errors);
    return errors ? 1 : 0;
}
