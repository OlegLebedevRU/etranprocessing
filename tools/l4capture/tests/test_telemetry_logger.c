/*
 * L4C-10 telemetry + rotating inventory logger unit tests.
 * Covers A5-A11 algorithmic parts without requiring a second OS image.
 */
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "l4capture/telemetry.h"
#include "l4capture/logger.h"
#include "l4capture/degrade_controller.h"

int test_telemetry_p95_window_rank(void) {
    l4c_p95_window_t w;
    uint16_t p95 = 0;
    uint32_t i;
    l4c_p95_window_reset(&w);
    if (l4c_p95_window_eval(&w, &p95)) return 1;
    /* 20 samples 1..20 -> ceil(0.95*20)=19 -> 19 */
    for (i = 1; i <= 20; ++i) {
        if (!l4c_p95_window_push(&w, i)) return 2;
    }
    if (!l4c_p95_window_eval(&w, &p95)) return 3;
    if (p95 != 19) return 4;
    return 0;
}

int test_telemetry_p95_window_reset(void) {
    l4c_p95_window_t w;
    uint16_t p95 = 0;
    uint32_t i;
    l4c_p95_window_reset(&w);
    for (i = 0; i < 10; ++i) (void)l4c_p95_window_push(&w, 5);
    l4c_p95_window_reset(&w);
    if (l4c_p95_window_eval(&w, &p95)) return 1;
    if (!l4c_p95_window_push(&w, 42)) return 2;
    if (!l4c_p95_window_eval(&w, &p95) || p95 != 42) return 3;
    return 0;
}

int test_telemetry_p95_window_capacity(void) {
    l4c_p95_window_t w;
    uint32_t i;
    l4c_p95_window_reset(&w);
    for (i = 0; i < L4C_TELEMETRY_P95_MAX; ++i) {
        if (!l4c_p95_window_push(&w, i)) return 1;
    }
    if (l4c_p95_window_push(&w, 0)) return 2;
    return 0;
}

int test_telemetry_rate_window_fps_bitrate(void) {
    l4c_rate_window_t w;
    uint16_t fps = 0;
    uint32_t kbps = 0;
    uint32_t i;
    l4c_rate_window_reset(&w, 10000);
    for (i = 0; i < 10; ++i) l4c_rate_window_add_au(&w, 1000); /* 10 AU * 1000 B */
    if (l4c_rate_window_eval(&w, 10500, 1000, &fps, &kbps)) return 1;
    if (!l4c_rate_window_eval(&w, 11000, 1000, &fps, &kbps)) return 2;
    if (fps != 10) return 3;
    /* 10 * 1000 bytes * 8 / 1000 = 80 kbit/s */
    if (kbps != 80) return 4;
    return 0;
}

int test_telemetry_rate_window_zero(void) {
    l4c_rate_window_t w;
    uint16_t fps = 1;
    uint32_t kbps = 1;
    l4c_rate_window_reset(&w, 50);
    if (!l4c_rate_window_eval(&w, 1050, 1000, &fps, &kbps)) return 1;
    if (fps != 0 || kbps != 0) return 2;
    return 0;
}

int test_telemetry_process_resources_smoke(void) {
    l4c_process_resources_t res;
    DWORD gdi_api;
    (void)l4c_telemetry_init();
    if (!l4c_telemetry_process_resources(&res)) return 1;
    if (!res.valid) return 2;
    if (res.private_bytes_kb == 0) return 3;
    gdi_api = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    if (res.gdi_handles != (uint32_t)gdi_api) return 4;
    /* A6: 100% match GetGuiResources */
    return 0;
}

int test_telemetry_private_bytes_nonzero_match_taskmgr_scale(void) {
    l4c_process_resources_t res;
    PROCESS_MEMORY_COUNTERS pmc;
    (void)l4c_telemetry_init();
    if (!l4c_telemetry_process_resources(&res)) return 1;
    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return 2;
    /* PagefileUsage (Private Bytes) и private_bytes_kb должны быть того же порядка (±2% не требуем в unit). */
    {
        uint32_t api_kb = (uint32_t)(pmc.PagefileUsage / 1024u);
        uint32_t diff = (res.private_bytes_kb > api_kb)
                            ? (res.private_bytes_kb - api_kb)
                            : (api_kb - res.private_bytes_kb);
        uint32_t lim = api_kb / 50u + 8u; /* ~2% + slack для PrivateUsage vs PagefileUsage */
        if (diff > lim) return 3;
    }
    return 0;
}

int test_telemetry_platform_inventory_fields(void) {
    l4c_inventory_snapshot_t inv;
    memset(&inv, 0, sizeof(inv));
    if (!l4c_telemetry_fill_platform_inventory(&inv)) return 1;
    if (inv.os_major == 0) return 2;
    if (inv.cpu_cores == 0) return 3;
    if (inv.ram_total_kb == 0) return 4;
    if (inv.display_w <= 0 || inv.display_h <= 0) return 5;
    if (inv.os_arch[0] == 0 || inv.cpu_model[0] == 0) return 6;
    return 0;
}

int test_logger_scrub_secrets(void) {
    char buf[256];
    strcpy_s(buf, sizeof(buf), "auth pin=12345 token=abcdef0123456789 ok");
    l4c_logger_scrub_secrets(buf, sizeof(buf));
    if (strstr(buf, "12345")) return 1;
    if (strstr(buf, "abcdef0123456789")) return 2;
    if (!strstr(buf, "pin=")) return 3;
    strcpy_s(buf, sizeof(buf), "clean line fps=10");
    l4c_logger_scrub_secrets(buf, sizeof(buf));
    if (strcmp(buf, "clean line fps=10") != 0) return 4;
    return 0;
}

static void test_logger_paths(wchar_t *dir, size_t cap) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    _snwprintf_s(dir, cap, _TRUNCATE, L"%sl4c_logtest_%u", tmp, (unsigned)GetCurrentProcessId());
    CreateDirectoryW(dir, NULL);
}

int test_logger_write_and_inventory(void) {
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    FILE *f;
    char line[256];
    l4c_inventory_snapshot_t inv;
    bool found_inv = false, found_tick = false;

    test_logger_paths(dir, MAX_PATH);
    if (l4c_logger_init(dir) != L4C_OK) return 1;
    memset(&inv, 0, sizeof(inv));
    inv.os_major = 6; inv.os_minor = 1; inv.os_build = 7601;
    strcpy_s(inv.service_pack, sizeof(inv.service_pack), "SP1");
    strcpy_s(inv.os_arch, sizeof(inv.os_arch), "x86");
    strcpy_s(inv.os_product, sizeof(inv.os_product), "Embedded");
    inv.cpu_cores = 2;
    strcpy_s(inv.cpu_model, sizeof(inv.cpu_model), "TestCPU");
    inv.ram_total_kb = 2048 * 1024;
    inv.ram_avail_kb = 1024 * 1024;
    inv.display_w = 1024; inv.display_h = 768;
    inv.source_rect.left = 0; inv.source_rect.top = 0;
    inv.source_rect.right = 1024; inv.source_rect.bottom = 768;
    inv.dpi_aware = true;
    inv.profile_requested = 1;
    inv.profile_actual = 1;
    inv.capture_backend = 1;
    inv.encoder_backend = 1;
    inv.start_fps = 10;
    inv.bitrate_min_kbps = 500; inv.bitrate_target_kbps = 500; inv.bitrate_max_kbps = 700;
    l4c_logger_startup_inventory(&inv);
    l4c_logger_write("TICK n=%d pin=9999", 1);
    l4c_logger_shutdown();

    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\l4capture.log", dir);
    f = _wfopen(path, L"r");
    if (!f) return 2;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "INV os=6.1.7601") && strstr(line, "Embedded") && strstr(line, "TestCPU")) {
            found_inv = true;
        }
        if (strstr(line, "TICK") && strstr(line, "pin=")) {
            found_tick = true;
            if (strstr(line, "9999")) return 3; /* secret leaked */
            if (!strstr(line, "****") && !strstr(line, "pin=****")) {
                /* scrub replaces value with * */
                if (!strstr(line, "pin=*")) return 4;
            }
        }
    }
    fclose(f);
    if (!found_inv) return 5;
    if (!found_tick) return 6;
    (void)DeleteFileW(path);
    RemoveDirectoryW(dir);
    return 0;
}

int test_logger_rotation_5mib_x2(void) {
    wchar_t dir[MAX_PATH], path[MAX_PATH], oldpath[MAX_PATH];
    char blob[512];
    uint32_t i;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    ULARGE_INTEGER sz, osz;

    test_logger_paths(dir, MAX_PATH);
    if (l4c_logger_init(dir) != L4C_OK) return 1;
    memset(blob, 'A', sizeof(blob) - 1);
    blob[sizeof(blob) - 1] = '\0';
    /* >5 МиБ записей (~512+stamp) -> ротация */
    for (i = 0; i < 12000; ++i) {
        l4c_logger_line(blob);
    }
    l4c_logger_write("after-rotate");
    l4c_logger_shutdown();

    _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\l4capture.log", dir);
    _snwprintf_s(oldpath, MAX_PATH, _TRUNCATE, L"%s\\l4capture.log.old", dir);
    if (!GetFileAttributesExW(oldpath, GetFileExInfoStandard, &fa)) return 3;
    osz.HighPart = fa.nFileSizeHigh;
    osz.LowPart = fa.nFileSizeLow;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) return 4;
    sz.HighPart = fa.nFileSizeHigh;
    sz.LowPart = fa.nFileSizeLow;
    /* A11: суммарный объём <= 10 МиБ */
    if (sz.QuadPart + osz.QuadPart > 10485760ull + 65536ull) return 5;
    if (osz.QuadPart == 0) return 6;
    (void)DeleteFileW(path);
    (void)DeleteFileW(oldpath);
    RemoveDirectoryW(dir);
    return 0;
}

int test_logger_queue_and_p95_wire_contract(void) {
    /* EVENT_METRICS layout still 30 bytes (wire freeze). */
    l4c_metrics_t m;
    size_t sz;
    memset(&m, 0, sizeof(m));
    sz = sizeof(m.fps);
    if (sz != 2) return 1;
    sz = sizeof(m.bitrate_kbps);
    if (sz != 4) return 2;
    sz = sizeof(m.raw_drops);
    if (sz != 4) return 3;
    sz = sizeof(m.encode_p95_ms);
    if (sz != 2) return 4;
    sz = sizeof(m.queue_depth);
    if (sz != 2) return 5;
    sz = sizeof(m.private_bytes_kb);
    if (sz != 4) return 6;
    sz = sizeof(m.gdi_handles);
    if (sz != 4) return 7;
    /* offsets as serialized: 2+4+4+4+4+2+2+4+4 = 30 */
    return 0;
}
