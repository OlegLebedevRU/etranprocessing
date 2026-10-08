#include "../src/config.h"
#include "../../l4common/layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    L4Layout layout; wchar_t exe[MAX_PATH], expected[MAX_PATH], base[MAX_PATH];
    bool installed; char expected_a[MAX_PATH];
    assert(l4_layout_resolve(&layout, L"1.13.2"));
    assert(l4_layout_component(&layout, L"l4desk", L"l4desk.exe", exe));
    assert(l4_runtime_release_from_exe(exe, L"l4desk", base, &installed) && installed);
    assert(!wcscmp(base, layout.release));
    const char* original = getenv("L4_TOOLS_BASE_PATH");
    char* saved = original ? _strdup(original) : NULL;
    assert(!_putenv_s("L4_TOOLS_BASE_PATH", "D:\\spoof"));
    L4DeskConfig cfg; config_init_defaults(&cfg);
    assert(config_set_runtime_paths(&cfg, exe) && cfg.installed_layout);
    assert(l4_runtime_path_ansi(cfg.base_path, L4_DATA_LOGS,
        L"l4desk\\l4desk.log", L"unused", expected_a));
    assert(!strcmp(cfg.log_file, expected_a));
    char* redirect[] = {"l4desk", "--base-path", "D:\\spoof"};
    assert(!config_parse_args(&cfg, 3, redirect));
    char* redirect_log[] = {"l4desk", "--log", "D:\\spoof\\log"};
    assert(!config_parse_args(&cfg, 3, redirect_log));
    char* same[] = {"l4desk", "--base-path", cfg.base_path};
    assert(config_parse_args(&cfg, 3, same));
    assert(l4_runtime_path(base, L4_DATA_STATE, L"l4desk\\ffmpeg_state.json", L"unused", expected));
    wchar_t wanted[MAX_PATH]; swprintf_s(wanted, MAX_PATH, L"%ls\\l4desk\\ffmpeg_state.json", layout.state);
    assert(!wcscmp(expected, wanted));
    assert(l4_runtime_path(base, L4_DATA_CONFIG, L"l4desk\\l4desk_policy.ini", L"unused", expected));
    swprintf_s(wanted, MAX_PATH, L"%ls\\l4desk\\l4desk_policy.ini", layout.config);
    assert(!wcscmp(expected, wanted));
    assert(!_putenv_s("L4_TOOLS_BASE_PATH", ""));
    assert(config_set_runtime_paths(&cfg, L"D:\\dev\\tools\\l4desk\\bin\\x86\\l4desk.exe"));
    assert(!cfg.installed_layout && !strcmp(cfg.base_path, "D:\\dev\\tools"));
    assert(!strcmp(cfg.log_file, "D:\\dev\\tools\\l4desk\\log\\l4desk.log"));
    assert(!config_set_runtime_paths(&cfg, L"D:\\dev\\other\\bin\\l4desk.exe"));
    assert(!_putenv_s("L4_TOOLS_BASE_PATH", saved ? saved : "")); free(saved);
    puts("Desk runtime paths: fixed installed root, stable state/config/logs, portable isolation PASS");
    return 0;
}
