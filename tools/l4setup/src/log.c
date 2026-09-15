#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

static CRITICAL_SECTION g_log_cs;
static bool g_cs_inited = false;
static FILE* g_log_fp = NULL;
static wchar_t g_log_path[MAX_PATH] = { 0 };

static LogCallback g_log_cb = NULL;
static void* g_log_cb_user = NULL;

#define LOG_REPLAY_BUFFER_SIZE 64
#define LOG_REPLAY_LINE_MAX 512
static char g_replay_lines[LOG_REPLAY_BUFFER_SIZE][LOG_REPLAY_LINE_MAX];
static size_t g_replay_count = 0;

void log_set_callback(LogCallback cb, void* user_data) {
    if (!g_cs_inited) {
        InitializeCriticalSection(&g_log_cs);
        g_cs_inited = true;
    }

    EnterCriticalSection(&g_log_cs);
    g_log_cb = cb;
    g_log_cb_user = user_data;

    if (cb && g_replay_count > 0) {
        for (size_t i = 0; i < g_replay_count; i++) {
            cb(g_replay_lines[i], user_data);
        }
    }
    LeaveCriticalSection(&g_log_cs);
}

void log_mask_pin(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) return;
    output[0] = '\0';

    size_t in_len = strlen(input);
    size_t in_idx = 0;
    size_t out_idx = 0;

    while (in_idx < in_len && out_idx + 1 < output_size) {
        // Look for "pin" keyword (case-insensitive)
        if (_strnicmp(&input[in_idx], "pin", 3) == 0) {
            // Check boundary: previous char should not be alphanumeric (unless part of --pin or -pin)
            bool valid_prefix = (in_idx == 0 || !isalnum((unsigned char)input[in_idx - 1]) || input[in_idx - 1] == '-');
            if (valid_prefix) {
                // Copy "pin"
                size_t kw_len = 3;
                for (size_t k = 0; k < kw_len && out_idx + 1 < output_size; k++) {
                    output[out_idx++] = input[in_idx++];
                }

                // Skip separators: '=', ':', ' ', '\t', '"', etc.
                while (in_idx < in_len && (input[in_idx] == '=' || input[in_idx] == ':' || 
                                          input[in_idx] == ' ' || input[in_idx] == '\t' || 
                                          input[in_idx] == '"')) {
                    if (out_idx + 1 < output_size) {
                        output[out_idx++] = input[in_idx++];
                    } else {
                        break;
                    }
                }

                // If characters follow that look like a PIN, mask them
                size_t pin_chars = 0;
                while (in_idx < in_len && input[in_idx] != ' ' && input[in_idx] != '\t' && 
                       input[in_idx] != '&' && input[in_idx] != '"' && input[in_idx] != '\r' && 
                       input[in_idx] != '\n' && input[in_idx] != ',') {
                    in_idx++;
                    pin_chars++;
                }

                if (pin_chars > 0) {
                    const char* mask = "******";
                    for (size_t m = 0; mask[m] && out_idx + 1 < output_size; m++) {
                        output[out_idx++] = mask[m];
                    }
                }
                continue;
            }
        }

        output[out_idx++] = input[in_idx++];
    }

    output[out_idx] = '\0';
}

void log_init(const wchar_t* dest_dir) {
    if (!g_cs_inited) {
        InitializeCriticalSection(&g_log_cs);
        g_cs_inited = true;
    }

    if (dest_dir && dest_dir[0] != L'\0') {
        swprintf_s(g_log_path, MAX_PATH, L"%ls\\l4setup.log", dest_dir);
        _wfopen_s(&g_log_fp, g_log_path, L"a, ccs=UTF-8");
    }
}

void log_get_path(wchar_t* out_path, size_t out_size) {
    if (!out_path || out_size == 0) return;
    wcsncpy_s(out_path, out_size, g_log_path, _TRUNCATE);
}

void log_close(void) {
    if (g_cs_inited) {
        EnterCriticalSection(&g_log_cs);
        if (g_log_fp) {
            fflush(g_log_fp);
            fclose(g_log_fp);
            g_log_fp = NULL;
        }
        LeaveCriticalSection(&g_log_cs);
        DeleteCriticalSection(&g_log_cs);
        g_cs_inited = false;
    }
}

static void log_write(const char* level, const char* fmt, va_list args) {
    char raw_buf[2048] = { 0 };
    vsnprintf(raw_buf, sizeof(raw_buf), fmt, args);

    char masked_buf[2048] = { 0 };
    log_mask_pin(raw_buf, masked_buf, sizeof(masked_buf));

    // Get current UTC time
    SYSTEMTIME st;
    GetSystemTime(&st);

    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%04u-%02u-%02u %02u:%02u:%02u UTC",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    char formatted_line[2200];
    snprintf(formatted_line, sizeof(formatted_line), "[%s] [%s] %s", time_str, level, masked_buf);

    if (g_cs_inited) {
        EnterCriticalSection(&g_log_cs);
    }

    // Store in replay buffer
    if (g_replay_count < LOG_REPLAY_BUFFER_SIZE) {
        strncpy_s(g_replay_lines[g_replay_count++], LOG_REPLAY_LINE_MAX, formatted_line, _TRUNCATE);
    }

    // Print to console (stdout / stderr)
    FILE* stream = (strcmp(level, "ERROR") == 0) ? stderr : stdout;
    fprintf(stream, "%s\n", formatted_line);
    fflush(stream);

    // Print to log file if open
    if (g_log_fp) {
        // UTF-8 file
        wchar_t w_buf[2048] = { 0 };
        MultiByteToWideChar(CP_UTF8, 0, masked_buf, -1, w_buf, 2048);
        fwprintf(g_log_fp, L"[%S] [%S] %s\n", time_str, level, w_buf);
        fflush(g_log_fp);
    }

    // Notify registered UI callback
    if (g_log_cb) {
        g_log_cb(formatted_line, g_log_cb_user);
    }

    if (g_cs_inited) {
        LeaveCriticalSection(&g_log_cs);
    }
}

void log_info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write("INFO", fmt, args);
    va_end(args);
}

void log_warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write("WARN", fmt, args);
    va_end(args);
}

void log_err(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write("ERROR", fmt, args);
    va_end(args);
}
