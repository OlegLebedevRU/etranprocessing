#include "tool_inventory.h"
#include "../../l4common/layout.h"

#include <windows.h>
#include <wincrypt.h>
#include <winver.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>

typedef struct {
    const char *name;
    const char *relative_path;
    const char *arch;
} ToolPath;

/* The installed layout is fixed by l4superv's package. Scan only executable files. */
static const ToolPath TOOL_PATHS[] = {
    {"l4capture", "l4capture/bin/x86/l4capture.exe", "x86"},
    {"l4capture", "l4capture/bin/x64/l4capture.exe", "x64"},
    {"l4capture", "l4capture/bin/l4capture.exe", "default"},
    {"l4con", "l4con/x86/l4con.exe", "x86"},
    {"l4con", "l4con/x64/l4con.exe", "x64"},
    {"l4con", "l4con/l4con.exe", "default"},
    {"l4desk", "l4desk/x86/l4desk.exe", "x86"},
    {"l4desk", "l4desk/x64/l4desk.exe", "x64"},
    {"l4desk", "l4desk/l4desk.exe", "default"},
    {"l4pin", "l4pin/x86/l4pin.exe", "x86"},
    {"l4pin", "l4pin/x64/l4pin.exe", "x64"},
    {"l4pin", "l4pin/l4pin.exe", "default"},
    {"l4sql", "l4sql/x86/l4sql.exe", "x86"},
    {"l4sql", "l4sql/x64/l4sql.exe", "x64"},
    {"l4sql", "l4sql/l4sql.exe", "default"},
    {"l4superv", "l4superv/x86/l4superv.exe", "x86"},
    {"l4superv", "l4superv/x64/l4superv.exe", "x64"},
    {"l4superv", "l4superv/l4superv.exe", "default"},
    {"leo4proxy", "leo4proxy/x86/leo4proxy.exe", "x86"},
    {"leo4proxy", "leo4proxy/x64/leo4proxy.exe", "x64"},
    {"leo4proxy", "leo4proxy/leo4proxy.exe", "default"},
    {"mosquitto", "mosquitto/mosquitto.exe", "default"},
    {"ffmpeg", "ffmpeg/ffmpeg.exe", "default"},
    {"l4setup", "l4setup.exe", "default"},
};

static bool appendf(char *out, size_t cap, size_t *used, const char *fmt, ...) {
    if (*used >= cap) return false;
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(out + *used, cap - *used, fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= cap - *used) return false;
    *used += (size_t)written;
    return true;
}

static bool inventory_root(wchar_t out[MAX_PATH]) {
    DWORD length = GetModuleFileNameW(NULL, out, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    wchar_t *slash = wcsrchr(out, L'\\');
    if (!slash) return false;
    *slash = L'\0';
    for (unsigned depth = 0; depth < 4; depth++) {
        slash = wcsrchr(out, L'\\');
        if (!slash) return false;
        if (_wcsicmp(slash + 1, L"l4con") == 0) {
            *slash = L'\0';
            return out[0] != L'\0';
        }
        *slash = L'\0';
    }
    return false;
}

static void hex_digest(const BYTE hash[32], char out[65]) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; i++) {
        out[i * 2] = hex[hash[i] >> 4];
        out[i * 2 + 1] = hex[hash[i] & 15];
    }
    out[64] = '\0';
}

static bool sha256_bytes(const BYTE *data, DWORD length, char out[65]) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    BYTE result[32];
    DWORD result_size = sizeof(result);
    bool ok = CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
              CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) &&
              CryptHashData(hash, data, length, 0) &&
              CryptGetHashParam(hash, HP_HASHVAL, result, &result_size, 0) &&
              result_size == sizeof(result);
    if (ok) hex_digest(result, out);
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    return ok;
}

static bool sha256_file(HANDLE file, char out[65]) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    BYTE buffer[16384], result[32];
    DWORD read = 0, result_size = sizeof(result);
    bool ok = CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
              CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
    if (ok) {
        for (;;) {
            if (!ReadFile(file, buffer, sizeof(buffer), &read, NULL)) { ok = false; break; }
            if (!read) break;
            if (!CryptHashData(hash, buffer, read, 0)) { ok = false; break; }
        }
    }
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, result, &result_size, 0) &&
                 result_size == sizeof(result);
    if (ok) hex_digest(result, out);
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    return ok;
}

static void file_version(const wchar_t *path, char out[64]) {
    out[0] = '\0';
    DWORD ignored = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &ignored);
    if (!size || size > 65536) return;
    BYTE *block = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size);
    if (!block) return;
    VS_FIXEDFILEINFO *info = NULL;
    UINT length = 0;
    if (GetFileVersionInfoW(path, 0, size, block) &&
        VerQueryValueW(block, L"\\", (LPVOID *)&info, &length) &&
        length >= sizeof(*info) && info->dwSignature == 0xFEEF04BD) {
        snprintf(out, 64, "%u.%u.%u.%u", HIWORD(info->dwFileVersionMS),
                 LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS),
                 LOWORD(info->dwFileVersionLS));
    }
    HeapFree(GetProcessHeap(), 0, block);
}

bool tool_inventory_build(char *out, size_t capacity, char digest[65]) {
    wchar_t root[MAX_PATH];
    if (!out || !digest || capacity < 3 || !inventory_root(root)) return false;
    size_t used = 0;
    if (!appendf(out, capacity, &used, "[")) return false;
    for (size_t i = 0; i < sizeof(TOOL_PATHS) / sizeof(TOOL_PATHS[0]); i++) {
        const ToolPath *entry = &TOOL_PATHS[i];
        wchar_t relative[MAX_PATH], path[MAX_PATH];
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, entry->relative_path, -1,
                                 relative, MAX_PATH)) return false;
        for (wchar_t *p = relative; *p; p++) if (*p == L'/') *p = L'\\';
        if (swprintf_s(path, MAX_PATH, L"%ls\\%ls", root, relative) < 0) return false;
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        bool present = file != INVALID_HANDLE_VALUE;
        char version[64] = { 0 }, hash[65] = { 0 }, modified[32] = { 0 };
        LARGE_INTEGER size = { 0 };
        if (present) {
            file_version(path, version);
            FILETIME file_time;
            SYSTEMTIME utc;
            if (GetFileTime(file, NULL, NULL, &file_time) && FileTimeToSystemTime(&file_time, &utc)) {
                snprintf(modified, sizeof(modified), "%04u-%02u-%02uT%02u:%02u:%02uZ",
                         utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
            }
            if (!GetFileSizeEx(file, &size) || !sha256_file(file, hash)) hash[0] = '\0';
            CloseHandle(file);
        }
        if (!appendf(out, capacity, &used,
                     "%s{\"name\":\"%s\",\"path\":\"%s\",\"arch\":\"%s\",\"present\":%s,\"file_version\":%s",
                     i ? "," : "", entry->name, entry->relative_path, entry->arch,
                     present ? "true" : "false", version[0] ? "\"" : "null")) return false;
        if (version[0] && !appendf(out, capacity, &used, "%s\"", version)) return false;
        if (!appendf(out, capacity, &used, ",\"size\":%llu,\"modified_utc\":%s",
                     (unsigned long long)size.QuadPart, modified[0] ? "\"" : "null")) return false;
        if (modified[0] && !appendf(out, capacity, &used, "%s\"", modified)) return false;
        if (!appendf(out, capacity, &used, ",\"sha256\":%s", hash[0] ? "\"" : "null")) return false;
        if (hash[0] && !appendf(out, capacity, &used, "%s\"", hash)) return false;
        if (!appendf(out, capacity, &used, "}")) return false;
    }
    if (!appendf(out, capacity, &used, "]")) return false;
    return sha256_bytes((const BYTE *)out, (DWORD)used, digest);
}

static bool read_json_version(const wchar_t *path, bool require_success, char out[64]) {
    FILE *file = NULL;
    if (_wfopen_s(&file, path, L"rb") != 0 || !file) return false;
    char json[65536];
    size_t count = fread(json, 1, sizeof(json) - 1, file);
    bool complete = feof(file) != 0;
    fclose(file);
    if (!complete) return false;
    json[count] = '\0';
    if (require_success && !strstr(json, "\"status\": \"ready\"") &&
        !strstr(json, "\"status\":\"ready\"") &&
        !strstr(json, "\"status\": \"ready_for_online\"") &&
        !strstr(json, "\"status\":\"ready_for_online\"") &&
        !strstr(json, "\"status\": \"activation_required\"") &&
        !strstr(json, "\"status\":\"activation_required\"") &&
        !strstr(json, "\"status\": \"degraded\"") &&
        !strstr(json, "\"status\":\"degraded\"")) return false;
    const char *value = strstr(json, "\"installed_version\"");
    if (!value) return false;
    value += strlen("\"installed_version\"");
    while (*value && isspace((unsigned char)*value)) value++;
    if (*value++ != ':') return false;
    while (*value && isspace((unsigned char)*value)) value++;
    if (*value++ != '"') return false;
    size_t length = 0;
    while (*value && *value != '"' && length < 63) {
        if (!(isalnum((unsigned char)*value) || *value == '.' || *value == '-' ||
              *value == '+')) return false;
        out[length++] = *value++;
    }
    out[length] = '\0';
    return length > 0 && *value == '"';
}

bool tool_inventory_package_version(char out[64]) {
    wchar_t root[MAX_PATH], path[MAX_PATH];
    out[0] = '\0';
    if (!inventory_root(root)) return false;
    /* A completed setup summary records the installed package.  state.json is
       also written by the supervisor and can contain a stale cached version. */
    if (l4_runtime_path(root, L4_DATA_STATE, L"install_summary.json", L"install_summary.json", path) &&
        read_json_version(path, true, out)) return true;
    if (!l4_runtime_path(root, L4_DATA_STATE, L"state.json", L"state.json", path)) return false;
    return read_json_version(path, false, out);
}
