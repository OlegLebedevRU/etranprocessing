#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/config.h"
#include "../src/service_mgr.h"
#include "../../l4common/layout.h"

int wmain(void) {
    L4Layout layout; L4SupervConfig config;
    wchar_t exe[MAX_PATH], path[MAX_PATH], expected[MAX_PATH];
    assert(l4_layout_resolve(&layout, L"1.13.2"));
    assert(l4_layout_component(&layout, L"l4superv", L"l4superv.exe", exe));
    assert(SetEnvironmentVariableW(L"L4_TOOLS_BASE_PATH", L"C:\\caller-controlled"));
    config_init_defaults(&config, exe);
    assert(!wcscmp(config.base_path, layout.release));
    swprintf_s(expected, MAX_PATH, L"%ls\\l4superv.json", layout.config);
    assert(!wcscmp(config.config_file, expected));
    assert(l4_runtime_path(layout.release, L4_DATA_STATE, L"pending_pin.json", L"pending_pin.json", path));
    swprintf_s(expected, MAX_PATH, L"%ls\\pending_pin.json", layout.state);
    assert(!wcscmp(path, expected));
    assert(l4_runtime_path(layout.release, L4_DATA_LOGS, L"mosquitto\\mosquitto.log", L"mosquitto\\log\\mosquitto.log", path));
    swprintf_s(expected, MAX_PATH, L"%ls\\mosquitto\\mosquitto.log", layout.logs);
    assert(!wcscmp(path, expected));
    assert(!svc_ensure_all_installed_and_running(layout.release));
    assert(GetLastError() == ERROR_NOT_SUPPORTED); /* Refusal before any SCM mutation. */
    assert(!l4_runtime_path(layout.release, L4_DATA_STATE, L"..\\control", L"ignored", path));
    swprintf_s(path, MAX_PATH, L"%ls\\releases\\latest", layout.binaries);
    assert(!l4_runtime_path(path, L4_DATA_STATE, L"state.json", L"state.json", expected));

    wchar_t temp[MAX_PATH], root[MAX_PATH], json[MAX_PATH];
    assert(GetTempPathW(MAX_PATH, temp));
    swprintf_s(root, MAX_PATH, L"%lsl4-config-paths-%lu-%llu", temp, GetCurrentProcessId(), GetTickCount64());
    assert(CreateDirectoryW(root, NULL));
    swprintf_s(json, MAX_PATH, L"%ls\\config.json", root);
    FILE* file = NULL;
    assert(!_wfopen_s(&file, json, L"wb") && file);
    fputs("{\"base_path\":\"C:\\\\caller-controlled\"}", file);
    assert(!fclose(file));
    assert(!config_load_json(&config, json));
    assert(!wcscmp(config.base_path, layout.release));
    assert(GetLastError() == ERROR_INVALID_DATA);
    assert(l4_runtime_path(root, L4_DATA_STATE, L"state.json", L"state.json", path));
    swprintf_s(expected, MAX_PATH, L"%ls\\state.json", root);
    assert(!wcscmp(path, expected)); /* Portable fixture remains isolated, not ProgramData. */
    assert(SetEnvironmentVariableW(L"L4_TOOLS_BASE_PATH", root));
    config_init_defaults(&config, L"C:\\portable\\l4superv\\l4superv.exe");
    assert(!wcscmp(config.base_path, root));
    assert(!_wfopen_s(&file, config.config_file, L"wb") && file);
    fputs("{\"base_path\":\"relative\"}", file); assert(!fclose(file));
    assert(!config_load_runtime(&config, L"C:\\portable\\l4superv\\l4superv.exe"));
    swprintf_s(path, MAX_PATH, L"%ls\\l4superv.json", root); assert(DeleteFileW(path));
    assert(DeleteFileW(json)); assert(RemoveDirectoryW(root));
    puts("Windows runtime paths: pinned release, ProgramData config/state/logs, environment/config redirect refusal, SCM mutation refusal PASS");
    return 0;
}
