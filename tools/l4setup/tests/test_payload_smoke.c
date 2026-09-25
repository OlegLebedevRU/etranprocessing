#include "../src/unpack.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *message) { fprintf(stderr, "FAIL: %s (Win32=%lu)\n", message, GetLastError()); return 1; }
static bool present(const wchar_t *path) { DWORD a = GetFileAttributesW(path); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
int wmain(int argc, wchar_t **argv) {
    if (argc != 4) return fail("usage: payload_smoke <candidate-dir> <dest> <x86|x64>");
    const wchar_t *archive_dir=argv[1], *dest=argv[2], *arch=argv[3];
    char narrow_arch[8]={0};
    if (!WideCharToMultiByte(CP_UTF8,0,arch,-1,narrow_arch,sizeof(narrow_arch),NULL,NULL)) return fail("arch conversion");
    if (wcscmp(arch,L"x86")!=0 && wcscmp(arch,L"x64")!=0) return fail("invalid architecture");
    if (!unpack_payload(dest,narrow_arch,archive_dir,"1.8.1")) return fail("clean payload extraction");
    wchar_t capture[MAX_PATH], pin[MAX_PATH], config[MAX_PATH], sentinel[MAX_PATH];
    swprintf_s(capture,MAX_PATH,L"%ls\\l4capture\\bin\\l4capture.exe",dest);
    swprintf_s(pin,MAX_PATH,L"%ls\\l4pin\\l4pin.exe",dest);
    swprintf_s(config,MAX_PATH,L"%ls\\mosquitto\\mosquitto.conf",dest);
    swprintf_s(sentinel,MAX_PATH,L"%ls\\l4pin\\smoke-sentinel.txt",dest);
    if (!present(capture)||!present(pin)) return fail("installed executable missing");
    DWORD binary=0;
    if (!GetBinaryTypeW(capture,&binary) || binary!=(DWORD)(wcscmp(arch,L"x64")==0 ? SCS_64BIT_BINARY : SCS_32BIT_BINARY)) return fail("capture architecture mismatch");
    FILE *f=NULL;
    if (_wfopen_s(&f,sentinel,L"wb") || !f) return fail("sentinel create");
    fputs("previous-version",f); fclose(f);
    if (_wfopen_s(&f,config,L"wb") || !f) return fail("config create");
    fputs("user-config",f); fclose(f);
    wchar_t state[MAX_PATH]; swprintf_s(state,MAX_PATH,L"%ls\\state.json",dest);
    if (_wfopen_s(&f,state,L"wb") || !f) return fail("state create");
    fputs("{\"installed_version\": \"1.8.0\"}",f); fclose(f);
    if (!unpack_payload(dest,narrow_arch,archive_dir,"1.8.1")) return fail("upgrade payload extraction");
    if (present(sentinel)) return fail("old sentinel still live after upgrade");
    if (!present(config)) return fail("user config missing after upgrade");
    if (!unpack_rollback(dest,"1.8.0")) return fail("rollback");
    if (!present(sentinel)||!present(capture)) return fail("previous payload not restored");
    if (!GetBinaryTypeW(capture,&binary) || binary!=(DWORD)(wcscmp(arch,L"x64")==0 ? SCS_64BIT_BINARY : SCS_32BIT_BINARY)) return fail("restored architecture mismatch");
    printf("PASS: clean install, upgrade and rollback for %ls\n",arch);
    return 0;
}
