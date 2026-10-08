#include "layout_plan.h"
#include "version.h"
#include "../../l4common/layout.h"
#include "../../l4common/access.h"
#include <stdio.h>
#include <stdlib.h>

static void json_string(const wchar_t* value) {
    char utf8[8192];
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, utf8, sizeof(utf8), NULL, NULL)) {
        fputs("null", stdout);
        return;
    }
    putchar('"');
    for (const unsigned char* p = (unsigned char*)utf8; *p; ++p) {
        if (*p == '"' || *p == '\\') { putchar('\\'); putchar(*p); }
        else if (*p < 32) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}

int setup_print_layout_plan(void) {
    L4Layout layout;
    L4ServiceInventory inventory[4];
    bool token_ready[4]; DWORD access_error[4];
    const wchar_t* names[] = {L"Leo4Proxy", L"mosquitto", L"L4Con", L"L4Superv"};
    if (!l4_layout_resolve(&layout, L4SETUP_VERSION_WSTRING)) {
        fprintf(stderr, "Cannot resolve Windows layout (error %lu). No changes were made.\n", GetLastError());
        return 1;
    }
    for (unsigned i = 0; i < _countof(names); ++i) {
        if (!l4_service_inventory(names[i], &inventory[i])) {
            fprintf(stderr, "Cannot query service inventory (error %lu). No changes were made.\n", GetLastError());
            return 1;
        }
    }
    for (unsigned i = 0; i < _countof(names); ++i) {
        HANDLE token = NULL;
        token_ready[i] = l4_access_service_token(names[i], &token);
        access_error[i] = token_ready[i] ? ERROR_SUCCESS : GetLastError();
        if (token) CloseHandle(token);
    }
    fputs("{\n  \"schema\": 1,\n  \"mode\": \"plan\",\n  \"installation_enabled\": false,\n  \"fresh_installation_entry\": true,\n  \"version\": ", stdout);
    json_string(L4SETUP_VERSION_WSTRING);
    fputs(",\n  \"paths\": {", stdout);
    const char* keys[] = {"binaries", "data", "release", "launchers", "config", "state", "logs", "operations", "cache", "staging"};
    const wchar_t* paths[] = {layout.binaries, layout.data, layout.release, layout.launchers,
        layout.config, layout.state, layout.logs, layout.operations, layout.cache, layout.staging};
    for (unsigned i = 0; i < _countof(keys); ++i) {
        printf("%s\n    \"%s\": ", i ? "," : "", keys[i]); json_string(paths[i]);
    }
    fputs("\n  },\n  \"services\": [", stdout);
    for (unsigned i = 0; i < _countof(names); ++i) {
        printf("%s\n    {\"name\": ", i ? "," : ""); json_string(names[i]);
        printf(", \"installed\": %s", inventory[i].installed ? "true" : "false");
        printf(", \"access_token_ready\": %s, \"access_error\": %lu", token_ready[i] ? "true" : "false", access_error[i]);
        if (inventory[i].installed) {
            fputs(", \"account\": ", stdout); json_string(inventory[i].account);
            fputs(", \"image_path\": ", stdout); json_string(inventory[i].image_path);
            printf(", \"start_type\": %lu", inventory[i].start_type);
        }
        putchar('}');
    }
    fputs("\n  ]\n}\n", stdout);
    return fflush(stdout) == 0 && !ferror(stdout) ? 0 : 1;
}
