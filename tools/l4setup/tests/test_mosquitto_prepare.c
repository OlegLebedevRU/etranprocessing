#include "../src/services.h"
#include <windows.h>
#include <aclapi.h>
#include <stdio.h>

static int passed = 0;
static int failed = 0;
#define CHECK(cond, label) do { if (cond) { passed++; } else { failed++; printf("FAIL: %s (Win32=%lu)\n", label, GetLastError()); } } while (0)

static bool acl_is_protected_system_admin_only(const wchar_t *path) {
    PSECURITY_DESCRIPTOR sd = NULL;
    PACL dacl = NULL;
    DWORD rc = GetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, NULL, NULL, &dacl, NULL, &sd);
    if (rc != ERROR_SUCCESS || !sd || !dacl) { if (sd) LocalFree(sd); return false; }
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    bool good = GetSecurityDescriptorControl(sd, &control, &revision) &&
        (control & SE_DACL_PROTECTED) && dacl->AceCount == 2;
    BYTE sysbuf[SECURITY_MAX_SID_SIZE], adminbuf[SECURITY_MAX_SID_SIZE];
    DWORD syslen = sizeof(sysbuf), adminlen = sizeof(adminbuf);
    good = good && CreateWellKnownSid(WinLocalSystemSid, NULL, sysbuf, &syslen) &&
        CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, adminbuf, &adminlen);
    bool seen_sys = false, seen_admin = false;
    for (DWORD i = 0; good && i < dacl->AceCount; i++) {
        ACE_HEADER *header = NULL;
        if (!GetAce(dacl, i, (void **)&header) || header->AceType != ACCESS_ALLOWED_ACE_TYPE) { good = false; break; }
        ACCESS_ALLOWED_ACE *ace = (ACCESS_ALLOWED_ACE *)header;
        if ((ace->Mask & FILE_ALL_ACCESS) != FILE_ALL_ACCESS) { good = false; break; }
        if (EqualSid(&ace->SidStart, sysbuf)) seen_sys = true;
        else if (EqualSid(&ace->SidStart, adminbuf)) seen_admin = true;
        else good = false;
    }
    LocalFree(sd);
    return good && seen_sys && seen_admin;
}

int main(void) {
    wchar_t temp[MAX_PATH], root[MAX_PATH], mosq[MAX_PATH], logdir[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"l4m", 0, root) ||
        !DeleteFileW(root) || !CreateDirectoryW(root, NULL)) return 2;
    swprintf_s(mosq, MAX_PATH, L"%ls\\mosquitto", root);
    swprintf_s(logdir, MAX_PATH, L"%ls\\mosquitto\\log", root);
    CHECK(services_prepare_mosquitto(root), "create missing mosquitto/log");
    CHECK(GetFileAttributesW(logdir) & FILE_ATTRIBUTE_DIRECTORY, "log directory exists");
    CHECK(acl_is_protected_system_admin_only(logdir), "protected SYSTEM/Admin ACL");
    CHECK(services_prepare_mosquitto(root), "idempotent repeat");
    RemoveDirectoryW(logdir);
    HANDLE f = CreateFileW(logdir, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(f != INVALID_HANDLE_VALUE, "create file collision fixture");
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    CHECK(!services_prepare_mosquitto(root), "file in place of log directory rejected");
    DeleteFileW(logdir);
    RemoveDirectoryW(mosq);
    RemoveDirectoryW(root);
    printf("mosquitto directory: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
