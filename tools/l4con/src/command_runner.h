#ifndef L4CON_COMMAND_RUNNER_H
#define L4CON_COMMAND_RUNNER_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    SHELL_CMD,
    SHELL_POWERSHELL
} ShellType;

typedef struct {
    char session_id[64];
    int  task_id;
    char task_id_str[128];
    char command_line[1024];
    ShellType shell;
    char working_dir[256];
    int  ttl_sec;
    int  max_output_bytes;
    char out_topic[128];
    bool enable_blacklist;

    // Runtime state
    volatile bool cancel_requested;
    volatile bool is_running;
    HANDLE hProcess;
    DWORD  dwProcessId;
} CommandContext;

typedef void (*OutputChunkCallback)(const char* topic, const char* json_envelope, size_t json_len, void* user_data);

void command_runner_setup_environment(void);
void command_runner_get_active_working_dir(char* out_dir, size_t out_max);
void command_runner_get_active_working_dir_w(wchar_t* out_dir, size_t out_max);
bool command_runner_is_blacklisted(const char* cmd);
void command_runner_init_context(CommandContext* ctx);
void command_runner_request_cancel(CommandContext* ctx);

int command_runner_execute(CommandContext* ctx,
                           OutputChunkCallback callback,
                           void* user_data,
                           int* out_exit_code,
                           uint64_t* out_duration_ms);

void command_runner_kill_process_tree(DWORD pid);

#endif /* L4CON_COMMAND_RUNNER_H */
