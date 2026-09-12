#pragma once
#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    wchar_t pin[64];
    bool pin_specified;
    bool force_reissue;
    bool no_pin;
    bool silent;
    wchar_t dest[MAX_PATH];
    bool dest_specified;
    bool repair;
    bool smoke_only;
    bool show_version;
    bool show_help;
    wchar_t payload_dir[MAX_PATH];
    bool payload_dir_specified;
} CliOptions;

void cli_init_defaults(CliOptions* opts);
bool cli_parse(int argc, wchar_t* argv[], CliOptions* opts, char* err_buf, size_t err_buf_size);
void cli_clean_pin(CliOptions* opts);
void cli_print_usage(const wchar_t* prog_name);
void cli_print_version(void);

#ifdef __cplusplus
}
#endif
