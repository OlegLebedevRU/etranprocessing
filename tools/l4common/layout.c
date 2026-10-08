#include "layout.h"
#include <shlobj.h>
#include <sddl.h>
#include <aclapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "uuid.lib")

static bool fail(DWORD error) { SetLastError(error); return false; }

static bool reserved_name(const wchar_t* segment, size_t length) {
    size_t stem = 0;
    while (stem < length && segment[stem] != L'.') ++stem;
    if (stem == 3 && (!_wcsnicmp(segment, L"CON", 3) || !_wcsnicmp(segment, L"PRN", 3) ||
        !_wcsnicmp(segment, L"AUX", 3) || !_wcsnicmp(segment, L"NUL", 3))) return true;
    if (stem == 4 && (!_wcsnicmp(segment, L"COM", 3) || !_wcsnicmp(segment, L"LPT", 3)) &&
        segment[3] >= L'1' && segment[3] <= L'9') return true;
    return (stem == 6 && !_wcsnicmp(segment, L"CONIN$", 6)) ||
        (stem == 7 && !_wcsnicmp(segment, L"CONOUT$", 7));
}

static bool simple_name(const wchar_t* value) {
    if (!value || !*value || !wcscmp(value, L".") || !wcscmp(value, L"..")) return false;
    if (reserved_name(value, wcslen(value))) return false;
    for (; *value; ++value)
        if (!((*value >= L'a' && *value <= L'z') || (*value >= L'A' && *value <= L'Z') ||
              (*value >= L'0' && *value <= L'9') || *value == L'_' || *value == L'-')) return false;
    return true;
}

static bool release_version(const wchar_t* value) {
    if (!value) return false;
    for (unsigned part = 0; part < 3; ++part) {
        if (*value < L'0' || *value > L'9') return false;
        if (*value == L'0' && value[1] >= L'0' && value[1] <= L'9') return false;
        unsigned digits = 0;
        do { ++value; if (++digits > 9) return false; } while (*value >= L'0' && *value <= L'9');
        if (part < 2) { if (*value++ != L'.') return false; }
    }
    return !*value;
}

static bool absolute_root(const wchar_t* value, wchar_t out[MAX_PATH]) {
    if (!value || wcslen(value) >= MAX_PATH ||
        !((value[0] >= L'A' && value[0] <= L'Z') || (value[0] >= L'a' && value[0] <= L'z')) ||
        value[1] != L':' || value[2] != L'\\' || !value[3]) return false;
    /* No device/UNC paths, ADS, wildcard, trailing dot/space or dot segments. */
    const wchar_t* segment = value + 3;
    for (const wchar_t* p = segment;; ++p) {
        if (*p == L'/' || *p == L':' || *p == L'"' || *p == L'<' || *p == L'>' ||
            *p == L'|' || *p == L'?' || *p == L'*' || (*p && *p < 32)) return false;
        if (!*p || *p == L'\\') {
            size_t length = (size_t)(p - segment);
            if (!length || p[-1] == L'.' || p[-1] == L' ' || reserved_name(segment, length)) return false;
            if (!*p) break;
            segment = p + 1;
        }
    }
    DWORD length = GetFullPathNameW(value, MAX_PATH, out, NULL);
    return length && length < MAX_PATH && !_wcsicmp(value, out);
}

static bool join(wchar_t out[MAX_PATH], const wchar_t* base, const wchar_t* leaf) {
    if (wcslen(base) + 1 + wcslen(leaf) >= MAX_PATH) return fail(ERROR_FILENAME_EXCED_RANGE);
    if (swprintf_s(out, MAX_PATH, L"%ls\\%ls", base, leaf) < 0) return fail(ERROR_FILENAME_EXCED_RANGE);
    return true;
}

static bool within(const wchar_t* root, const wchar_t* path) {
    size_t size = wcslen(root);
    return !_wcsnicmp(root, path, size) && (!path[size] || path[size] == L'\\');
}

bool l4_layout_from_roots(L4Layout* layout, const wchar_t* binaries,
                          const wchar_t* data, const wchar_t* version) {
    L4Layout result = {0};
    if (!layout || !release_version(version) || !absolute_root(binaries, result.binaries) ||
        !absolute_root(data, result.data) || within(result.binaries, result.data) ||
        within(result.data, result.binaries)) return fail(ERROR_INVALID_PARAMETER);
    wchar_t leaf[MAX_PATH];
    if (swprintf_s(leaf, MAX_PATH, L"releases\\%ls", version) < 0 ||
        !join(result.release, result.binaries, leaf) ||
        !join(result.launchers, result.binaries, L"bin") ||
        !join(result.config, result.data, L"config") || !join(result.state, result.data, L"state") ||
        !join(result.logs, result.data, L"logs") ||
        !join(result.operations, result.data, L"update\\operations") ||
        !join(result.cache, result.data, L"update\\cache") ||
        !join(result.staging, result.data, L"update\\staging")) return false;
    *layout = result;
    return true;
}

bool l4_layout_resolve(L4Layout* layout, const wchar_t* version) {
    PWSTR binaries = NULL, data = NULL;
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    if (info.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64 &&
        info.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_INTEL) return fail(ERROR_NOT_SUPPORTED);
    const KNOWNFOLDERID* programs = info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64
        ? &FOLDERID_ProgramFilesX64 : &FOLDERID_ProgramFiles;
    const wchar_t* program_path = NULL;
    HRESULT result;
#ifndef _WIN64
    wchar_t native_programs[MAX_PATH];
    /* Microsoft does not expose ProgramFilesX64 through Known Folders to a
     * WOW64 caller. Read the OS-owned 64-bit registry view, never ProgramW6432
     * from process environment (which a launch caller can override). */
    if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
        HKEY key = NULL;
        LONG error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion",
            0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
        DWORD type = 0, size = sizeof(native_programs);
        if (!error) {
            error = RegQueryValueExW(key, L"ProgramFilesDir", NULL, &type, (LPBYTE)native_programs, &size);
            RegCloseKey(key);
        }
        if (error || type != REG_SZ || size < sizeof(wchar_t) || size % sizeof(wchar_t) ||
            size > sizeof(native_programs) || native_programs[size / sizeof(wchar_t) - 1])
            return fail(error ? (DWORD)error : ERROR_INVALID_DATA);
        program_path = native_programs;
        result = S_OK;
    } else
#endif
    {
        result = SHGetKnownFolderPath(programs, KF_FLAG_DONT_VERIFY, NULL, &binaries);
        program_path = binaries;
    }
    if (SUCCEEDED(result)) result = SHGetKnownFolderPath(&FOLDERID_ProgramData, KF_FLAG_DONT_VERIFY, NULL, &data);
    wchar_t program_root[MAX_PATH], data_root[MAX_PATH];
    bool ok = SUCCEEDED(result) && join(program_root, program_path, L"Leo4\\Tools") &&
        join(data_root, data, L"Leo4\\Tools") && l4_layout_from_roots(layout, program_root, data_root, version);
    DWORD error = SUCCEEDED(result) ? GetLastError() : HRESULT_CODE(result);
    CoTaskMemFree(binaries);
    CoTaskMemFree(data);
    if (!ok) SetLastError(error);
    return ok;
}

bool l4_layout_component(const L4Layout* layout, const wchar_t* component,
                         const wchar_t* relative_file, wchar_t out[MAX_PATH]) {
    wchar_t directory[MAX_PATH];
    if (!layout || !out || !simple_name(component) || !relative_file || !*relative_file)
        return fail(ERROR_INVALID_PARAMETER);
    const wchar_t* segment = relative_file;
    for (const wchar_t* p = segment;; ++p) {
        if (!*p || *p == L'\\') {
            size_t length = (size_t)(p - segment);
            if (!length || p[-1] == L'.' || p[-1] == L' ' || reserved_name(segment, length) ||
                (length == 1 && *segment == L'.') ||
                (length == 2 && segment[0] == L'.' && segment[1] == L'.')) return fail(ERROR_INVALID_NAME);
            if (!*p) break;
            segment = p + 1;
        } else if (*p < 32 || wcschr(L"/:\"<>|?*", *p)) return fail(ERROR_INVALID_NAME);
    }
    return join(directory, layout->release, component) && join(out, directory, relative_file);
}

bool l4_runtime_path(const wchar_t* release, L4DataArea area,
                     const wchar_t* relative, const wchar_t* portable_relative,
                     wchar_t out[MAX_PATH]) {
    wchar_t canonical[MAX_PATH], prefix[MAX_PATH];
    L4Layout system;
    if (!out || !absolute_root(release, canonical)) return fail(ERROR_INVALID_PARAMETER);
    if (!l4_layout_resolve(&system, L"0.0.0") || !join(prefix, system.binaries, L"releases")) return false;
    const wchar_t* directory;
    const wchar_t* leaf;
    if (within(prefix, canonical)) {
        size_t length = wcslen(prefix);
        if (canonical[length] != L'\\' || !release_version(canonical + length + 1)) return fail(ERROR_INVALID_NAME);
        switch (area) {
            case L4_DATA_CONFIG: directory = system.config; break;
            case L4_DATA_STATE: directory = system.state; break;
            case L4_DATA_LOGS: directory = system.logs; break;
            default: return fail(ERROR_INVALID_PARAMETER);
        }
        leaf = relative;
    } else {
        if (area < L4_DATA_CONFIG || area > L4_DATA_LOGS) return fail(ERROR_INVALID_PARAMETER);
        directory = canonical;
        leaf = portable_relative;
    }
    /* Reuse the component relative-file validator, including device/ADS refusal. */
    L4Layout validator = {0}; wchar_t checked[MAX_PATH];
    wcscpy_s(validator.release, MAX_PATH, L"C:\\validation");
    if (!l4_layout_component(&validator, L"data", leaf, checked)) return false;
    return join(out, directory, leaf);
}

bool l4_runtime_release_from_exe(const wchar_t* exe, const wchar_t* component,
                                 wchar_t release[MAX_PATH], bool* installed) {
    wchar_t canonical[MAX_PATH], prefix[MAX_PATH]; L4Layout system;
    if (!release || !installed || !simple_name(component) || !absolute_root(exe, canonical))
        return fail(ERROR_INVALID_PARAMETER);
    wchar_t* slash = wcsrchr(canonical, L'\\');
    if (!slash || slash <= canonical + 2) return fail(ERROR_INVALID_NAME);
    *slash = 0;
    slash = wcsrchr(canonical, L'\\');
    if (slash && (!_wcsicmp(slash + 1, L"x86") || !_wcsicmp(slash + 1, L"x64"))) {
        *slash = 0; slash = wcsrchr(canonical, L'\\');
    }
    if (slash && !_wcsicmp(slash + 1, L"bin")) {
        *slash = 0; slash = wcsrchr(canonical, L'\\');
    }
    if (!slash || _wcsicmp(slash + 1, component)) return fail(ERROR_INVALID_NAME);
    *slash = 0;
    if (!l4_layout_resolve(&system, L"0.0.0") || !join(prefix, system.binaries, L"releases")) return false;
    *installed = within(prefix, canonical);
    if (*installed && (canonical[wcslen(prefix)] != L'\\' ||
        !release_version(canonical + wcslen(prefix) + 1))) return fail(ERROR_INVALID_NAME);
    wcscpy_s(release, MAX_PATH, canonical);
    return true;
}

bool l4_runtime_exe_path(const wchar_t* exe, const wchar_t* component,
                         L4DataArea area, const wchar_t* relative,
                         const wchar_t* portable_relative, wchar_t out[MAX_PATH]) {
    wchar_t release[MAX_PATH], directory[MAX_PATH]; bool installed;
    if (!l4_runtime_release_from_exe(exe, component, release, &installed)) return false;
    if (installed) return l4_runtime_path(release, area, relative, portable_relative, out);
    wcscpy_s(directory, MAX_PATH, exe);
    *wcsrchr(directory, L'\\') = 0;
    return l4_runtime_path(directory, area, relative, portable_relative, out);
}

bool l4_runtime_path_ansi(const char* release, L4DataArea area,
                          const wchar_t* relative, const wchar_t* portable_relative,
                          char out[MAX_PATH]) {
    wchar_t base[MAX_PATH], path[MAX_PATH]; BOOL substituted = FALSE;
    if (!release || !out || !MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS,
        release, -1, base, MAX_PATH)) return fail(ERROR_INVALID_PARAMETER);
    if (!l4_runtime_path(base, area, relative, portable_relative, path)) return false;
    if (!WideCharToMultiByte(CP_ACP, GetACP() == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS, path, -1, out,
        MAX_PATH, NULL, GetACP() == CP_UTF8 ? NULL : &substituted) || substituted) return fail(ERROR_NO_UNICODE_TRANSLATION);
    return true;
}

static bool protected_directory(const wchar_t* path, PSECURITY_DESCRIPTOR descriptor) {
    wchar_t parent[MAX_PATH];
    if (!absolute_root(path, parent)) return fail(ERROR_INVALID_NAME);
    HANDLE held[MAX_PATH / 2]; unsigned held_count = 0;
    bool ok = true; DWORD error = ERROR_SUCCESS;
    SECURITY_ATTRIBUTES security = {sizeof(security), descriptor, FALSE};
    PSECURITY_DESCRIPTOR ancestor_sd = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",
        SDDL_REVISION_1, &ancestor_sd, NULL)) return false;
    SECURITY_ATTRIBUTES ancestor_security = {sizeof(ancestor_security), ancestor_sd, FALSE};
    /* Hold every ancestor without FILE_SHARE_DELETE while resolving children.
     * OPEN_REPARSE_POINT + handle inspection closes the check/use rename race. */
    for (wchar_t* p = parent + 3;; ++p) {
        if (!*p || *p == L'\\') {
            wchar_t saved = *p; *p = 0;
            DWORD rights = FILE_READ_ATTRIBUTES | READ_CONTROL | (!saved ? WRITE_DAC | WRITE_OWNER : 0);
            HANDLE directory = CreateFileW(parent, rights, FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
            if (directory == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND) {
                if (!CreateDirectoryW(parent, saved ? &ancestor_security : &security)) { ok = false; error = GetLastError(); }
                else directory = CreateFileW(parent, rights, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
            }
            if (ok && directory == INVALID_HANDLE_VALUE) { ok = false; error = GetLastError(); }
            if (ok) {
                BY_HANDLE_FILE_INFORMATION info;
                if (!GetFileInformationByHandle(directory, &info)) { ok = false; error = GetLastError(); }
                else if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { ok = false; error = ERROR_ACCESS_DENIED; }
                if (ok && !saved) {
                    PACL acl = NULL; BOOL leaf_present = FALSE, leaf_defaulted = FALSE;
                    PSID owner = NULL;
                    if (!GetSecurityDescriptorDacl(descriptor, &leaf_present, &acl, &leaf_defaulted) || !leaf_present || !acl) {
                        ok = false; error = ERROR_INVALID_SECURITY_DESCR;
                    } else if (!GetSecurityDescriptorOwner(descriptor, &owner, &leaf_defaulted) || !owner) {
                        ok = false; error = ERROR_INVALID_SECURITY_DESCR;
                    } else {
                        error = SetSecurityInfo(directory, SE_FILE_OBJECT,
                            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                            owner, NULL, acl, NULL);
                        ok = error == ERROR_SUCCESS;
                    }
                }
                held[held_count++] = directory;
            } else if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
            *p = saved;
            if (!ok || !saved) break;
        }
    }
    while (held_count) CloseHandle(held[--held_count]);
    LocalFree(ancestor_sd);
    return ok ? true : fail(error);
}

/* Explicit owner assignment needs SeRestorePrivilege. Enable it only in a
 * private thread token; never change process-wide privileges or caller identity. */
static bool begin_restore(HANDLE* previous) {
    HANDLE source = NULL, temporary = NULL;
    *previous = NULL;
    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE, TRUE, previous)) {
        source = *previous;
    } else {
        if (GetLastError() != ERROR_NO_TOKEN) return false;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &source)) return false;
    }
    bool ok = DuplicateTokenEx(source, TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES | TOKEN_IMPERSONATE,
        NULL, SecurityImpersonation, TokenImpersonation, &temporary) != 0;
    TOKEN_PRIVILEGES privilege = {0};
    privilege.PrivilegeCount = 1;
    privilege.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (ok) ok = LookupPrivilegeValueW(NULL, L"SeRestorePrivilege", &privilege.Privileges[0].Luid) != 0;
    if (ok) {
        SetLastError(ERROR_SUCCESS);
        ok = AdjustTokenPrivileges(temporary, FALSE, &privilege, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
    }
    if (ok) ok = SetThreadToken(NULL, temporary) != 0;
    DWORD error = GetLastError();
    if (temporary) CloseHandle(temporary);
    if (source != *previous) CloseHandle(source);
    if (!ok && *previous) { CloseHandle(*previous); *previous = NULL; }
    return ok ? true : fail(error);
}

bool l4_layout_prepare(const L4Layout* layout) {
    if (!layout) return fail(ERROR_INVALID_PARAMETER);
    /* Validate the public struct before any write, not just at its constructor. */
    const wchar_t* version = wcsrchr(layout->release, L'\\');
    L4Layout expected;
    if (!version || !l4_layout_from_roots(&expected, layout->binaries, layout->data, version + 1) ||
        memcmp(&expected, layout, sizeof(expected))) return fail(ERROR_INVALID_PARAMETER);
    PSECURITY_DESCRIPTOR descriptor = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",
        SDDL_REVISION_1, &descriptor, NULL)) return false;
    HANDLE previous = NULL;
    if (!begin_restore(&previous)) { DWORD error = GetLastError(); LocalFree(descriptor); return fail(error); }
    wchar_t releases[MAX_PATH];wcscpy_s(releases,MAX_PATH,layout->release);
    *wcsrchr(releases,L'\\')=0;
    /* Version directories are published only after their entire inventory passes. */
    const wchar_t* paths[] = {layout->binaries, layout->data, releases,
        layout->launchers, layout->config, layout->state, layout->logs,
        layout->operations, layout->cache, layout->staging};
    bool ok = true;
    for (unsigned i = 0; i < _countof(paths) && ok; ++i) ok = protected_directory(paths[i], descriptor);
    DWORD error = GetLastError();
    if (!SetThreadToken(NULL, previous)) { ok = false; error = GetLastError(); }
    if (previous) CloseHandle(previous);
    LocalFree(descriptor);
    if (!ok) SetLastError(error);
    return ok;
}

bool l4_layout_prepare_installer(const L4Layout* layout,wchar_t executable[MAX_PATH]) {
    if(!layout || !executable || !l4_layout_prepare(layout))return false;
    const wchar_t* version=wcsrchr(layout->release,L'\\');wchar_t parent[MAX_PATH],slot[MAX_PATH];
    if(!version || swprintf_s(parent,MAX_PATH,L"%ls\\setup",layout->binaries)<0 ||
       swprintf_s(slot,MAX_PATH,L"%ls\\%ls",parent,version+1)<0 || swprintf_s(executable,MAX_PATH,L"%ls\\l4setup.exe",slot)<0)return fail(ERROR_FILENAME_EXCED_RANGE);
    PSECURITY_DESCRIPTOR descriptor=NULL;HANDLE previous=NULL;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",SDDL_REVISION_1,&descriptor,NULL))return false;
    bool ok=begin_restore(&previous);DWORD code=GetLastError();if(ok){ok=protected_directory(parent,descriptor) && protected_directory(slot,descriptor);code=GetLastError();
        if(!SetThreadToken(NULL,previous)){ok=false;code=GetLastError();}if(previous)CloseHandle(previous);}
    LocalFree(descriptor);return ok?true:fail(code);
}
bool l4_layout_data_path(const L4Layout* layout, const wchar_t* relative, wchar_t out[MAX_PATH]) {
    if (!layout || !out) return fail(ERROR_INVALID_PARAMETER);
    const wchar_t* version = wcsrchr(layout->release, L'\\'); L4Layout expected, validator = {0};
    if (!version || !l4_layout_from_roots(&expected, layout->binaries, layout->data, version + 1) ||
        memcmp(layout, &expected, sizeof(expected))) return fail(ERROR_INVALID_PARAMETER);
    wchar_t checked[MAX_PATH]; wcscpy_s(validator.release, MAX_PATH, L"C:\\validation");
    if (!l4_layout_component(&validator, L"data", relative, checked)) return false;
    return join(out, layout->data, relative);
}

bool l4_layout_prepare_shared_leaf(const L4Layout* layout, const wchar_t* relative,
                            PSID reader, PSID writer, PSID second_writer, bool private_leaf) {
    wchar_t path[MAX_PATH], sddl[2048]; LPWSTR read_text = NULL, write_text = NULL, second_text = NULL;
    if (!l4_layout_data_path(layout, relative, path) ||
        (reader && !IsValidSid(reader)) || (writer && !IsValidSid(writer)) || (second_writer && !IsValidSid(second_writer))) return fail(ERROR_INVALID_PARAMETER);
    if (writer && (IsWellKnownSid(writer, WinWorldSid) || IsWellKnownSid(writer, WinBuiltinUsersSid) ||
        IsWellKnownSid(writer, WinAuthenticatedUserSid) || IsWellKnownSid(writer, WinInteractiveSid) ||
        IsWellKnownSid(writer, WinAnonymousSid))) return fail(ERROR_ACCESS_DENIED);
    if (second_writer && (IsWellKnownSid(second_writer, WinWorldSid) || IsWellKnownSid(second_writer, WinBuiltinUsersSid) ||
        IsWellKnownSid(second_writer, WinAuthenticatedUserSid) || IsWellKnownSid(second_writer, WinInteractiveSid) ||
        IsWellKnownSid(second_writer, WinAnonymousSid))) return fail(ERROR_ACCESS_DENIED);
    if ((reader && !ConvertSidToStringSidW(reader, &read_text)) ||
        (writer && !ConvertSidToStringSidW(writer, &write_text)) ||
        (second_writer && !ConvertSidToStringSidW(second_writer, &second_text))) {
        DWORD error = GetLastError(); LocalFree(read_text); LocalFree(write_text); LocalFree(second_text); return fail(error);
    }
    wcscpy_s(sddl, _countof(sddl), L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)");
    if (!private_leaf) wcscat_s(sddl, _countof(sddl), L"(A;OICI;GRGX;;;BU)");
    wchar_t ace[512];
    if (reader) { swprintf_s(ace, _countof(ace), L"(A;OICI;GRGX;;;%ls)", read_text); wcscat_s(sddl, _countof(sddl), ace); }
    if (writer) { swprintf_s(ace, _countof(ace), L"(A;OICI;0x1301bf;;;%ls)", write_text); wcscat_s(sddl, _countof(sddl), ace); }
    if (second_writer) { swprintf_s(ace, _countof(ace), L"(A;OICI;0x1301bf;;;%ls)", second_text); wcscat_s(sddl, _countof(sddl), ace); }
    LocalFree(read_text); LocalFree(write_text); LocalFree(second_text);
    PSECURITY_DESCRIPTOR descriptor = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, NULL)) return false;
    HANDLE previous = NULL;
    if (!begin_restore(&previous)) { DWORD error = GetLastError(); LocalFree(descriptor); return fail(error); }
    bool ok = protected_directory(path, descriptor); DWORD error = GetLastError();
    if (!SetThreadToken(NULL, previous)) { ok = false; error = GetLastError(); }
    if (previous) CloseHandle(previous);
    LocalFree(descriptor);
    return ok ? true : fail(error);
}

bool l4_layout_prepare_leaf(const L4Layout* layout, const wchar_t* relative,
                            PSID reader, PSID writer, bool private_leaf) {
    return l4_layout_prepare_shared_leaf(layout,relative,reader,writer,NULL,private_leaf);
}

bool l4_service_inventory(const wchar_t* name, L4ServiceInventory* inventory) {
    if (!name || !*name || !inventory) return fail(ERROR_INVALID_PARAMETER);
    memset(inventory, 0, sizeof(*inventory));
    SC_HANDLE manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!manager) return false;
    SC_HANDLE service = OpenServiceW(manager, name, SERVICE_QUERY_CONFIG);
    DWORD error = GetLastError();
    CloseServiceHandle(manager);
    if (!service) return error == ERROR_SERVICE_DOES_NOT_EXIST ? true : fail(error);
    DWORD size = 0;
    QueryServiceConfigW(service, NULL, 0, &size);
    error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER || !size || size > 65536) {
        CloseServiceHandle(service); return fail(error);
    }
    QUERY_SERVICE_CONFIGW* config = (QUERY_SERVICE_CONFIGW*)malloc(size);
    if (!config) { CloseServiceHandle(service); return fail(ERROR_NOT_ENOUGH_MEMORY); }
    bool ok = QueryServiceConfigW(service, config, size, &size) != 0;
    error = GetLastError();
    if (ok) {
        if (wcslen(config->lpBinaryPathName) >= _countof(inventory->image_path) ||
            wcslen(config->lpServiceStartName) >= _countof(inventory->account)) {
            ok = false; error = ERROR_INSUFFICIENT_BUFFER;
        } else {
            inventory->installed = true;
            inventory->start_type = config->dwStartType;
            wcscpy_s(inventory->image_path, _countof(inventory->image_path), config->lpBinaryPathName);
            wcscpy_s(inventory->account, _countof(inventory->account), config->lpServiceStartName);
        }
    }
    free(config);
    CloseServiceHandle(service);
    return ok ? true : fail(error);
}

bool l4_layout_owner_begin(HANDLE* previous) { return begin_restore(previous); }
bool l4_layout_owner_end(HANDLE previous) {
    bool ok=SetThreadToken(NULL,previous)!=0;DWORD code=GetLastError();
    if(previous)CloseHandle(previous);return ok?true:fail(code);
}
