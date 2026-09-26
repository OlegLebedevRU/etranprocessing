#include <windows.h>
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4201)
#endif
#define CINTERFACE
#define COBJMACROS
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <strmif.h>
#include <codecapi.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "ole32.lib")
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#ifndef MF_E_TRANSFORM_NEED_MORE_INPUT
#define MF_E_TRANSFORM_NEED_MORE_INPUT ((HRESULT)0xC00D6D72L)
#endif
#ifndef MF_E_TRANSFORM_STREAM_CHANGE
#define MF_E_TRANSFORM_STREAM_CHANGE ((HRESULT)0xC00D6D61L)
#endif
#ifndef METransformNeedInput
#define METransformNeedInput  601
#define METransformHaveOutput 602
#endif
#ifndef MF_EVENT_FLAG_NO_WAIT
#define MF_EVENT_FLAG_NO_WAIT 0x00000001
#endif

#include "l4capture/mf_encoder.h"
#include "l4capture/clock.h"
#include "l4capture/limits.h"
#include "l4capture/telemetry.h"
#include "l4capture/mft_event_gate.h"
#include "l4capture/logger.h"

#define l4c_clock_monotonic_ms l4c_now_monotonic_ms

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Autonomous GUID definitions (no linkage to mfuuid.lib required) */
static const GUID s_CLSID_CMSH264EncoderMFT = { 0x6ca50344, 0x051a, 0x4ded, { 0x97, 0x79, 0xa4, 0x33, 0x05, 0x16, 0x5e, 0x35 } };
static const GUID s_MFT_CATEGORY_VIDEO_ENCODER = { 0xf79eac7d, 0xe545, 0x4387, { 0xbd, 0xee, 0xd6, 0x47, 0xd7, 0xbd, 0xe4, 0x2a } };
static const GUID s_MFMediaType_Video = { 0x73646976, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID s_MFVideoFormat_H264 = { 0x34363248, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID s_MFVideoFormat_NV12 = { 0x3231564e, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID s_MF_MT_MAJOR_TYPE = { 0x48eba18e, 0xf827, 0x49e4, { 0x97, 0x24, 0xed, 0xa4, 0x6e, 0x48, 0x8b, 0x73 } };
static const GUID s_MF_MT_SUBTYPE = { 0xf7e34c9a, 0x42e8, 0x4714, { 0xb7, 0x4b, 0xcb, 0xeb, 0x27, 0x5d, 0x5b, 0x4f } };
static const GUID s_MF_MT_FRAME_SIZE = { 0x1652c33d, 0xd6b2, 0x4012, { 0xb8, 0x34, 0x72, 0x03, 0x08, 0x49, 0xa3, 0x7d } };
static const GUID s_MF_MT_FRAME_RATE = { 0xc450720c, 0x9540, 0x4e20, { 0x88, 0x49, 0x49, 0x1d, 0x74, 0xac, 0x62, 0x91 } };
static const GUID s_MF_MT_PIXEL_ASPECT_RATIO = { 0xc63764b2, 0xd678, 0x4376, { 0xb6, 0x1d, 0x50, 0xe2, 0x49, 0xac, 0xa4, 0xe2 } };
static const GUID s_MF_MT_INTERLACE_MODE = { 0xe272446c, 0x8112, 0x4972, { 0x81, 0x07, 0xf0, 0x89, 0xac, 0x4b, 0x30, 0x8a } };
static const GUID s_MF_MT_AVG_BITRATE = { 0x20332624, 0xfb0d, 0x4d9e, { 0xbd, 0x0d, 0xcb, 0xf6, 0x78, 0x6c, 0x10, 0x2e } };
static const GUID s_MF_MT_MPEG2_PROFILE = { 0xad76a80b, 0x2d5c, 0x4e0b, { 0xb3, 0x75, 0x64, 0xe5, 0x20, 0x13, 0x70, 0x36 } };
static const GUID s_MF_MT_MPEG2_LEVEL = { 0x96f66574, 0x11c5, 0x4015, { 0x86, 0x66, 0xbf, 0xf5, 0x16, 0x43, 0x6d, 0xa7 } };
static const GUID s_IID_IMFTransform = { 0xbf94c121, 0x5b05, 0x4e6f, { 0x80, 0x00, 0xba, 0x59, 0x89, 0x61, 0x41, 0x4d } };
static const GUID s_IID_ICodecAPI = { 0x901db4c7, 0x31ce, 0x41a2, { 0x85, 0xdc, 0x8f, 0xa0, 0xbf, 0x41, 0xb8, 0xda } };
static const GUID s_CODECAPI_AVEncCommonRateControlMode = { 0x1c0608e9, 0x370c, 0x4710, { 0x8a, 0x58, 0xcb, 0x61, 0x81, 0xc4, 0x24, 0x23 } };
static const GUID s_CODECAPI_AVEncCommonMeanBitRate = { 0xf7222374, 0x2144, 0x4815, { 0xb5, 0x50, 0xa3, 0x7f, 0x8e, 0x12, 0xee, 0x52 } };
static const GUID s_CODECAPI_AVEncCommonMaxBitRate = { 0x9651eae4, 0x39b9, 0x4ebf, { 0x85, 0xef, 0xd7, 0xf4, 0x44, 0xec, 0x74, 0x65 } };
static const GUID s_CODECAPI_AVEncCommonLowLatency = { 0x9d3ecd55, 0x89e8, 0x490a, { 0x97, 0x0a, 0x0c, 0x95, 0x48, 0xd5, 0xa5, 0x6e } };
static const GUID s_CODECAPI_AVEncCommonQualityVsSpeed = { 0x98332df8, 0x03cd, 0x476b, { 0x89, 0xfa, 0x3f, 0x9e, 0x46, 0x76, 0x3b, 0x1a } };
static const GUID s_CODECAPI_AVEncMPVDefaultBPictureCount = { 0x8d390aac, 0xdc5c, 0x4200, { 0xb5, 0x7f, 0x81, 0x4d, 0x04, 0xba, 0xba, 0xb2 } };
static const GUID s_CODECAPI_AVEncMPVGOPSize = { 0x95f31b26, 0x95a4, 0x41aa, { 0x93, 0x03, 0x24, 0x6a, 0x7f, 0xc6, 0xee, 0xf1 } };
static const GUID s_CODECAPI_AVEncH264CABACEnable = { 0xee6cad62, 0xd305, 0x4248, { 0xa5, 0x0e, 0xe1, 0xb2, 0x55, 0xf7, 0xca, 0xf8 } };
static const GUID s_CODECAPI_AVEncVideoForceKeyFrame = { 0x398c1b98, 0x8353, 0x475a, { 0x9e, 0xf2, 0x8f, 0x26, 0x5d, 0x26, 0x03, 0x45 } };
static const GUID s_CODECAPI_AVEncVideoMaxQP = { 0x3daf6f66, 0xa6a7, 0x45e0, { 0xa8, 0xe5, 0xf2, 0x74, 0x3f, 0x46, 0xa3, 0xa2 } };
static const GUID s_MF_TRANSFORM_ASYNC_UNLOCK = { 0xe5666d6b, 0x3422, 0x4eb6, { 0xa4, 0x21, 0xda, 0x7d, 0xb1, 0xf8, 0xe2, 0x07 } };
static const GUID s_MFT_FRIENDLY_NAME_Attribute = { 0x314ffbae, 0x5b41, 0x4c95, { 0x9c, 0x19, 0x4e, 0x7d, 0x58, 0x6f, 0xac, 0xe3 } };

/* Dynamic Media Foundation function pointer definitions */
typedef HRESULT (WINAPI *PFN_MFStartup)(ULONG Version, DWORD dwFlags);
typedef HRESULT (WINAPI *PFN_MFShutdown)(void);
typedef HRESULT (WINAPI *PFN_MFTEnumEx)(
    GUID guidCategory,
    UINT32 Flags,
    const MFT_REGISTER_TYPE_INFO *pInputType,
    const MFT_REGISTER_TYPE_INFO *pOutputType,
    IMFActivate ***pppMFTActivate,
    UINT32 *pnumMFTActivate
);
typedef HRESULT (WINAPI *PFN_MFCreateMediaType)(IMFMediaType **ppMFType);
typedef HRESULT (WINAPI *PFN_MFCreateSample)(IMFSample **ppIMFSample);
typedef HRESULT (WINAPI *PFN_MFCreateMemoryBuffer)(DWORD cbMaxLength, IMFMediaBuffer **ppBuffer);
typedef HRESULT (WINAPI *PFN_MFCreateAttributes)(IMFAttributes **ppMFAttributes, UINT32 cInitialSize);

typedef struct {
    HMODULE h_mfplat;
    HMODULE h_mf;
    PFN_MFStartup pfn_MFStartup;
    PFN_MFShutdown pfn_MFShutdown;
    PFN_MFTEnumEx pfn_MFTEnumEx;
    PFN_MFCreateMediaType pfn_MFCreateMediaType;
    PFN_MFCreateSample pfn_MFCreateSample;
    PFN_MFCreateMemoryBuffer pfn_MFCreateMemoryBuffer;
    PFN_MFCreateAttributes pfn_MFCreateAttributes;
    bool initialized;
} mf_loader_t;

static mf_loader_t g_mf = {0};

/* Test injection hooks */
static bool s_test_injected_probe_fail = false;
static bool s_test_injected_process_fail = false;
static int s_probe_cached = -1;
static bool probe_encoded_sequence(void);

/* Persistent capability cache (mft_capability.ini next to exe).
 * reason: ok | unavailable | timeout | negotiate_fail | runtime_fail */
typedef enum {
    L4C_MFT_CACHE_MISS = 0,
    L4C_MFT_CACHE_OK,
    L4C_MFT_CACHE_UNAVAILABLE,
    L4C_MFT_CACHE_RETRYABLE
} l4c_mft_cache_kind_t;

#define L4C_MFT_PROBE_BUDGET_MS 5000u
#define L4C_MFT_STRATEGY_AVAIL_FULL  1
#define L4C_MFT_STRATEGY_CRAFTED     2
#define L4C_MFT_STRATEGY_AVAIL_MIN   3
static int s_mft_preferred_strategy = L4C_MFT_STRATEGY_AVAIL_FULL;

void l4c_mf_encoder_test_inject_probe_fail(bool fail) {
    s_test_injected_probe_fail = fail;
    if (fail) {
        s_probe_cached = -1;
    }
}

void l4c_mf_encoder_test_inject_process_fail(bool fail) {
    s_test_injected_process_fail = fail;
}

static bool mft_cache_path(wchar_t *out, size_t cap) {
    wchar_t *slash;
    if (!out || cap < 32) return false;
    if (!GetModuleFileNameW(NULL, out, (DWORD)cap)) return false;
    slash = wcsrchr(out, L'\\');
    if (!slash) return false;
    slash[1] = L'\0';
    if (wcslen(out) + 20 >= cap) return false;
    wcscat_s(out, cap, L"mft_capability.ini");
    return true;
}

static uint32_t mf_quality_max_qp(void) {
    wchar_t path[MAX_PATH];
    wchar_t *slash;
    UINT qp;
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) return 0;
    slash = wcsrchr(path, L'\\');
    if (!slash) return 0;
    wcscpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - path), L"idle_refresh.ini");
    qp = GetPrivateProfileIntW(L"quality", L"max_qp", 0, path);
    return qp >= 20u && qp <= 40u ? qp : 0;
}

static l4c_mft_cache_kind_t mft_cache_parse_reason(const char *reason, int hw) {
    if (hw == 1) return L4C_MFT_CACHE_OK;
    if (hw == 0) {
        if (reason && strcmp(reason, "unavailable") == 0) return L4C_MFT_CACHE_UNAVAILABLE;
        return L4C_MFT_CACHE_RETRYABLE;
    }
    return L4C_MFT_CACHE_MISS;
}

static l4c_mft_cache_kind_t mft_cache_load(void) {
    wchar_t path[MAX_PATH];
    FILE *fp;
    char line[128];
    char reason[32];
    int hw = -1;
    int version = 0;
    int strategy = 0;

    if (!mft_cache_path(path, MAX_PATH)) return L4C_MFT_CACHE_MISS;
    fp = _wfopen(path, L"r");
    if (!fp) return L4C_MFT_CACHE_MISS;
    reason[0] = '\0';
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "version=", 8) == 0) version = atoi(line + 8);
        else if (strncmp(line, "hw=", 3) == 0) hw = atoi(line + 3);
        else if (strncmp(line, "strategy=", 9) == 0) strategy = atoi(line + 9);
        else if (strncmp(line, "reason=", 7) == 0) {
            char *p = line + 7;
            size_t n = 0;
            while (p[n] && p[n] != '\n' && p[n] != '\r' && n + 1 < sizeof(reason)) {
                reason[n] = p[n];
                n++;
            }
            reason[n] = '\0';
        }
    }
    fclose(fp);
    if (version != 2) return L4C_MFT_CACHE_MISS;
    if (strategy >= 1 && strategy <= 3) s_mft_preferred_strategy = strategy;
    return mft_cache_parse_reason(reason, hw);
}

static void mft_cache_save(int hw, const char *reason, uint64_t probe_ms) {
    wchar_t path[MAX_PATH];
    FILE *fp;
    SYSTEMTIME st;

    if (!mft_cache_path(path, MAX_PATH)) return;
    fp = _wfopen(path, L"w");
    if (!fp) return;
    GetLocalTime(&st);
    fprintf(fp, "version=2\n");
    fprintf(fp, "hw=%d\n", hw ? 1 : 0);
    fprintf(fp, "strategy=%d\n", s_mft_preferred_strategy);
    fprintf(fp, "reason=%s\n", reason ? reason : "unknown");
    fprintf(fp, "probe_ms=%llu\n", (unsigned long long)probe_ms);
    fprintf(fp, "updated_utc=%04u-%02u-%02u %02u:%02u:%02u\n",
            (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
            (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
    fclose(fp);
}

void l4c_mf_encoder_cache_reset(void) {
    wchar_t path[MAX_PATH];
    s_probe_cached = -1;
    if (mft_cache_path(path, MAX_PATH)) {
        (void)DeleteFileW(path);
    }
}

void l4c_mf_encoder_note_runtime_failure(void) {
    /* Retryable: следующий старт может один раз перепробовать MFT. */
    s_probe_cached = 0;
    mft_cache_save(0, "runtime_fail", 0);
}

/* Helper to load libraries strictly from System32 */
static HMODULE load_system_dll(const wchar_t *dll_name) {
    HMODULE h = LoadLibraryExW(dll_name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) {
        WCHAR path[MAX_PATH];
        UINT len = GetSystemDirectoryW(path, MAX_PATH);
        if (len > 0 && len < MAX_PATH - 32) {
            wcscat_s(path, MAX_PATH, L"\\");
            wcscat_s(path, MAX_PATH, dll_name);
            h = LoadLibraryW(path);
        }
    }
    return h;
}

static bool init_mf_loader(void) {
    if (g_mf.initialized) return true;

    /* MF/MFT требуют COM; DXGI-захват в процессе мог не инициализировать. */
    (void)CoInitializeEx(NULL, COINIT_MULTITHREADED);

    g_mf.h_mfplat = load_system_dll(L"mfplat.dll");
    if (!g_mf.h_mfplat) return false;

    g_mf.h_mf = load_system_dll(L"mf.dll");

    g_mf.pfn_MFStartup = (PFN_MFStartup)GetProcAddress(g_mf.h_mfplat, "MFStartup");
    g_mf.pfn_MFShutdown = (PFN_MFShutdown)GetProcAddress(g_mf.h_mfplat, "MFShutdown");
    g_mf.pfn_MFCreateMediaType = (PFN_MFCreateMediaType)GetProcAddress(g_mf.h_mfplat, "MFCreateMediaType");
    g_mf.pfn_MFCreateSample = (PFN_MFCreateSample)GetProcAddress(g_mf.h_mfplat, "MFCreateSample");
    g_mf.pfn_MFCreateMemoryBuffer = (PFN_MFCreateMemoryBuffer)GetProcAddress(g_mf.h_mfplat, "MFCreateMemoryBuffer");
    g_mf.pfn_MFCreateAttributes = (PFN_MFCreateAttributes)GetProcAddress(g_mf.h_mfplat, "MFCreateAttributes");

    g_mf.pfn_MFTEnumEx = (PFN_MFTEnumEx)GetProcAddress(g_mf.h_mfplat, "MFTEnumEx");
    if (!g_mf.pfn_MFTEnumEx && g_mf.h_mf) {
        g_mf.pfn_MFTEnumEx = (PFN_MFTEnumEx)GetProcAddress(g_mf.h_mf, "MFTEnumEx");
    }

    if (!g_mf.pfn_MFStartup || !g_mf.pfn_MFShutdown || !g_mf.pfn_MFTEnumEx ||
        !g_mf.pfn_MFCreateMediaType || !g_mf.pfn_MFCreateSample || !g_mf.pfn_MFCreateMemoryBuffer) {
        return false;
    }

    g_mf.initialized = true;
    return true;
}

/*
 * Bit reader for parsing SPS headers and validating 854x480 cropping
 */
typedef struct {
    const uint8_t *data;
    size_t size;
    size_t byte_pos;
    int bit_pos;
} l4c_bit_reader_t;

static void bit_reader_init(l4c_bit_reader_t *br, const uint8_t *data, size_t size) {
    br->data = data;
    br->size = size;
    br->byte_pos = 0;
    br->bit_pos = 7;
}

static uint32_t read_bit(l4c_bit_reader_t *br) {
    uint32_t val;
    if (br->byte_pos >= br->size) return 0;
    val = (br->data[br->byte_pos] >> br->bit_pos) & 1;
    if (br->bit_pos == 0) {
        br->bit_pos = 7;
        br->byte_pos++;
        if (br->byte_pos >= 2 && br->byte_pos < br->size &&
            br->data[br->byte_pos - 2] == 0 && br->data[br->byte_pos - 1] == 0 &&
            br->data[br->byte_pos] == 3) {
            br->byte_pos++;
        }
    } else {
        br->bit_pos--;
    }
    return val;
}

static uint32_t read_bits(l4c_bit_reader_t *br, int n) {
    uint32_t res = 0;
    int i;
    for (i = 0; i < n; ++i) {
        res = (res << 1) | read_bit(br);
    }
    return res;
}

static uint32_t read_ue(l4c_bit_reader_t *br) {
    int leading_zeros = 0;
    uint32_t rest;
    while (read_bit(br) == 0 && leading_zeros < 32 && br->byte_pos < br->size) {
        leading_zeros++;
    }
    if (leading_zeros == 0) return 0;
    if (leading_zeros >= 32) return 0;
    rest = read_bits(br, leading_zeros);
    return (1u << leading_zeros) - 1u + rest;
}

static int32_t read_se(l4c_bit_reader_t *br) {
    uint32_t val = read_ue(br);
    if (val & 1) {
        return (int32_t)((val + 1) >> 1);
    } else {
        return -(int32_t)(val >> 1);
    }
}

static bool check_sps_cropping(const uint8_t *sps, size_t size, uint32_t target_w, uint32_t target_h) {
    l4c_bit_reader_t br;
    uint32_t profile_idc, pic_order_cnt_type, pic_width_in_mbs_minus1, pic_height_in_map_units_minus1;
    uint32_t frame_mbs_only_flag, frame_cropping_flag;
    uint32_t crop_left = 0, crop_right = 0, crop_top = 0, crop_bottom = 0;
    uint32_t visible_w, visible_h;

    if (!sps || size < 4) return false;
    bit_reader_init(&br, sps, size);

    /* Skip NAL header byte */
    read_bits(&br, 8);
    profile_idc = read_bits(&br, 8);
    read_bits(&br, 8); /* constraint flags */
    read_bits(&br, 8); /* level_idc */
    read_ue(&br);      /* seq_parameter_set_id */

    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244 ||
        profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118 ||
        profile_idc == 128 || profile_idc == 138 || profile_idc == 139 || profile_idc == 134) {
        uint32_t chroma_format_idc = read_ue(&br);
        if (chroma_format_idc == 3) read_bit(&br);
        read_ue(&br); /* bit_depth_luma_minus8 */
        read_ue(&br); /* bit_depth_chroma_minus8 */
        read_bit(&br); /* qpprime_y_zero_transform_bypass_flag */
        if (read_bit(&br)) { /* seq_scaling_matrix_present_flag */
            int i;
            for (i = 0; i < ((chroma_format_idc != 3) ? 8 : 12); ++i) {
                if (read_bit(&br)) {
                    int last_scale = 8, next_scale = 8, j;
                    int count = (i < 6) ? 16 : 64;
                    for (j = 0; j < count; ++j) {
                        if (next_scale != 0) {
                            int delta_scale = (int)read_se(&br);
                            next_scale = (last_scale + delta_scale + 256) % 256;
                        }
                        last_scale = (next_scale == 0) ? last_scale : next_scale;
                    }
                }
            }
        }
    }

    read_ue(&br); /* log2_max_frame_num_minus4 */
    pic_order_cnt_type = read_ue(&br);
    if (pic_order_cnt_type == 0) {
        read_ue(&br); /* log2_max_pic_order_cnt_lsb_minus4 */
    } else if (pic_order_cnt_type == 1) {
        uint32_t i, num_ref;
        read_bit(&br);
        read_se(&br);
        read_se(&br);
        num_ref = read_ue(&br);
        for (i = 0; i < num_ref; ++i) read_se(&br);
    }
    read_ue(&br); /* max_num_ref_frames */
    read_bit(&br); /* gaps_in_frame_num_value_allowed_flag */
    pic_width_in_mbs_minus1 = read_ue(&br);
    pic_height_in_map_units_minus1 = read_ue(&br);
    frame_mbs_only_flag = read_bit(&br);
    if (!frame_mbs_only_flag) read_bit(&br);
    read_bit(&br); /* direct_8x8_inference_flag */
    frame_cropping_flag = read_bit(&br);
    if (frame_cropping_flag) {
        crop_left = read_ue(&br);
        crop_right = read_ue(&br);
        crop_top = read_ue(&br);
        crop_bottom = read_ue(&br);
    }

    visible_w = ((pic_width_in_mbs_minus1 + 1) * 16) - (crop_left + crop_right) * 2;
    visible_h = ((2 - frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1) * 16) - (crop_top + crop_bottom) * 2;

    if (target_w == 854 && visible_w != 854) {
        /* Not matching 854 */
        return false;
    }
    (void)target_h;
    (void)visible_h;
    return true;
}

/*
 * Backend implementation structure
 */
typedef struct {
    l4c_encoder_backend_t base;
    l4c_encoder_config_t config;

    IMFActivate *activate;
    IMFTransform *transform;
    ICodecAPI *codec_api;
    IMFMediaEventGenerator *event_gen;

    IMFSample *input_sample;
    IMFMediaBuffer *input_buffer;

    IMFSample *output_sample;
    IMFMediaBuffer *output_buffer;
    MFT_OUTPUT_STREAM_INFO output_stream_info;

    uint8_t *au_buffer;
    l4c_nal_desc_t nals[64];
    l4c_nal_desc_t out_nals[64];

    uint8_t cached_sps[256];
    uint32_t cached_sps_len;
    uint8_t cached_pps[256];
    uint32_t cached_pps_len;

    uint64_t last_idr_ms;
    uint64_t last_force_idr_req_ms;
    bool first_frame;
    bool force_key_frame_pending;
    bool mf_started;
    l4c_mft_event_gate_t events;
    bool input_pending;
    bool input_submitted;
    uint64_t pending_pts_ms;
    uint64_t pending_since_ms;
    uint64_t input_wait_since_ms;
} mf_encoder_backend_t;

/* Forward declarations */
static l4c_status_t mf_init(struct l4c_encoder_backend *self, const l4c_encoder_config_t *config);
static l4c_status_t mf_encode(struct l4c_encoder_backend *self, const l4c_raw_frame_t *raw, l4c_access_unit_t *out_au);
static l4c_status_t mf_force_idr(struct l4c_encoder_backend *self);
static void mf_release_au(struct l4c_encoder_backend *self, l4c_access_unit_t *au);
static void mf_destroy(struct l4c_encoder_backend *self);

static const l4c_encoder_backend_vtable_t s_mf_vtable = {
    mf_init,
    mf_encode,
    mf_force_idr,
    mf_release_au,
    mf_destroy
};

/* Intel QSV: не использовать GetGUID/SetGUID на IMFMediaType — в этом
 * toolchain их C-vtable слот мимо (GetGUID → MF_E_INVALIDMEDIATYPE), из-за
 * чего типы «невалидны». Рабочий путь: GetMajorType + мутировать available
 * type через SetUINT32/SetUINT64 и выставить его как output/input. */
static bool negotiate_crafted(IMFTransform *pTransform, UINT32 w, UINT32 h, UINT32 fps, UINT32 br) {
    IMFMediaType *avail = NULL;
    IMFMediaType *pOutputType = NULL;
    IMFMediaType *pInputType = NULL;
    GUID maj, sub;
    HRESULT hr;

    hr = pTransform->lpVtbl->GetOutputAvailableType(pTransform, 0, 0, &avail);
    if (FAILED(hr) || !avail) return false;
    memset(&maj, 0, sizeof(maj));
    memset(&sub, 0, sizeof(sub));
    avail->lpVtbl->GetMajorType(avail, &maj);
    /* Subtype via GetItem (VT_CLSID) — GetGUID vtable slot is unreliable here. */
    {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        hr = avail->lpVtbl->GetItem(avail, &MF_MT_SUBTYPE, &pv);
        if (SUCCEEDED(hr) && pv.vt == VT_CLSID && pv.puuid) sub = *pv.puuid;
        PropVariantClear(&pv);
    }
    avail->lpVtbl->Release(avail);

    hr = MFCreateMediaType(&pOutputType);
    if (FAILED(hr) || !pOutputType) return false;
    pOutputType->lpVtbl->SetGUID(pOutputType, &MF_MT_MAJOR_TYPE, &maj);
    pOutputType->lpVtbl->SetGUID(pOutputType, &MF_MT_SUBTYPE, &sub);
    pOutputType->lpVtbl->SetUINT64(pOutputType, &MF_MT_FRAME_SIZE, ((UINT64)w << 32) | h);
    pOutputType->lpVtbl->SetUINT64(pOutputType, &MF_MT_FRAME_RATE, ((UINT64)fps << 32) | 1);
    pOutputType->lpVtbl->SetUINT64(pOutputType, &MF_MT_PIXEL_ASPECT_RATIO, ((UINT64)1 << 32) | 1);
    pOutputType->lpVtbl->SetUINT32(pOutputType, &MF_MT_INTERLACE_MODE, 2);
    pOutputType->lpVtbl->SetUINT32(pOutputType, &MF_MT_AVG_BITRATE, br);
    hr = pTransform->lpVtbl->SetOutputType(pTransform, 0, pOutputType, 0);
    pOutputType->lpVtbl->Release(pOutputType);
    if (FAILED(hr)) return false;

    hr = MFCreateMediaType(&pInputType);
    if (FAILED(hr) || !pInputType) return false;
    pInputType->lpVtbl->SetGUID(pInputType, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    pInputType->lpVtbl->SetGUID(pInputType, &MF_MT_SUBTYPE, &MFVideoFormat_NV12);
    pInputType->lpVtbl->SetUINT64(pInputType, &MF_MT_FRAME_SIZE, ((UINT64)w << 32) | h);
    pInputType->lpVtbl->SetUINT64(pInputType, &MF_MT_FRAME_RATE, ((UINT64)fps << 32) | 1);
    pInputType->lpVtbl->SetUINT64(pInputType, &MF_MT_PIXEL_ASPECT_RATIO, ((UINT64)1 << 32) | 1);
    pInputType->lpVtbl->SetUINT32(pInputType, &MF_MT_INTERLACE_MODE, 2);
    hr = pTransform->lpVtbl->SetInputType(pTransform, 0, pInputType, 0);
    pInputType->lpVtbl->Release(pInputType);
    return SUCCEEDED(hr);
}

/* Intel QSV: start from GetOutputAvailableType, then attach size/rate/bitrate. */
static bool negotiate_from_available(IMFTransform *pTransform, UINT32 w, UINT32 h, UINT32 fps, UINT32 br) {
    IMFMediaType *avail = NULL;
    IMFMediaType *out_t = NULL;
    IMFMediaType *in_t = NULL;
    GUID maj = {0}, sub = {0};
    HRESULT hr;

    hr = pTransform->lpVtbl->GetOutputAvailableType(pTransform, 0, 0, &avail);
    if (FAILED(hr) || !avail) return false;
    avail->lpVtbl->GetGUID(avail, &s_MF_MT_MAJOR_TYPE, &maj);
    avail->lpVtbl->GetGUID(avail, &s_MF_MT_SUBTYPE, &sub);
    avail->lpVtbl->Release(avail);

    hr = MFCreateMediaType(&out_t);
    if (FAILED(hr) || !out_t) return false;
    out_t->lpVtbl->SetGUID(out_t, &s_MF_MT_MAJOR_TYPE, &maj);
    out_t->lpVtbl->SetGUID(out_t, &s_MF_MT_SUBTYPE, &sub);
    out_t->lpVtbl->SetUINT64(out_t, &s_MF_MT_FRAME_SIZE, ((UINT64)w << 32) | h);
    out_t->lpVtbl->SetUINT64(out_t, &s_MF_MT_FRAME_RATE, ((UINT64)fps << 32) | 1);
    out_t->lpVtbl->SetUINT64(out_t, &s_MF_MT_PIXEL_ASPECT_RATIO, ((UINT64)1 << 32) | 1);
    out_t->lpVtbl->SetUINT32(out_t, &s_MF_MT_INTERLACE_MODE, 2);
    out_t->lpVtbl->SetUINT32(out_t, &s_MF_MT_AVG_BITRATE, br);
    hr = pTransform->lpVtbl->SetOutputType(pTransform, 0, out_t, 0);
    out_t->lpVtbl->Release(out_t);
    if (FAILED(hr)) return false;

    hr = MFCreateMediaType(&in_t);
    if (FAILED(hr) || !in_t) return false;
    in_t->lpVtbl->SetGUID(in_t, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    in_t->lpVtbl->SetGUID(in_t, &MF_MT_SUBTYPE, &MFVideoFormat_NV12);
    in_t->lpVtbl->SetUINT64(in_t, &s_MF_MT_FRAME_SIZE, ((UINT64)w << 32) | h);
    in_t->lpVtbl->SetUINT64(in_t, &s_MF_MT_FRAME_RATE, ((UINT64)fps << 32) | 1);
    in_t->lpVtbl->SetUINT64(in_t, &s_MF_MT_PIXEL_ASPECT_RATIO, ((UINT64)1 << 32) | 1);
    in_t->lpVtbl->SetUINT32(in_t, &s_MF_MT_INTERLACE_MODE, 2);
    hr = pTransform->lpVtbl->SetInputType(pTransform, 0, in_t, 0);
    in_t->lpVtbl->Release(in_t);
    return SUCCEEDED(hr);
}

static void unlock_async_mft(IMFTransform *pTransform) {
    IMFAttributes *pAttr = NULL;
    if (!pTransform) return;
    if (SUCCEEDED(pTransform->lpVtbl->GetAttributes(pTransform, &pAttr)) && pAttr) {
        pAttr->lpVtbl->SetUINT32(pAttr, &s_MF_TRANSFORM_ASYNC_UNLOCK, 1u);
        pAttr->lpVtbl->Release(pAttr);
    }
}

/* Async HW MFT (Intel QSV): unlock on BOTH activate and transform, then Activate.
 * Без unlock SetInputType падает MF_E_TRANSFORM_ASYNC_LOCKED (0xC00D6D77). */
static HRESULT activate_unlocked(IMFActivate *act, IMFTransform **out_xf) {
    IMFAttributes *pAttr = NULL;
    HRESULT hr;

    *out_xf = NULL;
    if (!act) return E_POINTER;
    /* IMFActivate наследует IMFAttributes — unlock до/после ActivateObject. */
    pAttr = (IMFAttributes *)act;
    pAttr->lpVtbl->SetUINT32(pAttr, &s_MF_TRANSFORM_ASYNC_UNLOCK, 1u);
    hr = act->lpVtbl->ActivateObject(act, &s_IID_IMFTransform, (void**)out_xf);
    if (FAILED(hr) || !*out_xf) return hr;
    unlock_async_mft(*out_xf);
    return S_OK;
}

static bool negotiate_nv12_h264(IMFTransform *pTransform, UINT32 w, UINT32 h, UINT32 fps, UINT32 br) {
    if (!pTransform) return false;
    unlock_async_mft(pTransform);
    /* Preferred strategy only — dirty MFT после неудачного Set*Type. */
    return negotiate_crafted(pTransform, w, h, fps, br);
}

/* Fresh Activate required. Tries alternate strategies (adaptive, not machine-bound). */
static bool negotiate_nv12_h264_fallback(IMFTransform *pTransform, UINT32 w, UINT32 h, UINT32 fps, UINT32 br) {
    if (!pTransform) return false;
    unlock_async_mft(pTransform);
    if (negotiate_from_available(pTransform, w, h, fps, br)) {
        s_mft_preferred_strategy = L4C_MFT_STRATEGY_CRAFTED;
        return true;
    }
    return false;
}

/*
 * Hardware MFT probe: cache-first, discovery probe <= 5.0 s fail-closed.
 */
bool l4c_mf_encoder_is_supported(void) {
    uint64_t start_ms;
    HRESULT hr;
    MFT_REGISTER_TYPE_INFO in_type;
    MFT_REGISTER_TYPE_INFO out_type;
    IMFActivate **ppActivate = NULL;
    UINT32 count = 0;
    IMFTransform *pTransform = NULL;
    bool supported = false;
    const char *fail_reason = "negotiate_fail";
    UINT32 i;

    if (s_test_injected_probe_fail) return false;
    if (s_probe_cached != -1) return (s_probe_cached == 1);

    /* Cache remembers negotiation strategy only. Hardware/driver updates and
     * a prior successful type negotiation cannot establish runtime liveness. */
    (void)mft_cache_load();

    start_ms = l4c_clock_monotonic_ms();

    if (!init_mf_loader()) {
        s_probe_cached = 0;
        mft_cache_save(0, "unavailable", 0);
        return false;
    }

    hr = g_mf.pfn_MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) {
        s_probe_cached = 0;
        mft_cache_save(0, "unavailable", 0);
        return false;
    }

    in_type.guidMajorType = s_MFMediaType_Video;
    in_type.guidSubtype = s_MFVideoFormat_NV12;
    out_type.guidMajorType = s_MFMediaType_Video;
    out_type.guidSubtype = s_MFVideoFormat_H264;

    /* MFT_ENUM_FLAG_HARDWARE=0x4 (mfapi.h). 0x100 — не флаг mfapi.
     * SORTANDFILTER (0x40) даёт полный скан+сортировку: дорого без HW MFT.
     * Для probe достаточно HARDWARE; сортировка не нужна. */
    hr = g_mf.pfn_MFTEnumEx(
        s_MFT_CATEGORY_VIDEO_ENCODER,
        0x00000004 /* HARDWARE */,
        &in_type,
        &out_type,
        &ppActivate,
        &count
    );

    if (FAILED(hr) || count == 0 || !ppActivate) {
        g_mf.pfn_MFShutdown();
        s_probe_cached = 0;
        mft_cache_save(0, "unavailable", l4c_clock_monotonic_ms() - start_ms);
        return false;
    }

    /* Раннего bailout до Activate больше нет: Intel ActivateObject ~1–4 с —
     * именно он и есть «редкое ожидание до 5 с». Fail-closed только в конце. */

    /* Перебор всех HW MFT: crafted на свежем transform, затем fallback
     * available-type — тоже на свежем Activate (dirty MFT не переиспользуем). */
    for (i = 0; i < count && !supported; ++i) {
        int attempt;
        for (attempt = 0; attempt < 2 && !supported; ++attempt) {
            pTransform = NULL;
            hr = activate_unlocked(ppActivate[i], &pTransform);
            if (FAILED(hr) || !pTransform) break;
            if (attempt == 0) {
                supported = negotiate_nv12_h264(pTransform, 640, 480, 10, 500000);
            } else {
                supported = negotiate_nv12_h264_fallback(pTransform, 640, 480, 10, 500000);
            }
            pTransform->lpVtbl->Release(pTransform);
            pTransform = NULL;
            ppActivate[i]->lpVtbl->ShutdownObject(ppActivate[i]);
        }
    }

    for (i = 0; i < count; ++i) {
        if (ppActivate[i]) ppActivate[i]->lpVtbl->Release(ppActivate[i]);
    }
    CoTaskMemFree(ppActivate);
    g_mf.pfn_MFShutdown();

    if (supported) {
        supported = probe_encoded_sequence();
        if (!supported) fail_reason = "encode_probe_fail";
    }

    /* Fail-closed: true только если переговоры уложились в бюджет.
     * Успех за бюджетом не выбрасываем — сохраняем ok (конфиг уже найден). */
    if (supported) {
        s_probe_cached = 1;
        mft_cache_save(1, "ok", l4c_clock_monotonic_ms() - start_ms);
        return true;
    }

    if (l4c_clock_monotonic_ms() - start_ms > L4C_MFT_PROBE_BUDGET_MS) {
        fail_reason = "timeout";
    }
    s_probe_cached = 0;
    mft_cache_save(0, fail_reason, l4c_clock_monotonic_ms() - start_ms);
    return false;
}

/*
 * Factory creation
 */
l4c_status_t l4c_mf_encoder_create(l4c_encoder_backend_t **out_backend) {
    mf_encoder_backend_t *self;
    if (!out_backend) return L4C_ERR_INVALID_ARG;

    self = (mf_encoder_backend_t *)calloc(1, sizeof(mf_encoder_backend_t));
    if (!self) return L4C_ERR_OUT_OF_MEMORY;

    self->base.vtable = &s_mf_vtable;
    self->first_frame = true;

    *out_backend = &self->base;
    return L4C_OK;
}

static void mf_release_event_gen(mf_encoder_backend_t *self) {
    if (self->event_gen) {
        self->event_gen->lpVtbl->Release(self->event_gen);
        self->event_gen = NULL;
    }
}

typedef enum { MFT_WAIT_READY, MFT_WAIT_TIMEOUT, MFT_WAIT_ERROR } mft_wait_result_t;

/* Poll only: GetEvent without NO_WAIT can park forever in a vendor MFT. Store
 * both kinds of events because async MFTs do not promise alternating order. */
static mft_wait_result_t wait_transform_event(mf_encoder_backend_t *self,
                                              MediaEventType want, DWORD timeout_ms) {
    uint64_t start = l4c_clock_monotonic_ms();
    if (!self->event_gen) return MFT_WAIT_READY;
    for (;;) {
        unsigned i;
        if (want == METransformNeedInput && l4c_mft_event_gate_take_input(&self->events))
            return MFT_WAIT_READY;
        if (want == METransformHaveOutput && l4c_mft_event_gate_take_output(&self->events))
            return MFT_WAIT_READY;
        for (i = 0; i < 16; ++i) {
            IMFMediaEvent *ev = NULL;
            MediaEventType got = 0;
            HRESULT event_status = S_OK;
            HRESULT hr = self->event_gen->lpVtbl->GetEvent(self->event_gen, MF_EVENT_FLAG_NO_WAIT, &ev);
            if (hr == MF_E_NO_EVENTS_AVAILABLE) break;
            if (FAILED(hr) || !ev) return MFT_WAIT_ERROR;
            hr = ev->lpVtbl->GetType(ev, &got);
            if (SUCCEEDED(hr)) hr = ev->lpVtbl->GetStatus(ev, &event_status);
            ev->lpVtbl->Release(ev);
            if (FAILED(hr) || FAILED(event_status)) return MFT_WAIT_ERROR;
            if (got == METransformNeedInput)
                l4c_mft_event_gate_record(&self->events, L4C_MFT_EVENT_INPUT);
            else if (got == METransformHaveOutput)
                l4c_mft_event_gate_record(&self->events, L4C_MFT_EVENT_OUTPUT);
            if (want == METransformNeedInput && l4c_mft_event_gate_take_input(&self->events))
                return MFT_WAIT_READY;
            if (want == METransformHaveOutput && l4c_mft_event_gate_take_output(&self->events))
                return MFT_WAIT_READY;
            if (l4c_mft_deadline_expired(start, l4c_clock_monotonic_ms(), timeout_ms)) {
                return MFT_WAIT_TIMEOUT;
            }
        }
        if (l4c_mft_deadline_expired(start, l4c_clock_monotonic_ms(), timeout_ms))
            return MFT_WAIT_TIMEOUT;
        Sleep(1);
    }
}

static void mf_cleanup_session(mf_encoder_backend_t *self) {
    if (!self) return;
    mf_release_event_gen(self);
    if (self->transform) {
        self->transform->lpVtbl->ProcessMessage(self->transform, MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        self->transform->lpVtbl->ProcessMessage(self->transform, MFT_MESSAGE_COMMAND_DRAIN, 0);
    }
    if (self->codec_api) {
        self->codec_api->lpVtbl->Release(self->codec_api);
        self->codec_api = NULL;
    }
    if (self->input_sample) {
        self->input_sample->lpVtbl->Release(self->input_sample);
        self->input_sample = NULL;
    }
    if (self->input_buffer) {
        self->input_buffer->lpVtbl->Release(self->input_buffer);
        self->input_buffer = NULL;
    }
    if (self->output_sample) {
        self->output_sample->lpVtbl->Release(self->output_sample);
        self->output_sample = NULL;
    }
    if (self->output_buffer) {
        self->output_buffer->lpVtbl->Release(self->output_buffer);
        self->output_buffer = NULL;
    }
    if (self->transform) {
        self->transform->lpVtbl->Release(self->transform);
        self->transform = NULL;
    }
    if (self->activate) {
        self->activate->lpVtbl->ShutdownObject(self->activate);
        self->activate->lpVtbl->Release(self->activate);
        self->activate = NULL;
    }
    if (self->au_buffer) {
        free(self->au_buffer);
        self->au_buffer = NULL;
    }
}

static l4c_status_t mf_init(struct l4c_encoder_backend *self_base, const l4c_encoder_config_t *config) {
    mf_encoder_backend_t *self = (mf_encoder_backend_t *)self_base;
    HRESULT hr;
    MFT_REGISTER_TYPE_INFO in_type;
    MFT_REGISTER_TYPE_INFO out_type;
    IMFActivate **ppActivate = NULL;
    UINT32 count = 0;
    DWORD in_size, out_size;
    UINT32 i;

    if (!self || !config) return L4C_ERR_INVALID_ARG;
    if (config->width == 0 || config->height == 0 || config->target_fps == 0) return L4C_ERR_INVALID_ARG;
    if ((uint64_t)config->width * config->height > L4C_MAX_PIXELS_AREA) return L4C_ERR_OVERFLOW;

    self->config = *config;

    if (!init_mf_loader()) return L4C_ERR_DEVICE_LOST;

    hr = g_mf.pfn_MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) return L4C_ERR_DEVICE_LOST;
    self->mf_started = true;

    in_type.guidMajorType = s_MFMediaType_Video;
    in_type.guidSubtype = s_MFVideoFormat_NV12;
    out_type.guidMajorType = s_MFMediaType_Video;
    out_type.guidSubtype = s_MFVideoFormat_H264;

    /* HARDWARE | SORTANDFILTER = 0x4 | 0x40 (см. mfapi.h; 0x100 — не флаг).
     * В mf_init — полный набор: нужен стабильный ppActivate[0]. */
    hr = g_mf.pfn_MFTEnumEx(
        s_MFT_CATEGORY_VIDEO_ENCODER,
        0x00000004 | 0x00000040 /* HARDWARE | SORTANDFILTER */,
        &in_type,
        &out_type,
        &ppActivate,
        &count
    );

    if (FAILED(hr) || count == 0 || !ppActivate) {
        g_mf.pfn_MFShutdown();
        self->mf_started = false;
        return L4C_ERR_DEVICE_LOST;
    }

    /* Prefer the first HW MFT that negotiates NV12→H264 at the target raster.
     * crafted → (re-Activate) → available-type fallback. */
    self->activate = NULL;
    self->transform = NULL;
    for (i = 0; i < count && !self->transform; ++i) {
        int attempt;
        for (attempt = 0; attempt < 2 && !self->transform; ++attempt) {
            IMFTransform *xf = NULL;
            hr = activate_unlocked(ppActivate[i], &xf);
            if (FAILED(hr) || !xf) break;
            if (attempt == 0) {
                hr = negotiate_nv12_h264(xf, config->width, config->height, config->target_fps,
                                         config->target_bitrate_kbps * 1000) ? S_OK : E_FAIL;
            } else {
                hr = negotiate_nv12_h264_fallback(xf, config->width, config->height, config->target_fps,
                                                  config->target_bitrate_kbps * 1000) ? S_OK : E_FAIL;
            }
            if (SUCCEEDED(hr)) {
                self->activate = ppActivate[i];
                self->transform = xf;
                ppActivate[i] = NULL;
                break;
            }
            xf->lpVtbl->Release(xf);
            ppActivate[i]->lpVtbl->ShutdownObject(ppActivate[i]);
        }
    }
    for (i = 0; i < count; ++i) {
        if (ppActivate[i]) ppActivate[i]->lpVtbl->Release(ppActivate[i]);
    }
    CoTaskMemFree(ppActivate);
    if (!self->transform || !self->activate) {
        g_mf.pfn_MFShutdown();
        self->mf_started = false;
        return L4C_ERR_DEVICE_LOST;
    }

    /* Query and configure ICodecAPI */
    hr = self->transform->lpVtbl->QueryInterface(self->transform, &s_IID_ICodecAPI, (void**)&self->codec_api);
    if (SUCCEEDED(hr) && self->codec_api) {
        VARIANT val;
        VariantInit(&val);

        /* Low latency: disable frame buffering/lookahead */
        val.vt = VT_BOOL;
        val.boolVal = VARIANT_TRUE;
        self->codec_api->lpVtbl->SetValue(self->codec_api, &s_CODECAPI_AVEncCommonLowLatency, &val);

        /* No B-frames */
        val.vt = VT_UI4;
        val.ulVal = 0;
        self->codec_api->lpVtbl->SetValue(self->codec_api, &s_CODECAPI_AVEncMPVDefaultBPictureCount, &val);

        /* Rate control: PeakConstrainedVBR — на desktop-тексте QSV/CBR
         * душит IDР и P-кадры (измерено ~700 kbps при target 1500). */
        val.vt = VT_UI4;
        val.ulVal = 1; /* eAVEncCommonRateControlMode_PeakConstrainedVBR */
        self->codec_api->lpVtbl->SetValue(self->codec_api, &s_CODECAPI_AVEncCommonRateControlMode, &val);

        /* Quality vs Speed: 0 = best quality, 100 = fastest. QSV по умолчанию
         * задирает speed — текст/UI-края мылятся. */
        val.vt = VT_UI4;
        val.ulVal = 25;
        self->codec_api->lpVtbl->SetValue(self->codec_api, &s_CODECAPI_AVEncCommonQualityVsSpeed, &val);

        /* Mean bitrate */
        val.vt = VT_UI4;
        val.ulVal = config->target_bitrate_kbps * 1000;
        self->codec_api->lpVtbl->SetValue(self->codec_api, &s_CODECAPI_AVEncCommonMeanBitRate, &val);

        /* Max bitrate */
        val.vt = VT_UI4;
        val.ulVal = config->max_bitrate_kbps * 1000;
        self->codec_api->lpVtbl->SetValue(self->codec_api, &s_CODECAPI_AVEncCommonMaxBitRate, &val);

        /* Optional desktop text experiment. The driver may reject the QP cap;
         * report its HRESULT so a visual A/B result has a known configuration. */
        {
            uint32_t max_qp = mf_quality_max_qp();
            if (max_qp) {
                val.vt = VT_UI4;
                val.ulVal = max_qp;
                hr = self->codec_api->lpVtbl->SetValue(self->codec_api,
                                                       &s_CODECAPI_AVEncVideoMaxQP, &val);
                l4c_logger_write("MF_MAX_QP requested=%u hr=0x%08X",
                                 (unsigned)max_qp, (unsigned)hr);
            }
        }

        /* GOP size: target_fps * 2 (IDR cadence <= 2.0s) */
        val.vt = VT_UI4;
        val.ulVal = config->target_fps * 2;
        self->codec_api->lpVtbl->SetValue(self->codec_api, &s_CODECAPI_AVEncMPVGOPSize, &val);

        /* CAVLC entropy coding: disable CABAC where supported */
        val.vt = VT_BOOL;
        val.boolVal = VARIANT_FALSE;
        self->codec_api->lpVtbl->SetValue(self->codec_api, &s_CODECAPI_AVEncH264CABACEnable, &val);
    }

    /* Async MFT event pump (Intel QSV); optional for sync MFTs. */
    {
        static const GUID IID_IMFMediaEventGenerator_X =
            { 0x2cd2d921, 0xc447, 0x44a7, { 0xa1, 0x3c, 0x4a, 0xda, 0xbf, 0xc2, 0x47, 0xe3 } };
        (void)self->transform->lpVtbl->QueryInterface(
            self->transform, &IID_IMFMediaEventGenerator, (void **)&self->event_gen);
        (void)IID_IMFMediaEventGenerator_X;
    }

    /* Send streaming begin notification */
    self->transform->lpVtbl->ProcessMessage(self->transform, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    self->transform->lpVtbl->ProcessMessage(self->transform, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    /* Get output stream info */
    memset(&self->output_stream_info, 0, sizeof(self->output_stream_info));
    self->transform->lpVtbl->GetOutputStreamInfo(self->transform, 0, &self->output_stream_info);

    /* Preallocate input sample and buffer */
    in_size = config->width * config->height + config->width * ((config->height + 1) / 2);
    hr = g_mf.pfn_MFCreateMemoryBuffer(in_size, &self->input_buffer);
    if (FAILED(hr) || !self->input_buffer) {
        mf_cleanup_session(self);
        g_mf.pfn_MFShutdown();
        self->mf_started = false;
        return L4C_ERR_OUT_OF_MEMORY;
    }
    hr = g_mf.pfn_MFCreateSample(&self->input_sample);
    if (FAILED(hr) || !self->input_sample) {
        mf_cleanup_session(self);
        g_mf.pfn_MFShutdown();
        self->mf_started = false;
        return L4C_ERR_OUT_OF_MEMORY;
    }
    self->input_sample->lpVtbl->AddBuffer(self->input_sample, self->input_buffer);

    /* Preallocate output sample and buffer */
    out_size = self->output_stream_info.cbSize;
    if (out_size == 0 || out_size > L4C_MAX_AU_SIZE) {
        out_size = L4C_MAX_AU_SIZE;
    }
    hr = g_mf.pfn_MFCreateMemoryBuffer(out_size, &self->output_buffer);
    if (FAILED(hr) || !self->output_buffer) {
        mf_cleanup_session(self);
        g_mf.pfn_MFShutdown();
        self->mf_started = false;
        return L4C_ERR_OUT_OF_MEMORY;
    }
    hr = g_mf.pfn_MFCreateSample(&self->output_sample);
    if (FAILED(hr) || !self->output_sample) {
        mf_cleanup_session(self);
        g_mf.pfn_MFShutdown();
        self->mf_started = false;
        return L4C_ERR_OUT_OF_MEMORY;
    }
    self->output_sample->lpVtbl->AddBuffer(self->output_sample, self->output_buffer);

    /* Preallocate AU buffer for zero-allocation in encode() */
    self->au_buffer = (uint8_t *)malloc(L4C_MAX_AU_SIZE);
    if (!self->au_buffer) {
        mf_cleanup_session(self);
        g_mf.pfn_MFShutdown();
        self->mf_started = false;
        return L4C_ERR_OUT_OF_MEMORY;
    }

    self->first_frame = true;
    self->last_idr_ms = 0;
    self->last_force_idr_req_ms = 0;
    self->force_key_frame_pending = false;

    return L4C_OK;
}

static l4c_status_t mf_force_idr(struct l4c_encoder_backend *self_base) {
    mf_encoder_backend_t *self = (mf_encoder_backend_t *)self_base;
    uint64_t now_ms;
    if (!self) return L4C_ERR_INVALID_ARG;

    now_ms = l4c_clock_monotonic_ms();
    if (!self->first_frame && (now_ms - self->last_idr_ms < 500)) {
        /* Coalesced: request within 500 ms ignored */
        return L4C_OK;
    }

    self->force_key_frame_pending = true;
    if (self->codec_api) {
        VARIANT val;
        VariantInit(&val);
        val.vt = VT_UI4;
        val.ulVal = 1;
        self->codec_api->lpVtbl->SetValue(self->codec_api, &s_CODECAPI_AVEncVideoForceKeyFrame, &val);
    }
    return L4C_OK;
}

static l4c_status_t mf_encode(
    struct l4c_encoder_backend *self_base,
    const l4c_raw_frame_t *raw,
    l4c_access_unit_t *out_au
) {
    mf_encoder_backend_t *self = (mf_encoder_backend_t *)self_base;
    BYTE *pInputData = NULL;
    DWORD max_in_len = 0, cur_in_len = 0;
    HRESULT hr;
    LONGLONG sample_time, sample_dur;
    uint32_t r, w, h, half_h;
    uint8_t *pDstUV;
    MFT_OUTPUT_DATA_BUFFER out_data;
    DWORD mft_status = 0;
    IMFMediaBuffer *pOutMediaBuffer = NULL;
    BYTE *pOutData = NULL;
    DWORD max_out_len = 0, cur_out_len = 0;
    uint64_t now_ms;
    bool is_idr = false;
    uint32_t nal_count = 0;
    size_t au_offset = 0;

    if (!self || !raw || !out_au) return L4C_ERR_INVALID_ARG;
    if (s_test_injected_process_fail) return L4C_ERR_DEVICE_LOST;
    if (!self->transform || !self->input_sample || !self->input_buffer) return L4C_ERR_DEVICE_LOST;

    w = self->config.width;
    h = self->config.height;
    half_h = (h + 1u) / 2u;

    now_ms = l4c_clock_monotonic_ms();

    /* Check force IDR coalescing */
    if (raw->force_idr) {
        mf_force_idr(self_base);
    }

    /* Periodic IDR cadence watchdog: guarantee IDR not rarer than once per 2.0s */
    if (!self->first_frame && (now_ms - self->last_idr_ms >= 2000)) {
        if (self->codec_api) {
            VARIANT val;
            VariantInit(&val);
            val.vt = VT_UI4;
            val.ulVal = 1;
            self->codec_api->lpVtbl->SetValue(self->codec_api, &s_CODECAPI_AVEncVideoForceKeyFrame, &val);
        }
    }

    /* A timed-out output may still be in flight. Keep its sample alive and
     * discard this fresh raw frame until the old output arrives or fails. */
    if (self->input_pending) goto wait_output;

    if (self->event_gen) {
        mft_wait_result_t wait = wait_transform_event(self, METransformNeedInput, 100);
        if (wait == MFT_WAIT_ERROR) return L4C_ERR_DEVICE_LOST;
        if (wait == MFT_WAIT_TIMEOUT) {
            uint64_t now = l4c_clock_monotonic_ms();
            if (!self->input_wait_since_ms) self->input_wait_since_ms = now;
            if (now - self->input_wait_since_ms >= 500) return L4C_ERR_DEVICE_LOST;
            return L4C_ERR_NO_FRAME;
        }
        self->input_wait_since_ms = 0;
    }

    /* A vendor MFT may retain the submitted sample beyond ProcessInput.
     * Allocate a fresh one before writing the next frame; COM refcounts keep
     * any retained previous sample alive inside the transform. */
    if (self->input_submitted) {
        DWORD in_size = w * h + w * half_h;
        self->input_sample->lpVtbl->Release(self->input_sample);
        self->input_buffer->lpVtbl->Release(self->input_buffer);
        self->input_sample = NULL;
        self->input_buffer = NULL;
        hr = g_mf.pfn_MFCreateMemoryBuffer(in_size, &self->input_buffer);
        if (FAILED(hr) || !self->input_buffer) return L4C_ERR_OUT_OF_MEMORY;
        hr = g_mf.pfn_MFCreateSample(&self->input_sample);
        if (FAILED(hr) || !self->input_sample) return L4C_ERR_OUT_OF_MEMORY;
        hr = self->input_sample->lpVtbl->AddBuffer(self->input_sample, self->input_buffer);
        if (FAILED(hr)) return L4C_ERR_DEVICE_LOST;
        self->input_submitted = false;
    }

    /* Fill input buffer: Y plane then UV interleaved plane */
    hr = self->input_buffer->lpVtbl->Lock(self->input_buffer, &pInputData, &max_in_len, &cur_in_len);
    if (FAILED(hr) || !pInputData) return L4C_ERR_DEVICE_LOST;

    for (r = 0; r < h; ++r) {
        memcpy(pInputData + (size_t)r * w, raw->planes[0] + (size_t)r * raw->strides[0], w);
    }

    pDstUV = pInputData + (size_t)w * h;
    for (r = 0; r < half_h; ++r) {
        memcpy(pDstUV + (size_t)r * w, raw->planes[1] + (size_t)r * raw->strides[1], w);
    }

    self->input_buffer->lpVtbl->Unlock(self->input_buffer);
    self->input_buffer->lpVtbl->SetCurrentLength(self->input_buffer, w * h + w * half_h);

    sample_time = (LONGLONG)raw->pts_ms * 10000; /* 100ns units */
    sample_dur = (LONGLONG)(1000 / self->config.target_fps) * 10000;
    self->input_sample->lpVtbl->SetSampleTime(self->input_sample, sample_time);
    self->input_sample->lpVtbl->SetSampleDuration(self->input_sample, sample_dur);

    hr = self->transform->lpVtbl->ProcessInput(self->transform, 0, self->input_sample, 0);
    if (hr == MF_E_NOTACCEPTING) return L4C_ERR_NO_FRAME;
    if (FAILED(hr)) {
        return L4C_ERR_DEVICE_LOST;
    }
    self->input_submitted = true;
    self->input_pending = true;
    self->pending_pts_ms = raw->pts_ms;
    self->pending_since_ms = l4c_clock_monotonic_ms();

wait_output:
    if (self->event_gen) {
        mft_wait_result_t wait = wait_transform_event(self, METransformHaveOutput, 100);
        if (wait == MFT_WAIT_ERROR) return L4C_ERR_DEVICE_LOST;
        if (wait == MFT_WAIT_TIMEOUT) {
            if (l4c_clock_monotonic_ms() - self->pending_since_ms >= 500)
                return L4C_ERR_DEVICE_LOST;
            return L4C_ERR_NO_FRAME;
        }
    }

    /* ProcessOutput */
    memset(&out_data, 0, sizeof(out_data));
    out_data.dwStreamID = 0;
    if (!(self->output_stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
        out_data.pSample = self->output_sample;
    }

    hr = self->transform->lpVtbl->ProcessOutput(self->transform, 0, 1, &out_data, &mft_status);
    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
        self->input_pending = false;
        return L4C_ERR_NO_FRAME;
    }
    if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
        UINT32 ti;
        HRESULT st = E_FAIL;
        for (ti = 0; ti < 8; ++ti) {
            IMFMediaType *pNewType = NULL;
            if (FAILED(self->transform->lpVtbl->GetOutputAvailableType(self->transform, 0, ti, &pNewType)) || !pNewType) break;
            st = self->transform->lpVtbl->SetOutputType(self->transform, 0, pNewType, 0);
            pNewType->lpVtbl->Release(pNewType);
            if (SUCCEEDED(st)) break;
        }
        /* После смены типа MFT требует новый output sample/buffer. */
        memset(&self->output_stream_info, 0, sizeof(self->output_stream_info));
        self->transform->lpVtbl->GetOutputStreamInfo(self->transform, 0, &self->output_stream_info);
        if (self->output_sample) { self->output_sample->lpVtbl->Release(self->output_sample); self->output_sample = NULL; }
        if (self->output_buffer) { self->output_buffer->lpVtbl->Release(self->output_buffer); self->output_buffer = NULL; }
        {
            DWORD osz = self->output_stream_info.cbSize;
            if (osz == 0 || osz > L4C_MAX_AU_SIZE) osz = L4C_MAX_AU_SIZE;
            hr = g_mf.pfn_MFCreateMemoryBuffer(osz, &self->output_buffer);
            if (SUCCEEDED(hr)) hr = g_mf.pfn_MFCreateSample(&self->output_sample);
            if (SUCCEEDED(hr) && self->output_sample && self->output_buffer) {
                self->output_sample->lpVtbl->AddBuffer(self->output_sample, self->output_buffer);
            }
        }
        if (self->event_gen && wait_transform_event(self, METransformHaveOutput, 100) == MFT_WAIT_ERROR)
            return L4C_ERR_DEVICE_LOST;
        memset(&out_data, 0, sizeof(out_data));
        out_data.dwStreamID = 0;
        if (!(self->output_stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
            out_data.pSample = self->output_sample;
        }
        hr = self->transform->lpVtbl->ProcessOutput(self->transform, 0, 1, &out_data, &mft_status);
    }
    if (FAILED(hr)) {
        if (out_data.pEvents) out_data.pEvents->lpVtbl->Release(out_data.pEvents);
        return L4C_ERR_DEVICE_LOST;
    }
    self->input_pending = false;

    if (out_data.pEvents) {
        out_data.pEvents->lpVtbl->Release(out_data.pEvents);
        out_data.pEvents = NULL;
    }

    /* Retrieve media buffer from output sample */
    if (out_data.pSample) {
        hr = out_data.pSample->lpVtbl->ConvertToContiguousBuffer(out_data.pSample, &pOutMediaBuffer);
        if (FAILED(hr) || !pOutMediaBuffer) {
            hr = out_data.pSample->lpVtbl->GetBufferByIndex(out_data.pSample, 0, &pOutMediaBuffer);
        }
    } else if (self->output_sample) {
        hr = self->output_sample->lpVtbl->ConvertToContiguousBuffer(self->output_sample, &pOutMediaBuffer);
        if (FAILED(hr) || !pOutMediaBuffer) {
            hr = self->output_sample->lpVtbl->GetBufferByIndex(self->output_sample, 0, &pOutMediaBuffer);
        }
    }

    if (FAILED(hr) || !pOutMediaBuffer) {
        if (out_data.pSample && (self->output_stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
            out_data.pSample->lpVtbl->Release(out_data.pSample);
        }
        return L4C_ERR_DEVICE_LOST;
    }

    hr = pOutMediaBuffer->lpVtbl->Lock(pOutMediaBuffer, &pOutData, &max_out_len, &cur_out_len);
    if (FAILED(hr) || !pOutData || cur_out_len == 0) {
        pOutMediaBuffer->lpVtbl->Release(pOutMediaBuffer);
        if (out_data.pSample && (self->output_stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
            out_data.pSample->lpVtbl->Release(out_data.pSample);
        }
        return L4C_ERR_NO_FRAME;
    }

    /*
     * NAL Parsing and Normalization:
     * Strip Annex B start codes (00 00 01 or 00 00 00 01) or AVCC length prefixes.
     */
    {
        const uint8_t *buf = pOutData;
        size_t len = cur_out_len;
        size_t pos = 0;
        bool is_avcc = false;

        /* Check if AVCC length-prefixed */
        if (len >= 4 && !(buf[0] == 0 && buf[1] == 0 && (buf[2] == 1 || (buf[2] == 0 && buf[3] == 1)))) {
            uint32_t nal_len = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
            if (nal_len > 0 && nal_len <= len - 4 && (buf[4] & 0x80) == 0 && (buf[4] & 0x1F) > 0) {
                is_avcc = true;
            }
        }

        if (is_avcc) {
            while (pos + 4 <= len && nal_count < 64) {
                uint32_t nlen = ((uint32_t)buf[pos] << 24) | ((uint32_t)buf[pos + 1] << 16) | ((uint32_t)buf[pos + 2] << 8) | buf[pos + 3];
                const uint8_t *ndata;
                uint8_t ntype;
                pos += 4;
                if (pos + nlen > len) break;
                ndata = buf + pos;
                ntype = ndata[0] & 0x1F;

                if (ntype == 7) {
                    if (nlen <= sizeof(self->cached_sps)) {
                        memcpy(self->cached_sps, ndata, nlen);
                        self->cached_sps_len = nlen;
                    }
                    check_sps_cropping(ndata, nlen, self->config.width, self->config.height);
                } else if (ntype == 8) {
                    if (nlen <= sizeof(self->cached_pps)) {
                        memcpy(self->cached_pps, ndata, nlen);
                        self->cached_pps_len = nlen;
                    }
                } else if (ntype == 5) {
                    is_idr = true;
                }

                self->nals[nal_count].data = ndata;
                self->nals[nal_count].length = nlen;
                self->nals[nal_count].nal_type = ntype;
                nal_count++;
                pos += nlen;
            }
        } else {
            /* Annex B start codes parsing */
            while (pos < len && nal_count < 64) {
                size_t start, end;
                const uint8_t *ndata;
                uint32_t nlen;
                uint8_t ntype;

                while (pos + 2 < len && !(buf[pos] == 0 && buf[pos + 1] == 0 && (buf[pos + 2] == 1 || (pos + 3 < len && buf[pos + 2] == 0 && buf[pos + 3] == 1)))) {
                    pos++;
                }
                if (pos >= len) break;

                if (pos + 3 < len && buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 0 && buf[pos + 3] == 1) {
                    pos += 4;
                } else if (pos + 2 < len && buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 1) {
                    pos += 3;
                } else {
                    break;
                }
                start = pos;

                end = start;
                while (end + 2 < len) {
                    if (buf[end] == 0 && buf[end + 1] == 0 && (buf[end + 2] == 1 || (end + 3 < len && buf[end + 2] == 0 && buf[end + 3] == 1))) {
                        break;
                    }
                    end++;
                }
                if (end + 2 >= len) end = len;

                while (end > start && buf[end - 1] == 0) end--;

                if (end <= start) {
                    pos = end;
                    continue;
                }

                ndata = buf + start;
                nlen = (uint32_t)(end - start);
                ntype = ndata[0] & 0x1F;

                if (ntype == 7) {
                    if (nlen <= sizeof(self->cached_sps)) {
                        memcpy(self->cached_sps, ndata, nlen);
                        self->cached_sps_len = nlen;
                    }
                    check_sps_cropping(ndata, nlen, self->config.width, self->config.height);
                } else if (ntype == 8) {
                    if (nlen <= sizeof(self->cached_pps)) {
                        memcpy(self->cached_pps, ndata, nlen);
                        self->cached_pps_len = nlen;
                    }
                } else if (ntype == 5) {
                    is_idr = true;
                }

                self->nals[nal_count].data = ndata;
                self->nals[nal_count].length = nlen;
                self->nals[nal_count].nal_type = ntype;
                nal_count++;
                pos = end;
            }
        }
    }

    if (self->first_frame) {
        is_idr = true;
    }

    /*
     * Build output Access Unit:
     * Prepend cached SPS/PPS before IDR if driver didn't emit them in this AU.
     */
    au_offset = 0;
    out_au->nals = self->out_nals;
    out_au->nal_count = 0;
    out_au->total_bytes = 0;

    if (is_idr) {
        bool has_sps = false, has_pps = false;
        uint32_t i;
        for (i = 0; i < nal_count; ++i) {
            if (self->nals[i].nal_type == 7) has_sps = true;
            if (self->nals[i].nal_type == 8) has_pps = true;
        }

        if (!has_sps && self->cached_sps_len > 0) {
            if (au_offset + self->cached_sps_len <= L4C_MAX_AU_SIZE) {
                memcpy(self->au_buffer + au_offset, self->cached_sps, self->cached_sps_len);
                out_au->nals[out_au->nal_count].data = self->au_buffer + au_offset;
                out_au->nals[out_au->nal_count].length = self->cached_sps_len;
                out_au->nals[out_au->nal_count].nal_type = 7;
                out_au->nal_count++;
                au_offset += self->cached_sps_len;
            }
        }
        if (!has_pps && self->cached_pps_len > 0) {
            if (au_offset + self->cached_pps_len <= L4C_MAX_AU_SIZE) {
                memcpy(self->au_buffer + au_offset, self->cached_pps, self->cached_pps_len);
                out_au->nals[out_au->nal_count].data = self->au_buffer + au_offset;
                out_au->nals[out_au->nal_count].length = self->cached_pps_len;
                out_au->nals[out_au->nal_count].nal_type = 8;
                out_au->nal_count++;
                au_offset += self->cached_pps_len;
            }
        }
    }

    /* Copy existing NALs to self->au_buffer for zero-allocation memory stability */
    {
        uint32_t i;
        for (i = 0; i < nal_count && out_au->nal_count < 64; ++i) {
            uint32_t nlen = self->nals[i].length;
            if (au_offset + nlen <= L4C_MAX_AU_SIZE) {
                memcpy(self->au_buffer + au_offset, self->nals[i].data, nlen);
                out_au->nals[out_au->nal_count].data = self->au_buffer + au_offset;
                out_au->nals[out_au->nal_count].length = nlen;
                out_au->nals[out_au->nal_count].nal_type = self->nals[i].nal_type;
                out_au->nal_count++;
                au_offset += nlen;
            }
        }
    }

    pOutMediaBuffer->lpVtbl->Unlock(pOutMediaBuffer);
    pOutMediaBuffer->lpVtbl->Release(pOutMediaBuffer);

    if (out_data.pSample && (self->output_stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
        out_data.pSample->lpVtbl->Release(out_data.pSample);
    }

    out_au->pts_ms = self->pending_pts_ms;
    out_au->is_idr = is_idr;
    out_au->total_bytes = au_offset;

    if (is_idr) {
        self->last_idr_ms = l4c_clock_monotonic_ms();
        self->first_frame = false;
        self->force_key_frame_pending = false;
    }

    return L4C_OK;
}

static void mf_release_au(struct l4c_encoder_backend *self_base, l4c_access_unit_t *au) {
    (void)self_base;
    if (au) {
        au->nal_count = 0;
    }
}

static void mf_destroy(struct l4c_encoder_backend *self_base) {
    mf_encoder_backend_t *self = (mf_encoder_backend_t *)self_base;
    if (!self) return;

    mf_cleanup_session(self);

    if (g_mf.pfn_MFShutdown && self->mf_started) {
        g_mf.pfn_MFShutdown();
        self->mf_started = false;
    }

    free(self);
}

static bool probe_encoded_sequence(void) {
    l4c_encoder_backend_t *backend = NULL;
    l4c_encoder_config_t cfg;
    l4c_raw_frame_t raw;
    l4c_access_unit_t au;
    uint8_t *nv12;
    unsigned frame;
    unsigned outputs = 0;
    bool ok = false;
    const uint32_t width = 640, height = 480;
    nv12 = (uint8_t *)malloc(width * height * 3u / 2u);
    if (!nv12) return false;
    memset(nv12, 128, width * height * 3u / 2u);
    memset(&cfg, 0, sizeof(cfg));
    cfg.width = width;
    cfg.height = height;
    cfg.target_fps = 10;
    cfg.target_bitrate_kbps = 500;
    cfg.max_bitrate_kbps = 800;
    cfg.input_format = L4C_PIX_FMT_NV12;
    if (l4c_mf_encoder_create(&backend) != L4C_OK) goto done;
    if (backend->vtable->init(backend, &cfg) != L4C_OK) goto done;
    memset(&raw, 0, sizeof(raw));
    raw.planes[0] = nv12;
    raw.planes[1] = nv12 + width * height;
    raw.strides[0] = width;
    raw.strides[1] = width;
    raw.plane_sizes[0] = width * height;
    raw.plane_sizes[1] = width * height / 2;
    raw.width = width;
    raw.height = height;
    raw.format = L4C_PIX_FMT_NV12;
    for (frame = 0; frame < 12; ++frame) {
        l4c_status_t status;
        nv12[(frame * 7919u) % (width * height)] = (uint8_t)(frame * 17u);
        raw.pts_ms = frame * 100u;
        raw.force_idr = frame == 0;
        memset(&au, 0, sizeof(au));
        status = backend->vtable->encode(backend, &raw, &au);
        if (status != L4C_OK && status != L4C_ERR_NO_FRAME) goto done;
        if (status == L4C_OK && au.nal_count > 0) ++outputs;
        backend->vtable->release_au(backend, &au);
        if (outputs >= 3) { ok = true; break; }
    }
done:
    if (backend) backend->vtable->destroy(backend);
    free(nv12);
    return ok;
}
