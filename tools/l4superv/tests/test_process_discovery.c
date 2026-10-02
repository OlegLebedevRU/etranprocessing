#include "session_proc.h"
#include <tlhelp32.h>
#include <stdio.h>

static int index_value, failures, mode;
static HANDLE WINAPI fixture_snapshot(DWORD flags, DWORD pid) { (void)flags; (void)pid; return (HANDLE)1; }
static BOOL entry(LPPROCESSENTRY32W e) {
    if (index_value > 2) return FALSE;
    e->th32ProcessID = (DWORD)index_value + 100;
    wcscpy_s(e->szExeFile, MAX_PATH, index_value == 0 ? L"other.exe" : L"l4desk.exe");
    return TRUE;
}
static BOOL WINAPI fixture_first(HANDLE s, LPPROCESSENTRY32W e) { (void)s; index_value = 0; return entry(e); }
static BOOL WINAPI fixture_next(HANDLE s, LPPROCESSENTRY32W e) { (void)s; index_value++; return entry(e); }
static BOOL WINAPI fixture_session_id(DWORD pid, DWORD* session) { (void)pid; *session = mode == 1 ? 2 : 1; return TRUE; }
static HANDLE WINAPI fixture_open_process(DWORD rights, BOOL inherit, DWORD pid) {
    (void)rights; (void)inherit; return mode == 2 ? NULL : (HANDLE)(size_t)pid;
}
static BOOL WINAPI fixture_image(HANDLE p, DWORD flags, LPWSTR out, PDWORD size) {
    (void)flags;
    wcscpy_s(out, *size, (size_t)p == 101 ? L"C:\\foreign\\l4desk.exe" : L"C:\\l4tools\\l4desk\\l4desk.exe");
    return TRUE;
}
static DWORD WINAPI fixture_wait_process(HANDLE p, DWORD ms) { (void)p; (void)ms; return mode == 3 ? WAIT_OBJECT_0 : WAIT_TIMEOUT; }
static BOOL WINAPI fixture_close_handle(HANDLE p) { (void)p; return TRUE; }
#define CreateToolhelp32Snapshot fixture_snapshot
#define Process32FirstW fixture_first
#define Process32NextW fixture_next
#define ProcessIdToSessionId fixture_session_id
#define OpenProcess fixture_open_process
#define QueryFullProcessImageNameW fixture_image
#define WaitForSingleObject fixture_wait_process
#define CloseHandle fixture_close_handle
#include "../src/session_proc.c"
#define CHECK(x) do { if (!(x)) { printf("FAIL discovery %d: %s\n", __LINE__, #x); failures++; } } while (0)
int main(void) {
    DWORD pid = 0, session = 0;
    const wchar_t* expected = L"C:\\l4tools\\l4desk\\l4desk.exe";
    CHECK(sp_find_session_process(expected, 1, &pid, &session)); CHECK(pid == 102); CHECK(session == 1);
    CHECK(sp_find_session_process(L"c:\\L4TOOLS\\l4desk\\L4DESK.exe", 1, &pid, &session));
    mode = 1; CHECK(!sp_find_session_process(expected, 1, &pid, &session)); CHECK(!pid && !session);
    mode = 2; CHECK(!sp_find_session_process(expected, 1, &pid, &session));
    mode = 3; CHECK(!sp_find_session_process(expected, 1, &pid, &session));
    mode = 0; CHECK(!sp_find_session_process(expected, 0, &pid, &session));
    CHECK(!sp_find_session_process(expected, MAXDWORD, &pid, &session));
    printf("Process discovery failures: %d\n", failures);
    return failures ? 1 : 0;
}
