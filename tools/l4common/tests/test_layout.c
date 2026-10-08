#include "../layout.h"
#include <aclapi.h>
#include <stdio.h>
#include <wchar.h>

static unsigned checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    ++failures; printf("FAIL line %u: %s (win32=%lu)\n", __LINE__, #condition, GetLastError()); \
} } while (0)

static void remove_owned_tree(const wchar_t* path) {
    wchar_t pattern[MAX_PATH], child[MAX_PATH];
    swprintf_s(pattern, MAX_PATH, L"%ls\\*", path);
    WIN32_FIND_DATAW entry;
    HANDLE find = FindFirstFileW(pattern, &entry);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(entry.cFileName, L".") || !wcscmp(entry.cFileName, L"..")) continue;
            swprintf_s(child, MAX_PATH, L"%ls\\%ls", path, entry.cFileName);
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) remove_owned_tree(child);
                else RemoveDirectoryW(child);
            } else DeleteFileW(child);
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    RemoveDirectoryW(path);
}

static bool user_access(const wchar_t* path, DWORD access) {
    PSECURITY_DESCRIPTOR descriptor = NULL;
    DWORD error = GetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        NULL, NULL, NULL, NULL, &descriptor);
    if (error) return false;
    HANDLE process_token = NULL, restricted = NULL, impersonation = NULL;
    BYTE sid[SECURITY_MAX_SID_SIZE]; DWORD sid_size = sizeof(sid);
    bool ok = CreateWellKnownSid(WinBuiltinUsersSid, NULL, sid, &sid_size) != 0;
    SID_AND_ATTRIBUTES restrict_to = {sid, 0};
    if (ok) ok = OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &process_token) != 0;
    if (ok) ok = CreateRestrictedToken(process_token, DISABLE_MAX_PRIVILEGE,
        0, NULL, 0, NULL, 1, &restrict_to, &restricted) != 0;
    if (ok) ok = DuplicateToken(restricted, SecurityImpersonation, &impersonation) != 0;
    GENERIC_MAPPING mapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    MapGenericMask(&access, &mapping);
    PRIVILEGE_SET privileges; DWORD size = sizeof(privileges), granted = 0; BOOL allowed = FALSE;
    if (ok) ok = AccessCheck(descriptor, impersonation, access, &mapping,
        &privileges, &size, &granted, &allowed) != 0;
    if (impersonation) CloseHandle(impersonation);
    if (restricted) CloseHandle(restricted);
    if (process_token) CloseHandle(process_token);
    LocalFree(descriptor);
    return ok && allowed;
}

static bool administrators_own(const wchar_t* path) {
    PSECURITY_DESCRIPTOR descriptor = NULL; PSID owner = NULL;
    DWORD error = GetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
        &owner, NULL, NULL, NULL, &descriptor);
    if (error) return false;
    BYTE sid[SECURITY_MAX_SID_SIZE]; DWORD size = sizeof(sid);
    bool ok = CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, sid, &size) && EqualSid(owner, sid);
    LocalFree(descriptor);
    return ok;
}

static void path_tests(void) {
    L4Layout layout;
    wchar_t exe[MAX_PATH];
    CHECK(l4_layout_from_roots(&layout, L"D:\\Programs\\Leo4\\Tools", L"E:\\Data\\Leo4\\Tools", L"1.13.2"));
    CHECK(!wcscmp(layout.release, L"D:\\Programs\\Leo4\\Tools\\releases\\1.13.2"));
    CHECK(l4_layout_component(&layout, L"l4capture", L"bin\\l4capture.exe", exe));
    CHECK(!wcscmp(exe, L"D:\\Programs\\Leo4\\Tools\\releases\\1.13.2\\l4capture\\bin\\l4capture.exe"));
    CHECK(!l4_layout_component(&layout, L"..", L"l4con.exe", exe));
    CHECK(!l4_layout_component(&layout, L"l4con", L"..\\escape.exe", exe));
    CHECK(!l4_layout_component(&layout, L"l4con", L"x.exe:payload", exe));
    CHECK(!l4_layout_component(&layout, L"l4con", L"\\outside.exe", exe));
    CHECK(!l4_layout_component(&layout, L"l4con", L"bin\\..\\outside.exe", exe));
    CHECK(!l4_layout_component(&layout, L"l4con", L"bad.\\x.exe", exe));
    CHECK(!l4_layout_component(&layout, L"l4con", L"CON.txt", exe));
    CHECK(!l4_layout_component(&layout, L"COM1", L"x.exe", exe));
    CHECK(!l4_layout_from_roots(&layout, L"C:\\CON", L"D:\\Data", L"1.13.2"));
    CHECK(!l4_layout_from_roots(&layout, L"C:\\Suite", L"c:\\suite\\Data", L"1.13.2"));
    CHECK(!l4_layout_from_roots(&layout, L"C:\\Suite\\bin", L"C:\\Suite", L"1.13.2"));
    CHECK(!l4_layout_from_roots(&layout, L"C:\\Suite", L"C:\\Suite", L"1.13.2"));
    CHECK(l4_layout_from_roots(&layout, L"C:\\Suite", L"C:\\SuiteData", L"1.13.2"));
    CHECK(!l4_layout_from_roots(&layout, L"C:relative", L"D:\\Data", L"1.13.2"));
    CHECK(!l4_layout_from_roots(&layout, L"\\\\host\\share", L"D:\\Data", L"1.13.2"));
    CHECK(!l4_layout_from_roots(&layout, L"C:\\Suite\\..\\Other", L"D:\\Data", L"1.13.2"));
    CHECK(!l4_layout_from_roots(&layout, L"C:\\Suite", L"D:\\Data", L"latest"));
    CHECK(!l4_layout_from_roots(&layout, L"C:\\Suite", L"D:\\Data", L"1.13.2\\escape"));
    CHECK(!l4_layout_from_roots(&layout, L"C:\\Suite", L"D:\\Data", L"01.13.2"));
    CHECK(!l4_layout_from_roots(&layout, L"C:\\Suite", L"D:\\Data", L"1.13"));
    CHECK(!l4_layout_from_roots(&layout, L"C:\\Suite", L"D:\\Data", L"1.13.2.0"));
    wchar_t long_root[MAX_PATH];
    wcscpy_s(long_root, MAX_PATH, L"C:\\");
    for (unsigned n = 3; n < MAX_PATH - 2; ++n) long_root[n] = L'a';
    long_root[MAX_PATH - 2] = 0;
    CHECK(!l4_layout_from_roots(&layout, long_root, L"D:\\Data", L"1.13.2"));
    CHECK(l4_layout_resolve(&layout, L"1.13.2"));
    CHECK(wcsstr(layout.binaries, L"\\Leo4\\Tools") != NULL);
    CHECK(wcsstr(layout.binaries, L"(x86)") == NULL); /* Native OS root, even from x86 setup. */
    CHECK(wcsstr(layout.data, L"\\Leo4\\Tools") != NULL);
    wchar_t mapped[MAX_PATH], expected[MAX_PATH];
    CHECK(l4_layout_component(&layout, L"l4capture", L"bin\\l4capture.exe", exe));
    CHECK(l4_runtime_exe_path(exe, L"l4capture", L4_DATA_LOGS,
        L"l4capture\\l4capture.log", L"l4capture.log", mapped));
    swprintf_s(expected, MAX_PATH, L"%ls\\l4capture\\l4capture.log", layout.logs);
    CHECK(!wcscmp(mapped, expected));
    CHECK(l4_runtime_exe_path(L"D:\\dev\\tools\\l4capture\\bin\\x64\\l4capture.exe",
        L"l4capture", L4_DATA_LOGS, L"l4capture\\l4capture.log", L"l4capture.log", mapped));
    CHECK(!wcscmp(mapped, L"D:\\dev\\tools\\l4capture\\bin\\x64\\l4capture.log"));
    CHECK(!l4_runtime_exe_path(exe, L"leo4proxy", L4_DATA_LOGS,
        L"leo4proxy\\crash.log", L"crash.log", mapped));
    CHECK(!l4_runtime_exe_path(L"C:\\relative\\..\\l4capture\\l4capture.exe", L"l4capture",
        L4_DATA_LOGS, L"l4capture\\capture.log", L"capture.log", mapped));
    swprintf_s(exe, MAX_PATH, L"%ls\\releases\\bad-version\\l4capture\\bin\\l4capture.exe", layout.binaries);
    CHECK(!l4_runtime_exe_path(exe, L"l4capture", L4_DATA_LOGS,
        L"l4capture\\capture.log", L"capture.log", mapped));
    L4ServiceInventory service;
    CHECK(l4_service_inventory(L"L4LayoutTest_Service_Does_Not_Exist_57810", &service));
    CHECK(!service.installed && !service.account[0] && !service.image_path[0]);
}

int wmain(void) {
    path_tests();
    wchar_t temp[MAX_PATH], root[MAX_PATH], programs[MAX_PATH], data[MAX_PATH], link[MAX_PATH];
    DWORD length = GetTempPathW(MAX_PATH, temp);
    if (!length || length >= MAX_PATH) return 1;
    swprintf_s(root, MAX_PATH, L"%lsl4layout-test-%lu-%llu", temp, GetCurrentProcessId(), GetTickCount64());
    /* A unique owned directory is mandatory before any recursive test cleanup. */
    if (!CreateDirectoryW(root, NULL)) return 1;
    swprintf_s(programs, MAX_PATH, L"%ls\\Programs", root);
    swprintf_s(data, MAX_PATH, L"%ls\\Data", root);
    swprintf_s(link, MAX_PATH, L"%ls\\link", root);
    L4Layout layout;
    CHECK(l4_layout_from_roots(&layout, programs, data, L"1.13.2"));
    CHECK(l4_layout_prepare(&layout));
    CHECK(GetFileAttributesW(layout.release)==INVALID_FILE_ATTRIBUTES);
    wchar_t releases[MAX_PATH];wcscpy_s(releases,MAX_PATH,layout.release);*wcsrchr(releases,L'\\')=0;
    DWORD attrs = GetFileAttributesW(releases);
    CHECK(attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
    attrs = GetFileAttributesW(layout.staging);
    CHECK(attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
    CHECK(user_access(releases, GENERIC_READ | GENERIC_EXECUTE));
    CHECK(!user_access(releases, FILE_ADD_FILE));
    CHECK(!user_access(layout.config, FILE_ADD_FILE));
    CHECK(!user_access(layout.operations, FILE_ADD_FILE));
    CHECK(!user_access(layout.config, WRITE_DAC | WRITE_OWNER));
    CHECK(administrators_own(layout.config));
    HANDLE thread_token = NULL;
    CHECK(!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &thread_token) && GetLastError() == ERROR_NO_TOKEN);
    if (thread_token) CloseHandle(thread_token);
    wchar_t protected_file[MAX_PATH];
    swprintf_s(protected_file, MAX_PATH, L"%ls\\control.json", layout.config);
    HANDLE file = CreateFileW(protected_file, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(file != INVALID_HANDLE_VALUE);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    CHECK(user_access(protected_file, GENERIC_READ));
    CHECK(!user_access(protected_file, GENERIC_WRITE));
    CHECK(l4_layout_prepare(&layout)); /* Idempotent protected tree. */
    L4Layout invalid = layout;
    wcscpy_s(invalid.cache, MAX_PATH, L"C:\\outside");
    CHECK(!l4_layout_prepare(&invalid));
    /* A directory link inside our owned tree must not redirect the writer. */
    CHECK(CreateSymbolicLinkW(link, data, SYMBOLIC_LINK_FLAG_DIRECTORY));
    CHECK(l4_layout_from_roots(&layout, link, programs, L"1.13.2"));
    CHECK(!l4_layout_prepare(&layout));
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    remove_owned_tree(root);
    CHECK(GetFileAttributesW(root) == INVALID_FILE_ATTRIBUTES);
    printf("layout: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
