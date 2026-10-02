#pragma once
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <aclapi.h>
#include <sddl.h>

static bool l4_set_log_acl(const wchar_t* path, const wchar_t* sddl) {
    PSECURITY_DESCRIPTOR descriptor = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, NULL))
        return false;
    PACL acl = NULL;
    BOOL present = FALSE, defaulted = FALSE;
    DWORD error = ERROR_INVALID_SECURITY_DESCR;
    if (GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) && present && acl)
        error = SetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL, acl, NULL);
    LocalFree(descriptor);
    if (error != ERROR_SUCCESS) SetLastError(error);
    return error == ERROR_SUCCESS;
}

/* Only the Mosquitto log directory/file: Users read; SYSTEM/Admins full control.
 * Do not change owner, content, other log files or unrelated suite directories. */
static bool l4_mosquitto_log_acl(const wchar_t* directory) {
    if (!directory || !directory[0]) { SetLastError(ERROR_INVALID_PARAMETER); return false; }
    if (!CreateDirectoryW(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    DWORD attrs = GetFileAttributesW(directory);
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
        SetLastError(ERROR_INVALID_NAME); return false;
    }
    if (!l4_set_log_acl(directory, L"D:P(A;OICI;GA;;;SY)(A;OICI;GA;;;BA)(A;OICI;GRGX;;;BU)")) return false;
    wchar_t file[MAX_PATH];
    if (swprintf_s(file, MAX_PATH, L"%ls\\mosquitto.log", directory) < 0) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE); return false;
    }
    attrs = GetFileAttributesW(file);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    if (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
        SetLastError(ERROR_INVALID_NAME); return false;
    }
    return l4_set_log_acl(file, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GR;;;BU)");
}
