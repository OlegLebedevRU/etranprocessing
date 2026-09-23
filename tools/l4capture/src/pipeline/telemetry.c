/*
 * L4C-10 accurate runtime telemetry: Private Bytes, GUI handles, encode p95,
 * measured FPS/bitrate windows. Optional APIs resolved dynamically (Win7-safe).
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#include <string.h>
#include "l4capture/telemetry.h"
#include "l4capture/degrade_controller.h"

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

typedef BOOL (WINAPI *fn_get_process_memory_info_t)(HANDLE, PROCESS_MEMORY_COUNTERS *, DWORD);

static HMODULE g_psapi_mod = NULL;
static bool g_psapi_owned = false;
static fn_get_process_memory_info_t g_GetProcessMemoryInfo = NULL;

static HMODULE load_system_dll(const wchar_t *name)
{
    HMODULE mod = LoadLibraryExW(name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (mod) return mod;
    /* Win7 SP1 без KB2533623: обычный поиск системной DLL. */
    return LoadLibraryW(name);
}

l4c_status_t l4c_telemetry_init(void)
{
    if (g_GetProcessMemoryInfo) return L4C_OK;
    g_psapi_mod = GetModuleHandleW(L"psapi.dll");
    if (!g_psapi_mod) {
        g_psapi_mod = load_system_dll(L"psapi.dll");
        g_psapi_owned = (g_psapi_mod != NULL);
    }
    if (g_psapi_mod) {
        g_GetProcessMemoryInfo =
            (fn_get_process_memory_info_t)(void *)GetProcAddress(g_psapi_mod, "GetProcessMemoryInfo");
    }
    return L4C_OK;
}

void l4c_telemetry_fini(void)
{
    g_GetProcessMemoryInfo = NULL;
    if (g_psapi_owned && g_psapi_mod) FreeLibrary(g_psapi_mod);
    g_psapi_mod = NULL;
    g_psapi_owned = false;
}

bool l4c_telemetry_process_resources(l4c_process_resources_t *out)
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    DWORD gdi_n = 0, user_n = 0, kernel_n = 0;
    bool mem_ok = false;

    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!g_GetProcessMemoryInfo) (void)l4c_telemetry_init();

    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (g_GetProcessMemoryInfo &&
        g_GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof(pmc))) {
        /* PagefileUsage = Private Bytes / Commit Size (Task Manager, Process Explorer). */
        SIZE_T priv = pmc.PagefileUsage ? pmc.PagefileUsage : pmc.PrivateUsage;
        out->private_bytes_kb = (uint32_t)(priv / 1024u);
        out->working_set_kb = (uint32_t)(pmc.WorkingSetSize / 1024u);
        mem_ok = true;
    }

    gdi_n = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    user_n = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    if (GetProcessHandleCount(GetCurrentProcess(), &kernel_n)) {
        out->kernel_handles = (uint32_t)kernel_n;
    }
    out->gdi_handles = (uint32_t)gdi_n;
    out->user_handles = (uint32_t)user_n;
    out->valid = mem_ok;
    return mem_ok;
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!src) return;
    n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

bool l4c_telemetry_fill_platform_inventory(l4c_inventory_snapshot_t *out)
{
    typedef LONG (WINAPI *rtl_get_version_fn)(void *);
    struct {
        ULONG os_version_info_size;
        ULONG major_version;
        ULONG minor_version;
        ULONG build_number;
        ULONG platform_id;
        WCHAR service_pack[128];
    } vi;
    rtl_get_version_fn rtl_get_version;
    HMODULE ntdll;
    SYSTEM_INFO si;
    MEMORYSTATUSEX ms;
    DWORD product_type = 0;
    HKEY hkey = NULL;

    if (!out) return false;

    memset(&vi, 0, sizeof(vi));
    vi.os_version_info_size = (ULONG)sizeof(vi);
    ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        rtl_get_version = (rtl_get_version_fn)(void *)GetProcAddress(ntdll, "RtlGetVersion");
        if (rtl_get_version) (void)rtl_get_version(&vi);
    }
    out->os_major = vi.major_version;
    out->os_minor = vi.minor_version;
    out->os_build = vi.build_number;
    {
        char sp[64];
        int i;
        for (i = 0; i < 63 && vi.service_pack[i]; ++i) {
            sp[i] = (char)((vi.service_pack[i] < 128) ? vi.service_pack[i] : '?');
        }
        sp[i] = '\0';
        copy_str(out->service_pack, sizeof(out->service_pack), sp[0] ? sp : "none");
    }
#if defined(_WIN64)
    copy_str(out->os_arch, sizeof(out->os_arch), "x64");
#else
    copy_str(out->os_arch, sizeof(out->os_arch), "x86");
#endif

    /* PRODUCT_* codes (winnt.h). POSReady 7 = 0x58 PRODUCT_EMBEDDED. */
    if (GetProductInfo(vi.major_version ? vi.major_version : 6,
                       vi.minor_version, 0, 0, &product_type)) {
        switch (product_type) {
        case 0x00000058: case 0x0000005B:
            copy_str(out->os_product, sizeof(out->os_product), "Embedded");
            break;
        case 0x0000000D: case 0x0000000C: case 0x00000012: case 0x00000013:
        case 0x00000007: case 0x00000018: case 0x00000021:
            copy_str(out->os_product, sizeof(out->os_product), "Server");
            break;
        default:
            copy_str(out->os_product, sizeof(out->os_product), "Workstation");
            break;
        }
    } else {
        copy_str(out->os_product, sizeof(out->os_product),
                 (vi.platform_id == VER_PLATFORM_WIN32_NT && vi.major_version >= 6) ? "Workstation" : "Legacy");
    }

    memset(&si, 0, sizeof(si));
    GetNativeSystemInfo(&si);
    out->cpu_cores = (uint32_t)si.dwNumberOfProcessors;
    out->cpu_model[0] = '\0';
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        DWORD cb = (DWORD)sizeof(out->cpu_model);
        DWORD type = 0;
        if (RegQueryValueExA(hkey, "ProcessorNameString", NULL, &type,
                             (LPBYTE)out->cpu_model, &cb) != ERROR_SUCCESS || type != REG_SZ) {
            out->cpu_model[0] = '\0';
        }
        RegCloseKey(hkey);
    }
    if (!out->cpu_model[0]) copy_str(out->cpu_model, sizeof(out->cpu_model), "unknown");

    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        out->ram_total_kb = ms.ullTotalPhys / 1024ull;
        out->ram_avail_kb = ms.ullAvailPhys / 1024ull;
    }

    out->display_w = GetSystemMetrics(SM_CXSCREEN);
    out->display_h = GetSystemMetrics(SM_CYSCREEN);
    out->dpi_aware = true;
    return true;
}

void l4c_p95_window_reset(l4c_p95_window_t *w)
{
    if (!w) return;
    w->count = 0;
}

bool l4c_p95_window_push(l4c_p95_window_t *w, uint32_t sample_ms)
{
    if (!w || w->count >= L4C_TELEMETRY_P95_MAX) return false;
    w->samples[w->count++] = sample_ms;
    return true;
}

bool l4c_p95_window_eval(const l4c_p95_window_t *w, uint16_t *out_p95_ms)
{
    uint32_t p95 = 0;
    if (!w || !out_p95_ms || w->count == 0) return false;
    if (!l4c_degrade_p95_from_samples(w->samples, w->count, &p95)) return false;
    *out_p95_ms = (uint16_t)(p95 > 0xFFFFu ? 0xFFFFu : p95);
    return true;
}

void l4c_rate_window_reset(l4c_rate_window_t *w, uint64_t now_ms)
{
    if (!w) return;
    w->start_ms = now_ms;
    w->au_count = 0;
    w->au_bytes = 0;
}

void l4c_rate_window_add_au(l4c_rate_window_t *w, uint32_t au_bytes)
{
    if (!w) return;
    if (w->au_count < 0xFFFFFFFFu) w->au_count++;
    if (w->au_bytes <= 0xFFFFFFFFu - au_bytes) w->au_bytes += au_bytes;
}

bool l4c_rate_window_eval(const l4c_rate_window_t *w, uint64_t now_ms, uint32_t window_ms,
                          uint16_t *out_fps, uint32_t *out_bitrate_kbps)
{
    uint64_t elapsed;
    uint64_t bits;
    if (!w || window_ms == 0) return false;
    if (w->start_ms == 0 || now_ms < w->start_ms) return false;
    elapsed = now_ms - w->start_ms;
    if (elapsed < window_ms) return false;
    if (out_fps) {
        /* AU за истёкшее окно, нормированные на window_ms. */
        uint64_t fps = ((uint64_t)w->au_count * 1000ull) / window_ms;
        *out_fps = (uint16_t)(fps > 0xFFFFu ? 0xFFFFu : fps);
    }
    if (out_bitrate_kbps) {
        /* kbit/s = (au_bytes * 8) / window_ms; window_ms=1000 -> (bytes*8)/1000. */
        bits = ((uint64_t)w->au_bytes * 8ull) / window_ms;
        *out_bitrate_kbps = (uint32_t)(bits > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)bits);
    }
    return true;
}
