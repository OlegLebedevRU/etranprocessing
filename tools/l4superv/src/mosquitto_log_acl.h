#pragma once
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <aclapi.h>
#include <sddl.h>

/* Mosquitto's Windows mosquitto_fopen(..., restrict_read=true) creates logs
 * with an explicit current-user-only DACL. Reconcile this one public diagnostic
 * file to the installer-owned parent policy; preserve custom service writers.
 * Never modify the directory policy or the file's owner/content. Hold every
 * ancestor and the file without delete sharing to refuse redirected paths. */
static bool l4_mosquitto_log_inherit(const wchar_t* directory) {
    if (!directory || wcslen(directory) < 3 || directory[1] != L':' || directory[2] != L'\\') {
        SetLastError(ERROR_INVALID_NAME); return false;
    }
    wchar_t path[MAX_PATH], prefix[MAX_PATH];
    if (swprintf_s(path, MAX_PATH, L"%ls\\mosquitto.log", directory) < 0) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE); return false;
    }
    wcscpy_s(prefix, MAX_PATH, directory);
    HANDLE held[MAX_PATH / 2]; unsigned count = 0;
    bool ok = true; DWORD error = ERROR_SUCCESS;
    for (wchar_t* p = prefix + 3;; ++p) if (!*p || *p == L'\\') {
        wchar_t saved = *p; *p = 0;
        HANDLE h = CreateFileW(prefix, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        BY_HANDLE_FILE_INFORMATION info;
        if (h == INVALID_HANDLE_VALUE) { ok = false; error = GetLastError(); }
        else {
            held[count++] = h;
            if (!GetFileInformationByHandle(h, &info)) { ok = false; error = GetLastError(); }
            else if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { ok = false; error = ERROR_ACCESS_DENIED; }
        }
        *p = saved;
        if (!ok || !saved) break;
    }
    HANDLE file = INVALID_HANDLE_VALUE;
    if (ok) {
        file = CreateFileW(path, READ_CONTROL | WRITE_DAC,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (file == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND)
            file = CreateFileW(path, READ_CONTROL | WRITE_DAC,
                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (file == INVALID_HANDLE_VALUE) { ok = false; error = GetLastError(); }
    }
    if (ok) {
        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(file, &info)) { ok = false; error = GetLastError(); }
        else if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            info.nNumberOfLinks != 1) { ok = false; error = ERROR_ACCESS_DENIED; }
    }
    if (ok) {
        /* Empty explicit ACL, not a NULL DACL: Windows merges only the actual
         * inheritable parent ACEs. Private service/control directories untouched. */
        ACL empty;
        if (!InitializeAcl(&empty, sizeof(empty), ACL_REVISION)) { ok = false; error = GetLastError(); }
        else {
            error = SetSecurityInfo(file, SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                NULL, NULL, &empty, NULL);
            ok = error == ERROR_SUCCESS;
        }
    }
    if (ok) {
        PSECURITY_DESCRIPTOR sd = NULL; PACL acl = NULL;
        error = GetSecurityInfo(file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            NULL, NULL, &acl, NULL, &sd);
        ok = error == ERROR_SUCCESS && acl != NULL;
        if (ok) {
            BYTE users[SECURITY_MAX_SID_SIZE]; DWORD size = sizeof(users);
            TRUSTEE_W trustee = {0}; ACCESS_MASK rights = 0;
            ok = CreateWellKnownSid(WinBuiltinUsersSid, NULL, users, &size) != 0;
            if (ok) {
                BuildTrusteeWithSidW(&trustee, users);
                error = GetEffectiveRightsFromAclW(acl, &trustee, &rights);
                GENERIC_MAPPING mapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
                MapGenericMask(&rights, &mapping);
                ok = error == ERROR_SUCCESS && (rights & FILE_GENERIC_READ) == FILE_GENERIC_READ &&
                    !(rights & (FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE | WRITE_DAC | WRITE_OWNER));
            }
            if (!ok && error == ERROR_SUCCESS) error = ERROR_ACCESS_DENIED;
        }
        if (sd) LocalFree(sd);
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    while (count) CloseHandle(held[--count]);
    if (!ok) SetLastError(error);
    return ok;
}

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
