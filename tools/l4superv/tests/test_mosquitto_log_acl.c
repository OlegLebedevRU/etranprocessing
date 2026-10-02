#include "mosquitto_log_acl.h"
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL log ACL %d: %s (error %lu)\n", __LINE__, #x, GetLastError()); failures++; } } while (0)
static bool access_allowed(const wchar_t* path, HANDLE token, DWORD rights) {
    PSECURITY_DESCRIPTOR descriptor = NULL;
    DWORD error = GetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        NULL, NULL, NULL, NULL, &descriptor);
    if (error != ERROR_SUCCESS) { failures++; return false; }
    GENERIC_MAPPING mapping = { FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS };
    PRIVILEGE_SET privileges;
    DWORD length = sizeof(privileges), granted = 0;
    BOOL allowed = FALSE;
    BOOL ok = AccessCheck(descriptor, token, rights, &mapping, &privileges, &length, &granted, &allowed);
    LocalFree(descriptor);
    if (!ok) failures++;
    return ok && allowed;
}
int main(void) {
    wchar_t temp[MAX_PATH], root[MAX_PATH], directory[MAX_PATH], file[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH, temp));
    CHECK(swprintf_s(root, MAX_PATH, L"%lsL4LogAcl-%lu", temp, GetCurrentProcessId()) > 0);
    if (!CreateDirectoryW(root, NULL)) return 1; /* Never reuse somebody else's folder. */
    swprintf_s(directory, MAX_PATH, L"%ls\\log", root);
    swprintf_s(file, MAX_PATH, L"%ls\\mosquitto.log", directory);
    HANDLE original = NULL, restricted = NULL, token = NULL;
    BYTE sid_buffer[SECURITY_MAX_SID_SIZE]; DWORD sid_size = sizeof(sid_buffer);
    CHECK(CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, sid_buffer, &sid_size));
    SID_AND_ATTRIBUTES disabled = { sid_buffer, 0 };
    CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &original));
    CHECK(CreateRestrictedToken(original, DISABLE_MAX_PRIVILEGE, 1, &disabled, 0, NULL, 0, NULL, &restricted));
    CHECK(DuplicateToken(restricted, SecurityImpersonation, &token));
    CHECK(l4_mosquitto_log_acl(directory)); /* New logs inherit read access. */
    HANDLE output = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(output != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    if (output != INVALID_HANDLE_VALUE) {
        CHECK(WriteFile(output, "retained", 8, &written, NULL)); CloseHandle(output);
    }
    CHECK(access_allowed(file, token, FILE_GENERIC_READ));
    CHECK(!access_allowed(file, token, FILE_GENERIC_WRITE));
    CHECK(access_allowed(directory, token, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE));
    CHECK(!access_allowed(directory, token, FILE_ADD_FILE));
    CHECK(l4_set_log_acl(file, L"D:P(A;;GA;;;SY)")); /* Existing protected SYSTEM-only log. */
    CHECK(l4_mosquitto_log_acl(directory));
    CHECK(access_allowed(file, token, FILE_GENERIC_READ));
    CHECK(!access_allowed(file, token, FILE_GENERIC_WRITE));
    char content[9] = { 0 }; DWORD bytes = 0;
    HANDLE input = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(input != INVALID_HANDLE_VALUE);
    if (input != INVALID_HANDLE_VALUE) {
        CHECK(ReadFile(input, content, 8, &bytes, NULL)); CloseHandle(input);
        CHECK(bytes == 8 && !strcmp(content, "retained"));
    }
    if (token) CloseHandle(token);
    if (restricted) CloseHandle(restricted);
    if (original) CloseHandle(original);
    CHECK(DeleteFileW(file)); CHECK(RemoveDirectoryW(directory)); CHECK(RemoveDirectoryW(root));
    printf("Mosquitto log ACL failures: %d\n", failures);
    return failures ? 1 : 0;
}
