#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static CRITICAL_SECTION g_log_cs;
static bool g_cs_initialized = false;
static char g_log_path[MAX_PATH] = { 0 };
static bool g_verbose = false;
static bool g_console = true;

static void ensure_directory_exists(const char* filepath) {
    char dir[MAX_PATH];
    strcpy_s(dir, sizeof(dir), filepath);
    char* last_slash = strrchr(dir, '\\');
    if (!last_slash) last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        for (char* p = dir; *p; p++) {
            if (*p == '\\' || *p == '/') {
                char old = *p;
                *p = '\0';
                CreateDirectoryA(dir, NULL);
                *p = old;
            }
        }
        CreateDirectoryA(dir, NULL);
    }
}

void log_init(const char* log_file_path, bool verbose, bool console_output) {
    if (!g_cs_initialized) {
        InitializeCriticalSection(&g_log_cs);
        g_cs_initialized = true;
    }
    EnterCriticalSection(&g_log_cs);
    if (log_file_path && strlen(log_file_path) > 0) {
        strcpy_s(g_log_path, sizeof(g_log_path), log_file_path);
        ensure_directory_exists(g_log_path);
    }
    g_verbose = verbose;
    g_console = console_output;
    LeaveCriticalSection(&g_log_cs);
}

static void log_write(const char* level, const char* fmt, va_list args) {
    char msg[2048];
    vsnprintf(msg, sizeof(msg), fmt, args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[2560];
    snprintf(line, sizeof(line), "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%s] %s\n",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
             level, msg);

    if (!g_cs_initialized) {
        InitializeCriticalSection(&g_log_cs);
        g_cs_initialized = true;
    }

    EnterCriticalSection(&g_log_cs);

    if (g_console) {
        fputs(line, level[0] == 'E' ? stderr : stdout);
        fflush(level[0] == 'E' ? stderr : stdout);
    }

    if (g_log_path[0] != '\0') {
        // Rotate if > 5 MB
        HANDLE hFile = CreateFileA(g_log_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size;
            if (GetFileSizeEx(hFile, &size) && size.QuadPart > 5 * 1024 * 1024) {
                CloseHandle(hFile);
                char old_path[MAX_PATH];
                snprintf(old_path, sizeof(old_path), "%s.old", g_log_path);
                DeleteFileA(old_path);
                MoveFileA(g_log_path, old_path);
            } else {
                CloseHandle(hFile);
            }
        }

        FILE* f = NULL;
        if (fopen_s(&f, g_log_path, "a") == 0 && f) {
            fputs(line, f);
            fclose(f);
        }
    }

    LeaveCriticalSection(&g_log_cs);
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

void log_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write("ERROR", fmt, args);
    va_end(args);
}

void log_debug(const char* fmt, ...) {
    if (!g_verbose) return;
    va_list args;
    va_start(args, fmt);
    log_write("DEBUG", fmt, args);
    va_end(args);
}

void log_close(void) {
    if (g_cs_initialized) {
        DeleteCriticalSection(&g_log_cs);
        g_cs_initialized = false;
    }
}
