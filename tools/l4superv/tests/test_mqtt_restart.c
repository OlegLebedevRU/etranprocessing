#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "service_mgr.h"

enum { OK, CON_STOP_FAIL, BROKER_STOP_FAIL, BROKER_START_FAIL, CON_START_FAIL, CON_ABSENT, QUERY_FAIL };
static int mode, failures, kills;
static DWORD states[4];
static char fixture_actions[32];
static size_t action_count;
static void action(char value) { fixture_actions[action_count++] = value; fixture_actions[action_count] = 0; }
static SC_HANDLE WINAPI fixture_manager(LPCWSTR a, LPCWSTR b, DWORD c) {
    (void)a; (void)b; (void)c; return (SC_HANDLE)1;
}
static SC_HANDLE WINAPI fixture_service(SC_HANDLE a, LPCWSTR name, DWORD rights) {
    (void)a; (void)rights;
    if (!_wcsicmp(name, SVC_NAME_L4CON)) {
        if (mode == CON_ABSENT || mode == QUERY_FAIL) {
            SetLastError(mode == CON_ABSENT ? ERROR_SERVICE_DOES_NOT_EXIST : ERROR_ACCESS_DENIED);
            return NULL;
        }
        return (SC_HANDLE)2;
    }
    return (SC_HANDLE)3;
}
static BOOL WINAPI fixture_query(SC_HANDLE h, SC_STATUS_TYPE t, LPBYTE buf, DWORD size, LPDWORD needed) {
    (void)t; (void)size; (void)needed;
    SERVICE_STATUS_PROCESS* s = (SERVICE_STATUS_PROCESS*)buf;
    ZeroMemory(s, sizeof(*s));
    s->dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    s->dwCurrentState = states[(size_t)h]; s->dwProcessId = (DWORD)(size_t)h + 100;
    return TRUE;
}
static BOOL WINAPI fixture_control(SC_HANDLE h, DWORD c, LPSERVICE_STATUS s) {
    (void)c; (void)s;
    if ((h == (SC_HANDLE)2 && mode == CON_STOP_FAIL) || (h == (SC_HANDLE)3 && mode == BROKER_STOP_FAIL)) {
        SetLastError(ERROR_ACCESS_DENIED); return FALSE;
    }
    if (h == (SC_HANDLE)3 && states[2] != SERVICE_STOPPED) {
        SetLastError(ERROR_DEPENDENT_SERVICES_RUNNING); return FALSE;
    }
    action(h == (SC_HANDLE)2 ? 'c' : 'b'); states[(size_t)h] = SERVICE_STOPPED;
    return TRUE;
}
static BOOL WINAPI fixture_start(SC_HANDLE h, DWORD c, LPCWSTR* a) {
    (void)c; (void)a;
    action(h == (SC_HANDLE)2 ? 'C' : 'B');
    if ((h == (SC_HANDLE)3 && mode == BROKER_START_FAIL) || (h == (SC_HANDLE)2 && mode == CON_START_FAIL)) {
        SetLastError(ERROR_SERVICE_LOGON_FAILED); return FALSE;
    }
    states[(size_t)h] = SERVICE_RUNNING; return TRUE;
}
static BOOL WINAPI fixture_close_service(SC_HANDLE h) { (void)h; return TRUE; }
static HANDLE WINAPI fixture_process(DWORD a, BOOL b, DWORD c) { (void)a; (void)b; (void)c; return (HANDLE)9; }
static BOOL WINAPI fixture_close_handle(HANDLE h) { (void)h; return TRUE; }
static VOID WINAPI fixture_sleep_ms(DWORD ms) { (void)ms; }
static DWORD WINAPI fixture_wait_process(HANDLE h, DWORD ms) { (void)h; (void)ms; return WAIT_TIMEOUT; }
static BOOL WINAPI fixture_kill(HANDLE h, UINT code) { (void)h; (void)code; kills++; return TRUE; }
#define OpenSCManagerW fixture_manager
#define OpenServiceW fixture_service
#define QueryServiceStatusEx fixture_query
#define ControlService fixture_control
#define StartServiceW fixture_start
#define CloseServiceHandle fixture_close_service
#define OpenProcess fixture_process
#define CloseHandle fixture_close_handle
#define Sleep fixture_sleep_ms
#define WaitForSingleObject fixture_wait_process
#define TerminateProcess fixture_kill
#include "../src/service_mgr.c"

#define CHECK(x) do { if (!(x)) { printf("FAIL MQTT restart %d: %s\n", __LINE__, #x); failures++; } } while (0)
static void reset(int scenario) {
    mode = scenario; states[2] = states[3] = SERVICE_RUNNING;
    if (mode == CON_ABSENT) states[2] = SERVICE_STOPPED;
    action_count = kills = 0; fixture_actions[0] = 0;
}
int main(void) {
    reset(OK); CHECK(svc_restart_mqtt_stack()); CHECK(!strcmp(fixture_actions, "cbBC")); CHECK(!kills);
    reset(OK); states[2] = SERVICE_STOPPED;
    CHECK(svc_restart_mqtt_stack()); CHECK(!strcmp(fixture_actions, "bB")); CHECK(states[2] == SERVICE_STOPPED);
    reset(CON_ABSENT); CHECK(svc_restart_mqtt_stack()); CHECK(!strcmp(fixture_actions, "bB"));
    reset(CON_STOP_FAIL); CHECK(!svc_restart_mqtt_stack()); CHECK(!fixture_actions[0]); CHECK(!kills);
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    reset(BROKER_STOP_FAIL); CHECK(!svc_restart_mqtt_stack()); CHECK(!strcmp(fixture_actions, "cC"));
    CHECK(states[2] == SERVICE_RUNNING); CHECK(!kills); CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    reset(BROKER_START_FAIL); CHECK(!svc_restart_mqtt_stack()); CHECK(!strcmp(fixture_actions, "cbB"));
    CHECK(states[2] == SERVICE_STOPPED); CHECK(GetLastError() == ERROR_SERVICE_LOGON_FAILED);
    reset(CON_START_FAIL); CHECK(!svc_restart_mqtt_stack()); CHECK(!strcmp(fixture_actions, "cbBC"));
    CHECK(GetLastError() == ERROR_SERVICE_LOGON_FAILED);
    reset(QUERY_FAIL); CHECK(!svc_restart_mqtt_stack()); CHECK(!fixture_actions[0]); CHECK(!kills);
    printf("MQTT restart failures: %d\n", failures);
    return failures ? 1 : 0;
}
