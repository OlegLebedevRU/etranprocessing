#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include <stdbool.h>
#include <stdio.h>
#include "proxy_probe.h"

static ULONGLONG clock_ms;
static int ready_after, scans, sleeps, failures;
static DWORD current_session = 1;
static ULONGLONG WINAPI fixture_clock(void) { return clock_ms; }
static VOID WINAPI fixture_sleep(DWORD ms) { clock_ms += ms; sleeps++; }
static DWORD WINAPI fixture_session(void) { return current_session; }
static HANDLE WINAPI fixture_snapshot(DWORD flags, DWORD pid) { (void)flags; (void)pid; scans++; return (HANDLE)1; }
static BOOL WINAPI fixture_first(HANDLE h, LPPROCESSENTRY32W p) {
    (void)h;
    if (ready_after < 0 || scans < ready_after) return FALSE;
    wcscpy_s(p->szExeFile, MAX_PATH, L"l4desk.exe"); p->th32ProcessID = 123; return TRUE;
}
static BOOL WINAPI fixture_next(HANDLE h, LPPROCESSENTRY32W p) { (void)h; (void)p; return FALSE; }
static BOOL WINAPI fixture_pid_session(DWORD pid, DWORD* session) { (void)pid; *session = 1; return TRUE; }
static BOOL WINAPI fixture_close(HANDLE h) { (void)h; return TRUE; }
bool setup_proxy_probe(int port, bool ready, int timeout) { (void)port; (void)ready; (void)timeout; return true; }
void log_info(const char* format, ...) { (void)format; }
void log_warn(const char* format, ...) { (void)format; }
void log_err(const char* format, ...) { (void)format; }
#define GetTickCount64 fixture_clock
#define Sleep fixture_sleep
#define WTSGetActiveConsoleSessionId fixture_session
#define CreateToolhelp32Snapshot fixture_snapshot
#define Process32FirstW fixture_first
#define Process32NextW fixture_next
#define ProcessIdToSessionId fixture_pid_session
#define CloseHandle fixture_close
#include "../src/smoke.c"
#define CHECK(x) do { if (!(x)) { printf("FAIL desk wait %d: %s\n", __LINE__, #x); failures++; } } while (0)
static void reset(int after) { clock_ms = 0; scans = sleeps = 0; ready_after = after; current_session = 1; }
int main(void) {
    reset(1); CHECK(wait_l4desk_in_session(1)); CHECK(!sleeps);
    reset(4); CHECK(wait_l4desk_in_session(1)); CHECK(sleeps == 3); CHECK(clock_ms == 750);
    reset(-1); CHECK(!wait_l4desk_in_session(1)); CHECK(clock_ms == 15000); CHECK(scans == 61);
    reset(-1); CHECK(!wait_l4desk_in_session(0)); CHECK(!scans && !sleeps);
    reset(-1); CHECK(!wait_l4desk_in_session(MAXDWORD)); CHECK(!scans && !sleeps);
    reset(-1); current_session = 2; CHECK(!wait_l4desk_in_session(1)); CHECK(!scans && !sleeps);
    printf("Desk startup wait failures: %d\n", failures);
    return failures ? 1 : 0;
}
