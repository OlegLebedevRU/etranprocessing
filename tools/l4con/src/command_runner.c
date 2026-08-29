#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "command_runner.h"
#include "mqtt_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

static wchar_t g_active_working_dir[MAX_PATH] = L"C:\\l4tools";

void command_runner_setup_environment(void) {
    wchar_t exe_path[MAX_PATH];
    if (GetModuleFileNameW(NULL, exe_path, MAX_PATH) == 0) return;

    // exe_dir: e.g. C:\l4tools\l4con or C:\l4tools\bin
    wchar_t exe_dir[MAX_PATH];
    wcscpy_s(exe_dir, MAX_PATH, exe_path);
    PathRemoveFileSpecW(exe_dir);

    // base_dir: e.g. C:\l4tools
    wchar_t base_dir[MAX_PATH];
    wcscpy_s(base_dir, MAX_PATH, exe_dir);
    wchar_t* last_slash = wcsrchr(base_dir, L'\\');
    if (last_slash && (_wcsicmp(last_slash + 1, L"l4con") == 0 || _wcsicmp(last_slash + 1, L"bin") == 0)) {
        *last_slash = L'\0';
    }

    if (PathFileExistsW(base_dir)) {
        wcscpy_s(g_active_working_dir, MAX_PATH, base_dir);
    } else if (PathFileExistsW(L"C:\\l4tools")) {
        wcscpy_s(g_active_working_dir, MAX_PATH, L"C:\\l4tools");
    } else {
        wcscpy_s(g_active_working_dir, MAX_PATH, exe_dir);
    }

    // Set process working directory to base directory (e.g. C:\l4tools)
    if (PathFileExistsW(g_active_working_dir)) {
        SetCurrentDirectoryW(g_active_working_dir);
    }

    // Read existing PATH
    DWORD cur_len = GetEnvironmentVariableW(L"PATH", NULL, 0);
    wchar_t* cur_path = NULL;
    if (cur_len > 0) {
        cur_path = (wchar_t*)malloc((cur_len + 1) * sizeof(wchar_t));
        if (cur_path) {
            GetEnvironmentVariableW(L"PATH", cur_path, cur_len + 1);
        }
    }

    // Build extended PATH with all tool directories
    wchar_t new_path[8192];
    _snwprintf(new_path, sizeof(new_path)/sizeof(wchar_t),
               L"%ls;%ls\\l4con;%ls\\l4sql;%ls\\l4pin;%ls\\l4superv;%ls\\leo4proxy;%ls;%ls",
               g_active_working_dir, g_active_working_dir, g_active_working_dir,
               g_active_working_dir, g_active_working_dir, g_active_working_dir,
               exe_dir,
               cur_path ? cur_path : L"");

    SetEnvironmentVariableW(L"PATH", new_path);

    if (cur_path) free(cur_path);
}

void command_runner_get_active_working_dir(char* out_dir, size_t out_max) {
    if (!out_dir || out_max == 0) return;
    out_dir[0] = '\0';
    WideCharToMultiByte(CP_UTF8, 0, g_active_working_dir, -1, out_dir, (int)out_max, NULL, NULL);
}

void command_runner_get_active_working_dir_w(wchar_t* out_dir, size_t out_max) {
    if (!out_dir || out_max == 0) return;
    wcsncpy(out_dir, g_active_working_dir, out_max - 1);
    out_dir[out_max - 1] = L'\0';
}

static const char* BLACKLIST_PATTERNS[] = {
    "format ",
    "format.com",
    "format.exe",
    "format:",
    "del ",
    "del.exe",
    "erase ",
    "rmdir ",
    "rd ",
    "remove-item",
    "clear-content",
    "diskpart",
    "bcdedit",
    "vssadmin delete",
    "shutdown /s",
    "shutdown /r",
    "shutdown -s",
    "shutdown -r",
    "reg delete hklm\\sam",
    "reg delete hklm\\system",
    NULL
};

static void str_to_lower(const char* src, char* dst, size_t max_len) {
    size_t i = 0;
    while (src[i] && i + 1 < max_len) {
        dst[i] = (char)tolower((unsigned char)src[i]);
        i++;
    }
    dst[i] = '\0';
}

static bool is_word_match(const char* str, const char* pattern) {
    const char* p = strstr(str, pattern);
    while (p != NULL) {
        if (p == str || (!isalnum((unsigned char)*(p - 1)) && *(p - 1) != '-' && *(p - 1) != '/')) {
            return true;
        }
        p = strstr(p + 1, pattern);
    }
    return false;
}

bool command_runner_is_blacklisted(const char* cmd) {
    if (!cmd) return false;
    char lower_cmd[1024];
    str_to_lower(cmd, lower_cmd, sizeof(lower_cmd));

    for (int i = 0; BLACKLIST_PATTERNS[i] != NULL; i++) {
        if (is_word_match(lower_cmd, BLACKLIST_PATTERNS[i])) {
            return true;
        }
    }
    return false;
}

void command_runner_init_context(CommandContext* ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(CommandContext));
    ctx->ttl_sec = 30;
    ctx->max_output_bytes = 1048576; // 1 MB
    ctx->enable_blacklist = true;
    ctx->shell = SHELL_CMD;
    ctx->hProcess = NULL;
    ctx->dwProcessId = 0;
}

void command_runner_request_cancel(CommandContext* ctx) {
    if (!ctx) return;
    ctx->cancel_requested = true;
    if (ctx->dwProcessId != 0) {
        command_runner_kill_process_tree(ctx->dwProcessId);
    }
}

void command_runner_kill_process_tree(DWORD pid) {
    if (pid == 0) return;
    wchar_t kill_cmd[128];
    _snwprintf(kill_cmd, sizeof(kill_cmd)/sizeof(wchar_t), L"taskkill.exe /F /T /PID %lu", pid);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcessW(NULL, kill_cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

static void convert_output_to_utf8(const char* in_buf, int in_len, ShellType shell, char* utf8_buf, int utf8_max) {
    if (!in_buf || in_len <= 0 || !utf8_buf || utf8_max <= 0) {
        if (utf8_buf && utf8_max > 0) utf8_buf[0] = '\0';
        return;
    }

    UINT src_cp = (shell == SHELL_POWERSHELL) ? CP_UTF8 : CP_OEMCP;
    int wide_len = MultiByteToWideChar(src_cp, 0, in_buf, in_len, NULL, 0);
    if (wide_len <= 0 && src_cp == CP_UTF8) {
        src_cp = CP_OEMCP;
        wide_len = MultiByteToWideChar(CP_OEMCP, 0, in_buf, in_len, NULL, 0);
    }

    if (wide_len <= 0) {
        utf8_buf[0] = '\0';
        return;
    }

    wchar_t* wide_buf = (wchar_t*)malloc((wide_len + 1) * sizeof(wchar_t));
    if (!wide_buf) {
        utf8_buf[0] = '\0';
        return;
    }

    MultiByteToWideChar(src_cp, 0, in_buf, in_len, wide_buf, wide_len);
    wide_buf[wide_len] = L'\0';

    // 2. Convert UTF-16 WCHAR to UTF-8
    int out_len = WideCharToMultiByte(CP_UTF8, 0, wide_buf, wide_len, utf8_buf, utf8_max - 1, NULL, NULL);
    if (out_len >= 0 && out_len < utf8_max) {
        utf8_buf[out_len] = '\0';
    } else {
        utf8_buf[utf8_max - 1] = '\0';
    }

    free(wide_buf);
}

static uint64_t get_tick_ms(void) {
    return (uint64_t)GetTickCount64();
}

int command_runner_execute(CommandContext* ctx,
                           OutputChunkCallback callback,
                           void* user_data,
                           int* out_exit_code,
                           uint64_t* out_duration_ms) {
    if (!ctx) return -1;

    uint64_t start_ms = get_tick_ms();
    uint32_t seq = 0;
    int exit_code = 0;

    // Check blacklist first
    if (ctx->enable_blacklist && command_runner_is_blacklisted(ctx->command_line)) {
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg),
                 "Execution blocked by security policy: command '%s' contains blacklisted pattern.\r\n",
                 ctx->command_line);

        char escaped_err[1024];
        json_escape_string(err_msg, strlen(err_msg), escaped_err, sizeof(escaped_err));

        char json_buf[2048];
        snprintf(json_buf, sizeof(json_buf),
                 "{\"v\":1,\"session_id\":\"%s\",\"seq\":1,\"kind\":\"error\",\"stream\":\"stderr\","
                 "\"encoding\":\"utf-8\",\"data\":\"%s\",\"eof\":true,\"exit_code\":126,\"truncated\":false}",
                 ctx->session_id, escaped_err);

        if (callback) {
            callback(ctx->out_topic, json_buf, strlen(json_buf), user_data);
        }

        if (out_exit_code) *out_exit_code = 126;
        if (out_duration_ms) *out_duration_ms = get_tick_ms() - start_ms;
        return 126;
    }

    // Create Anonymous Pipe for stdout/stderr
    HANDLE hReadPipe = NULL;
    HANDLE hWritePipe = NULL;
    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        if (out_exit_code) *out_exit_code = -1;
        return -1;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    // Build CommandLine
    wchar_t sys_dir[MAX_PATH];
    if (GetSystemDirectoryW(sys_dir, MAX_PATH) == 0) {
        wcscpy_s(sys_dir, MAX_PATH, L"C:\\Windows\\System32");
    }

    wchar_t w_cmdline[4096];
    wchar_t w_usercmd[2048];
    MultiByteToWideChar(CP_UTF8, 0, ctx->command_line, -1, w_usercmd, 2048);

    if (ctx->shell == SHELL_POWERSHELL) {
        _snwprintf(w_cmdline, sizeof(w_cmdline)/sizeof(wchar_t),
                   L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command %ls",
                   w_usercmd);
    } else {
        _snwprintf(w_cmdline, sizeof(w_cmdline)/sizeof(wchar_t),
                   L"cmd.exe /c %ls",
                   w_usercmd);
    }

    wchar_t* w_workdir = NULL;
    wchar_t w_workdir_buf[MAX_PATH];
    if (strlen(ctx->working_dir) > 0) {
        MultiByteToWideChar(CP_UTF8, 0, ctx->working_dir, -1, w_workdir_buf, MAX_PATH);
        w_workdir = w_workdir_buf;
    } else {
        w_workdir = g_active_working_dir;
    }

    // Create stdin pipe for non-interactive EOF
    HANDLE hStdInRead = NULL;
    HANDLE hStdInWrite = NULL;
    if (CreatePipe(&hStdInRead, &hStdInWrite, &sa, 0)) {
        SetHandleInformation(hStdInWrite, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.hStdInput = hStdInRead;

    ZeroMemory(&pi, sizeof(pi));

    BOOL proc_created = CreateProcessW(NULL, w_cmdline, NULL, NULL, TRUE,
                                       CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                                       NULL, w_workdir, &si, &pi);

    // Close stdin handles in parent process
    if (hStdInWrite != NULL) {
        CloseHandle(hStdInWrite);
        hStdInWrite = NULL;
    }
    if (hStdInRead != NULL) {
        CloseHandle(hStdInRead);
        hStdInRead = NULL;
    }

    // Parent must close write pipe handle now
    if (hWritePipe != NULL) {
        CloseHandle(hWritePipe);
        hWritePipe = NULL;
    }

    if (!proc_created) {
        DWORD dwErr = GetLastError();
        CloseHandle(hReadPipe);
        char err_json[512];
        snprintf(err_json, sizeof(err_json),
                 "{\"v\":1,\"session_id\":\"%s\",\"seq\":1,\"kind\":\"error\",\"stream\":\"stderr\","
                 "\"encoding\":\"utf-8\",\"data\":\"Failed to create process: error code %lu\\r\\n\",\"eof\":true,\"exit_code\":-1,\"truncated\":false}",
                 ctx->session_id, dwErr);
        if (callback) callback(ctx->out_topic, err_json, strlen(err_json), user_data);
        if (out_exit_code) *out_exit_code = -1;
        return -1;
    }

    ctx->hProcess = pi.hProcess;
    ctx->dwProcessId = pi.dwProcessId;
    ctx->is_running = true;

    char raw_buf[3072];
    char utf8_buf[6144];
    char json_buf[16384];
    size_t total_output_bytes = 0;
    bool is_truncated = false;

    // Send initial working directory prompt
    char cwd_utf8[MAX_PATH];
    command_runner_get_active_working_dir(cwd_utf8, sizeof(cwd_utf8));

    char prompt_data[512];
    snprintf(prompt_data, sizeof(prompt_data), "%s> %s\r\n", cwd_utf8, ctx->command_line);
    char escaped_prompt[1024];
    json_escape_string(prompt_data, strlen(prompt_data), escaped_prompt, sizeof(escaped_prompt));

    seq++;
    snprintf(json_buf, sizeof(json_buf),
             "{\"v\":1,\"session_id\":\"%s\",\"seq\":%u,\"kind\":\"stdout\",\"stream\":\"stdout\","
             "\"encoding\":\"utf-8\",\"data\":\"%s\",\"eof\":false,\"exit_code\":null,\"truncated\":false}",
             ctx->session_id, seq, escaped_prompt);
    if (callback) callback(ctx->out_topic, json_buf, strlen(json_buf), user_data);

    DWORD max_wait_ms = (DWORD)(ctx->ttl_sec * 1000);
    uint64_t proc_start_tick = get_tick_ms();

    while (true) {
        // 1. Check for cancellation
        if (ctx->cancel_requested) {
            command_runner_kill_process_tree(pi.dwProcessId);
            exit_code = 130;

            char cancel_json[512];
            seq++;
            snprintf(cancel_json, sizeof(cancel_json),
                     "{\"v\":1,\"session_id\":\"%s\",\"seq\":%u,\"kind\":\"error\",\"stream\":\"stderr\","
                     "\"encoding\":\"utf-8\",\"data\":\"\\r\\n[Process terminated: Cancelled by user]\\r\\n\","
                     "\"eof\":true,\"exit_code\":130,\"truncated\":false}",
                     ctx->session_id, seq);
            if (callback) callback(ctx->out_topic, cancel_json, strlen(cancel_json), user_data);
            break;
        }

        // 2. Check for TTL expiration
        uint64_t elapsed_ms = get_tick_ms() - proc_start_tick;
        if (elapsed_ms >= max_wait_ms) {
            command_runner_kill_process_tree(pi.dwProcessId);
            exit_code = 124;

            char timeout_json[512];
            seq++;
            snprintf(timeout_json, sizeof(timeout_json),
                     "{\"v\":1,\"session_id\":\"%s\",\"seq\":%u,\"kind\":\"error\",\"stream\":\"stderr\","
                     "\"encoding\":\"utf-8\",\"data\":\"\\r\\n[Process terminated: Timeout exceeded]\\r\\n\","
                     "\"eof\":true,\"exit_code\":124,\"truncated\":false}",
                     ctx->session_id, seq);
            if (callback) callback(ctx->out_topic, timeout_json, strlen(timeout_json), user_data);
            break;
        }

        // 3. Peek & Read available output from pipe
        DWORD bytes_avail = 0;
        if (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &bytes_avail, NULL) && bytes_avail > 0) {
            DWORD to_read = (DWORD)(sizeof(raw_buf) - 1);
            if (to_read > bytes_avail) to_read = bytes_avail;

            DWORD bytes_read = 0;
            if (ReadFile(hReadPipe, raw_buf, to_read, &bytes_read, NULL) && bytes_read > 0) {
                total_output_bytes += bytes_read;
                convert_output_to_utf8(raw_buf, (int)bytes_read, ctx->shell, utf8_buf, (int)sizeof(utf8_buf));

                char escaped_data[12288];
                json_escape_string(utf8_buf, strlen(utf8_buf), escaped_data, sizeof(escaped_data));

                seq++;
                snprintf(json_buf, sizeof(json_buf),
                         "{\"v\":1,\"session_id\":\"%s\",\"seq\":%u,\"kind\":\"stdout\",\"stream\":\"stdout\","
                         "\"encoding\":\"utf-8\",\"data\":\"%s\",\"eof\":false,\"exit_code\":null,\"truncated\":false}",
                         ctx->session_id, seq, escaped_data);

                if (callback) callback(ctx->out_topic, json_buf, strlen(json_buf), user_data);

                // Check max output byte limit
                if (ctx->max_output_bytes > 0 && total_output_bytes >= (size_t)ctx->max_output_bytes) {
                    is_truncated = true;
                    command_runner_kill_process_tree(pi.dwProcessId);
                    exit_code = 1;

                    seq++;
                    snprintf(json_buf, sizeof(json_buf),
                             "{\"v\":1,\"session_id\":\"%s\",\"seq\":%u,\"kind\":\"error\",\"stream\":\"stderr\","
                             "\"encoding\":\"utf-8\",\"data\":\"\\r\\n[Output truncated: Limit reached]\\r\\n\","
                             "\"eof\":true,\"exit_code\":1,\"truncated\":true}",
                             ctx->session_id, seq);
                    if (callback) callback(ctx->out_topic, json_buf, strlen(json_buf), user_data);
                    break;
                }
                continue; // Read next batch immediately
            }
        }

        // 4. Check if process has terminated
        DWORD wait_res = WaitForSingleObject(pi.hProcess, 50);
        if (wait_res == WAIT_OBJECT_0) {
            // Drain remaining pipe bytes
            while (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &bytes_avail, NULL) && bytes_avail > 0) {
                DWORD to_read = (DWORD)(sizeof(raw_buf) - 1);
                if (to_read > bytes_avail) to_read = bytes_avail;
                DWORD bytes_read = 0;
                if (ReadFile(hReadPipe, raw_buf, to_read, &bytes_read, NULL) && bytes_read > 0) {
                    convert_output_to_utf8(raw_buf, (int)bytes_read, ctx->shell, utf8_buf, (int)sizeof(utf8_buf));
                    char escaped_data[12288];
                    json_escape_string(utf8_buf, strlen(utf8_buf), escaped_data, sizeof(escaped_data));
                    seq++;
                    snprintf(json_buf, sizeof(json_buf),
                             "{\"v\":1,\"session_id\":\"%s\",\"seq\":%u,\"kind\":\"stdout\",\"stream\":\"stdout\","
                             "\"encoding\":\"utf-8\",\"data\":\"%s\",\"eof\":false,\"exit_code\":null,\"truncated\":false}",
                             ctx->session_id, seq, escaped_data);
                    if (callback) callback(ctx->out_topic, json_buf, strlen(json_buf), user_data);
                } else {
                    break;
                }
            }

            DWORD dwCode = 0;
            if (GetExitCodeProcess(pi.hProcess, &dwCode)) {
                exit_code = (int)dwCode;
            }

            // Send EOF chunk
            seq++;
            snprintf(json_buf, sizeof(json_buf),
                     "{\"v\":1,\"session_id\":\"%s\",\"seq\":%u,\"kind\":\"status\",\"stream\":\"stdout\","
                     "\"encoding\":\"utf-8\",\"data\":\"\",\"eof\":true,\"exit_code\":%d,\"truncated\":%s}",
                     ctx->session_id, seq, exit_code, is_truncated ? "true" : "false");
            if (callback) callback(ctx->out_topic, json_buf, strlen(json_buf), user_data);
            break;
        }
    }

    ctx->is_running = false;
    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    ctx->hProcess = NULL;
    ctx->dwProcessId = 0;

    if (out_exit_code) *out_exit_code = exit_code;
    if (out_duration_ms) *out_duration_ms = get_tick_ms() - start_ms;
    return exit_code;
}
