/*
 * L4C-10 rotating inventory log: l4capture.log 5 MiB × 2, startup inventory,
 * secret scrub. Local-only diagnostics (wire freeze preserved).
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "l4capture/logger.h"

static FILE *g_log_fp = NULL;
static wchar_t g_log_path[MAX_PATH];
static wchar_t g_log_old_path[MAX_PATH];
static uint64_t g_active_bytes = 0;
static CRITICAL_SECTION g_cs;
static bool g_cs_ready = false;

static void ensure_cs(void)
{
    if (!g_cs_ready) {
        InitializeCriticalSection(&g_cs);
        g_cs_ready = true;
    }
}

static bool path_join_log(wchar_t *out, size_t cap, const wchar_t *dir, const wchar_t *name)
{
    size_t dlen;
    if (!out || cap == 0 || !dir || !name) return false;
    dlen = wcslen(dir);
    if (dlen + wcslen(name) + 2 > cap) return false;
    wcscpy_s(out, cap, dir);
    if (dlen > 0 && out[dlen - 1] != L'\\' && out[dlen - 1] != L'/') {
        wcscat_s(out, cap, L"\\");
    }
    wcscat_s(out, cap, name);
    return true;
}

static bool resolve_default_dir(wchar_t *dir, size_t cap)
{
    wchar_t *slash;
    if (!GetModuleFileNameW(NULL, dir, (DWORD)cap)) return false;
    slash = wcsrchr(dir, L'\\');
    if (!slash) return false;
    slash[1] = L'\0';
    return true;
}

uint64_t l4c_logger_active_bytes(void)
{
    return g_active_bytes;
}

void l4c_logger_scrub_secrets(char *line, size_t cap)
{
    static const char *keys[] = {
        "pin=", "password=", "passwd=", "token=", "secret=", "authorization:",
        "cookie:", "api_key=", "apikey=", "private_key=", NULL
    };
    size_t i;
    char lower[1024];
    size_t n;

    if (!line || cap == 0) return;
    n = strlen(line);
    if (n >= sizeof(lower)) n = sizeof(lower) - 1;
    for (i = 0; i < n; ++i) lower[i] = (char)tolower((unsigned char)line[i]);
    lower[n] = '\0';

    for (i = 0; keys[i]; ++i) {
        char *hit = strstr(lower, keys[i]);
        if (hit) {
            size_t key_len = strlen(keys[i]);
            size_t off = (size_t)(hit - lower);
            size_t j = off + key_len;
            while (j < n && line[j] != ' ' && line[j] != '\n' && line[j] != '\r' && line[j] != '\t') {
                line[j] = '*';
                ++j;
            }
        }
    }
}

bool l4c_logger_rotate_if_needed(void)
{
    long pos;
    if (!g_log_fp) return false;
    pos = ftell(g_log_fp);
    if (pos < 0) return false;
    if ((uint64_t)pos < L4C_LOG_MAX_FILE_BYTES && g_active_bytes < L4C_LOG_MAX_FILE_BYTES) {
        return false;
    }
    fclose(g_log_fp);
    g_log_fp = NULL;
    (void)DeleteFileW(g_log_old_path);
    (void)MoveFileW(g_log_path, g_log_old_path);
    g_log_fp = _wfopen(g_log_path, L"a");
    g_active_bytes = 0;
    return true;
}

static void log_open(void)
{
    if (g_log_fp) return;
    g_log_fp = _wfopen(g_log_path, L"a");
    if (!g_log_fp) return;
    if (fseek(g_log_fp, 0, SEEK_END) == 0) {
        long pos = ftell(g_log_fp);
        g_active_bytes = (pos > 0) ? (uint64_t)pos : 0;
    }
}

l4c_status_t l4c_logger_init(const wchar_t *dir_or_null)
{
    wchar_t dir[MAX_PATH];
    ensure_cs();
    EnterCriticalSection(&g_cs);
    if (g_log_fp) {
        LeaveCriticalSection(&g_cs);
        return L4C_OK;
    }
    if (dir_or_null && dir_or_null[0]) {
        wcscpy_s(dir, MAX_PATH, dir_or_null);
    } else if (!resolve_default_dir(dir, MAX_PATH)) {
        LeaveCriticalSection(&g_cs);
        return L4C_ERR_INVALID_ARG;
    }
    if (!path_join_log(g_log_path, MAX_PATH, dir, L"l4capture.log") ||
        !path_join_log(g_log_old_path, MAX_PATH, dir, L"l4capture.log.old")) {
        LeaveCriticalSection(&g_cs);
        return L4C_ERR_INVALID_ARG;
    }
    log_open();
    LeaveCriticalSection(&g_cs);
    return g_log_fp ? L4C_OK : L4C_ERR_PIPE_BROKEN;
}

void l4c_logger_shutdown(void)
{
    if (!g_cs_ready) return;
    EnterCriticalSection(&g_cs);
    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
    LeaveCriticalSection(&g_cs);
}

static void stamp_now(char *out, size_t cap)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf_s(out, cap, _TRUNCATE,
                "[%04u-%02u-%02u %02u:%02u:%02u.%03u]",
                (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
                (unsigned)st.wMilliseconds);
}

void l4c_logger_line(const char *line)
{
    char stamp[40];
    char scrubbed[4096];
    size_t len;

    if (!line) return;
    ensure_cs();
    len = strlen(line);
    if (len >= sizeof(scrubbed)) len = sizeof(scrubbed) - 1;
    memcpy(scrubbed, line, len);
    scrubbed[len] = '\0';
    l4c_logger_scrub_secrets(scrubbed, sizeof(scrubbed));

    stamp_now(stamp, sizeof(stamp));
    EnterCriticalSection(&g_cs);
    log_open();
    if (g_log_fp) {
        fprintf(g_log_fp, "%s %s\n", stamp, scrubbed);
        fflush(g_log_fp);
        g_active_bytes += (uint64_t)strlen(stamp) + 1u + (uint64_t)strlen(scrubbed) + 1u;
        (void)l4c_logger_rotate_if_needed();
    }
    LeaveCriticalSection(&g_cs);
}

void l4c_logger_write(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    if (!fmt) return;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    l4c_logger_line(buf);
}

void l4c_logger_startup_inventory(const l4c_inventory_snapshot_t *inv)
{
    if (!inv) return;
    l4c_logger_write(
        "INV os=%u.%u.%u sp=%s arch=%s product=%s cpu_cores=%u cpu=\"%s\" "
        "ram_total_kb=%llu ram_avail_kb=%llu display=%dx%d rect=%d,%d,%d,%d dpi_aware=%d",
        (unsigned)inv->os_major, (unsigned)inv->os_minor, (unsigned)inv->os_build,
        inv->service_pack, inv->os_arch, inv->os_product,
        (unsigned)inv->cpu_cores, inv->cpu_model,
        (unsigned long long)inv->ram_total_kb, (unsigned long long)inv->ram_avail_kb,
        (int)inv->display_w, (int)inv->display_h,
        (int)inv->source_rect.left, (int)inv->source_rect.top,
        (int)inv->source_rect.right, (int)inv->source_rect.bottom,
        inv->dpi_aware ? 1 : 0);
    l4c_logger_write(
        "INV media profile_req=%u profile_act=%u cap=%u enc=%u fallback=%u "
        "fps=%u br=%u/%u/%u lease_ms=%u",
        (unsigned)inv->profile_requested,
        (unsigned)inv->profile_actual,
        (unsigned)inv->capture_backend, (unsigned)inv->encoder_backend,
        (unsigned)inv->fallback_reason,
        (unsigned)inv->start_fps,
        (unsigned)inv->bitrate_min_kbps, (unsigned)inv->bitrate_target_kbps,
        (unsigned)inv->bitrate_max_kbps,
        (unsigned)inv->lease_or_loop_ms);
    l4c_logger_write(
        "INV limits private_target_hint_kb=%u max_au=%u",
        47185920u / 1024u, 2097152u);
}
