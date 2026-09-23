#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "l4capture/clock.h"
#include "l4capture/safety_gate.h"
#include "l4capture/ipc_pipe.h"
#include "l4capture/gdi_capture.h"
#include "l4capture/dxgi_capture.h"
#include "l4capture/openh264_encoder.h"
#include "l4capture/mf_encoder.h"
#include "l4capture/rtp_sender.h"
#include "l4capture/scale.h"
#include "l4capture/color_convert.h"
#include "l4capture/types.h"
#include "l4capture/video_profile.h"
#include "l4capture/degrade_controller.h"
#include "l4capture/telemetry.h"

typedef struct {
    HANDLE pipe_in;
    HANDLE pipe_out;
    bool valid;
} l4c_args_t;

typedef struct {
    l4c_capture_backend_t *capture;
    l4c_encoder_backend_t *encoder;
    l4c_rtp_sender_t *rtp;
    l4c_color_converter_t *converter;
    uint8_t *scaled_buf;
    bool active;
    uint64_t last_metrics_ms;
    uint32_t raw_drops;
    uint32_t encoder_drops;
    uint32_t transport_drops;
    uint32_t frames_captured;
    uint32_t frames_encoded;
    uint32_t frames_sent;
    uint32_t frames_skipped;
    uint16_t active_capture_backend;
    uint16_t active_encoder_backend;
    uint16_t fallback_reason;
    l4c_capture_config_t cap_cfg;
    /* L4C-09 profiles / degrade */
    uint8_t requested_profile;
    uint16_t actual_profile;
    uint8_t current_fps;
    uint32_t target_width, target_height;
    uint16_t bitrate_min, bitrate_target, bitrate_max;
    l4c_degrade_controller_t degrade;
    uint32_t config_gen;
    uint16_t degrade_state;
    uint64_t win_start_ms;
    l4c_degrade_window_t win_acc;
    bool force_next_idr;
    uint64_t pts_base_ms;
    uint32_t frames_since_pts_base;
    uint32_t au_bytes_10s;
    uint64_t bitrate_win_start_ms;
    bool pending_degrade_action;
    l4c_degrade_action_t next_action;
} l4c_pipeline_state_t;

static void profile_bounds(const l4c_pipeline_state_t *ps, uint32_t *w, uint32_t *h, uint8_t *fps) {
    if (w) *w = ps->target_width;
    if (h) *h = ps->target_height;
    if (fps) *fps = ps->current_fps;
}

/* Local-only diagnostics (§8/§4.4): лог ступеней рядом с exe. Не wire. */
static void local_log(const char *line) {
    wchar_t path[MAX_PATH];
    FILE *f;
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) return;
    {
        wchar_t *slash = wcsrchr(path, L'\\');
        if (slash) wcscpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - path), L"l4capture_degrade.log");
        else wcscpy_s(path, MAX_PATH, L"l4capture_degrade.log");
    }
    f = _wfopen(path, L"a");
    if (!f) return;
    fprintf(f, "%s\n", line);
    fclose(f);
}

/* Fault-injection harness (§6.1): delay encode if test_encode_delay_ms.txt sits next to exe. */
static uint32_t read_test_encode_delay_ms(void) {
    wchar_t path[MAX_PATH];
    FILE *f;
    unsigned value = 0;
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) return 0;
    {
        wchar_t *slash = wcsrchr(path, L'\\');
        if (slash) wcscpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - path), L"test_encode_delay_ms.txt");
        else return 0;
    }
    f = _wfopen(path, L"r");
    if (!f) return 0;
    if (fscanf_s(f, "%u", &value) != 1) value = 0;
    fclose(f);
    return value > 5000u ? 5000u : value;
}

static bool is_single_output(const l4c_rect_t *target_rect) {
    if (!target_rect) return false;
    if (target_rect->left == 0 && target_rect->top == 0 &&
        target_rect->right == 0 && target_rect->bottom == 0) {
        return (GetSystemMetrics(SM_CMONITORS) == 1);
    }
    RECT rc;
    rc.left = target_rect->left;
    rc.top = target_rect->top;
    rc.right = target_rect->right;
    rc.bottom = target_rect->bottom;
    HMONITOR hmon = MonitorFromRect(&rc, MONITOR_DEFAULTTONULL);
    if (!hmon) return false;
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(hmon, &mi)) return false;
    return (rc.left >= mi.rcMonitor.left && rc.top >= mi.rcMonitor.top &&
            rc.right <= mi.rcMonitor.right && rc.bottom <= mi.rcMonitor.bottom);
}

static bool os_is_win7_or_older(void) {
    typedef LONG (WINAPI *rtl_get_version_fn)(void *);
    HMODULE ntdll;
    rtl_get_version_fn rtl_get_version;
    struct {
        ULONG os_version_info_size;
        ULONG major_version;
        ULONG minor_version;
        ULONG build_number;
        ULONG platform_id;
        WCHAR service_pack[128];
    } vi;
    ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return true;
    rtl_get_version = (rtl_get_version_fn)(void *)GetProcAddress(ntdll, "RtlGetVersion");
    if (!rtl_get_version) return true;
    memset(&vi, 0, sizeof(vi));
    vi.os_version_info_size = (ULONG)sizeof(vi);
    if (rtl_get_version(&vi) != 0) return true;
    return (vi.major_version < 6u) || (vi.major_version == 6u && vi.minor_version < 2u);
}

static l4c_args_t parse_args(int argc, char *argv[]) {
    l4c_args_t args;
    int i;
    memset(&args, 0, sizeof(args));
    for (i = 1; i < argc; ++i) {
        const char *p = argv[i];
        if (strncmp(p, "--pipe-in=", 10) == 0) {
            if (l4c_parse_handle(p + 10, &args.pipe_in) != L4C_OK) return args;
        } else if (strncmp(p, "--pipe-out=", 11) == 0) {
            if (l4c_parse_handle(p + 11, &args.pipe_out) != L4C_OK) return args;
        } else {
            return args;
        }
    }
    if (args.pipe_in && args.pipe_out) args.valid = true;
    return args;
}

static void send_event_ready(l4c_ipc_pipe_t *pipe, uint64_t now, uint16_t capture_backend, uint16_t encoder_backend,
                             uint32_t actual_width, uint32_t actual_height, uint16_t actual_fps) {
    l4c_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = L4C_EVENT_READY;
    msg.request_seq = 0;
    msg.body.ready.actual_width = actual_width;
    msg.body.ready.actual_height = actual_height;
    msg.body.ready.actual_fps = actual_fps;
    msg.body.ready.capture_backend = capture_backend;
    msg.body.ready.encoder_backend = encoder_backend;
    l4c_pipe_enqueue(pipe, &msg, now);
}

static void send_event_metrics(l4c_ipc_pipe_t *pipe, const l4c_pipeline_state_t *ps, uint64_t now) {
    l4c_message_t msg;
    l4c_rtp_stats_t rtp_stats;
    memset(&msg, 0, sizeof(msg));
    msg.type = L4C_EVENT_METRICS;
    msg.body.metrics.fps = ps->current_fps;
    msg.body.metrics.bitrate_kbps = ps->bitrate_target;
    msg.body.metrics.raw_drops = ps->raw_drops;
    msg.body.metrics.encoder_drops = ps->encoder_drops;
    msg.body.metrics.transport_drops = ps->transport_drops;
    msg.body.metrics.encode_p95_ms = 0;
    msg.body.metrics.queue_depth = (uint16_t)(ps->frames_sent & 0xFFFF);
    msg.body.metrics.private_bytes_kb = ps->frames_captured;
    msg.body.metrics.gdi_handles = ps->frames_encoded;
    if (ps->rtp) {
        l4c_rtp_sender_get_stats(ps->rtp, &rtp_stats);
        msg.body.metrics.transport_drops = rtp_stats.transport_drops;
    }
    l4c_pipe_enqueue(pipe, &msg, now);
}

static void pipeline_cleanup(l4c_pipeline_state_t *ps) {
    if (ps->rtp) { l4c_rtp_sender_destroy(ps->rtp); ps->rtp = NULL; }
    if (ps->encoder) { ps->encoder->vtable->destroy(ps->encoder); ps->encoder = NULL; }
    if (ps->capture) { ps->capture->vtable->destroy(ps->capture); ps->capture = NULL; }
    if (ps->converter) { l4c_color_converter_destroy(ps->converter); ps->converter = NULL; }
    if (ps->scaled_buf) { free(ps->scaled_buf); ps->scaled_buf = NULL; }
    ps->active = false;
}

static void accumulate_window_sample(l4c_pipeline_state_t *ps, uint64_t now) {
    if (ps->win_start_ms == 0) {
        ps->win_start_ms = now;
        memset(&ps->win_acc, 0, sizeof(ps->win_acc));
    }
}

static void count_raw_pass(l4c_pipeline_state_t *ps) { ps->win_acc.raw.passed++; }
static void count_raw_drop(l4c_pipeline_state_t *ps) { ps->win_acc.raw.dropped++; ps->raw_drops++; }
static void count_enc_pass(l4c_pipeline_state_t *ps) { ps->win_acc.encoder.passed++; }
static void count_enc_drop(l4c_pipeline_state_t *ps) { ps->win_acc.encoder.dropped++; ps->encoder_drops++; }
static void count_tr_pass(l4c_pipeline_state_t *ps) { ps->win_acc.transport.passed++; }
static void count_tr_drop(l4c_pipeline_state_t *ps) { ps->win_acc.transport.dropped++; ps->transport_drops++; }

/* Транзакция смены растра D2/D3: очистка pending, reinit, первый AU — SPS/PPS+IDR. */
static l4c_status_t pipeline_reconfigure_raster(l4c_pipeline_state_t *ps, uint16_t new_profile, uint64_t now) {
    const l4c_profile_params_t *pp = l4c_profile_params(new_profile);
    l4c_encoder_config_t enc_cfg;
    l4c_status_t status;
    uint8_t *new_buf;
    l4c_color_converter_t *new_conv = NULL;

    if (!pp) return L4C_ERR_INVALID_ARG;
    /* Бюджет до allocations */
    if (pp->width == 0 || pp->height == 0) return L4C_ERR_OVERFLOW;

    new_buf = (uint8_t *)malloc((size_t)pp->width * (size_t)pp->height * 4u);
    if (!new_buf) return L4C_ERR_OUT_OF_MEMORY;
    status = l4c_color_converter_create(pp->width, pp->height, &new_conv);
    if (status != L4C_OK) { free(new_buf); return status; }

    if (ps->encoder) {
        ps->encoder->vtable->destroy(ps->encoder);
        ps->encoder = NULL;
    }
    if (ps->converter) l4c_color_converter_destroy(ps->converter);
    if (ps->scaled_buf) free(ps->scaled_buf);
    ps->converter = new_conv;
    ps->scaled_buf = new_buf;
    ps->target_width = pp->width;
    ps->target_height = pp->height;
    ps->bitrate_target = pp->bitrate_target_kbps;
    if (ps->bitrate_min < pp->bitrate_min_kbps) ps->bitrate_min = pp->bitrate_min_kbps;
    if (ps->bitrate_target < ps->bitrate_min) ps->bitrate_target = ps->bitrate_min;
    ps->actual_profile = new_profile;
    ps->force_next_idr = true;
    ps->config_gen++;

    /* Сохраняем backend; MFT отказ — существующий bounded fallback L4C-08 */
    if (ps->active_encoder_backend == L4C_ENCODER_MF_HARDWARE) {
        status = l4c_mf_encoder_create(&ps->encoder);
        if (status == L4C_OK) {
            memset(&enc_cfg, 0, sizeof(enc_cfg));
            enc_cfg.width = ps->target_width;
            enc_cfg.height = ps->target_height;
            enc_cfg.target_fps = ps->current_fps;
            enc_cfg.target_bitrate_kbps = ps->bitrate_target;
            enc_cfg.max_bitrate_kbps = ps->bitrate_max;
            enc_cfg.input_format = L4C_PIX_FMT_NV12;
            status = ps->encoder->vtable->init(ps->encoder, &enc_cfg);
            if (status != L4C_OK) {
                ps->encoder->vtable->destroy(ps->encoder);
                ps->encoder = NULL;
            }
        }
        if (!ps->encoder) {
            status = l4c_openh264_encoder_create(&ps->encoder);
            if (status != L4C_OK) return status;
            ps->active_encoder_backend = L4C_ENCODER_OPENH264_SOFTWARE;
            ps->fallback_reason = L4C_MFT_UNAVAILABLE;
        }
    }
    if (!ps->encoder) {
        status = l4c_openh264_encoder_create(&ps->encoder);
        if (status != L4C_OK) return status;
        ps->active_encoder_backend = L4C_ENCODER_OPENH264_SOFTWARE;
    }
    if (ps->active_encoder_backend == L4C_ENCODER_OPENH264_SOFTWARE) {
        memset(&enc_cfg, 0, sizeof(enc_cfg));
        enc_cfg.width = ps->target_width;
        enc_cfg.height = ps->target_height;
        enc_cfg.target_fps = ps->current_fps;
        enc_cfg.target_bitrate_kbps = ps->bitrate_target;
        enc_cfg.max_bitrate_kbps = ps->bitrate_max;
        enc_cfg.input_format = L4C_PIX_FMT_I420;
        status = ps->encoder->vtable->init(ps->encoder, &enc_cfg);
        if (status != L4C_OK) return status;
    } else if (ps->active_encoder_backend == L4C_ENCODER_MF_HARDWARE) {
        memset(&enc_cfg, 0, sizeof(enc_cfg));
        enc_cfg.width = ps->target_width;
        enc_cfg.height = ps->target_height;
        enc_cfg.target_fps = ps->current_fps;
        enc_cfg.target_bitrate_kbps = ps->bitrate_target;
        enc_cfg.max_bitrate_kbps = ps->bitrate_max;
        enc_cfg.input_format = L4C_PIX_FMT_NV12;
        status = ps->encoder->vtable->init(ps->encoder, &enc_cfg);
        if (status != L4C_OK) return status;
    }
    /* Очистка pending raw/AU: drain запрещён. */
    memset(&ps->win_acc, 0, sizeof(ps->win_acc));
    ps->win_start_ms = now;
    l4c_degrade_notify_applied(&ps->degrade, ps->actual_profile, ps->current_fps,
                               ps->degrade_state, ps->config_gen, now);
    return L4C_OK;
}

static void apply_degrade_action(l4c_pipeline_state_t *ps, l4c_ipc_pipe_t *pipe, l4c_safety_gate_t *gate,
                                 l4c_degrade_action_t act, uint64_t now) {
    l4c_message_t deg;
    char logline[160];
    switch (act) {
    case L4C_DEG_ACT_D0_DROP_LATE_RAW:
        /* Однократный сброс просроченных pending raw; latest-frame уже действует. */
        break;
    case L4C_DEG_ACT_D1_FPS_THROTTLE:
        ps->current_fps = 10;
        ps->degrade_state = L4C_FPS_THROTTLED;
        l4c_degrade_notify_applied(&ps->degrade, ps->actual_profile, ps->current_fps,
                                   ps->degrade_state, ps->config_gen, now);
        break;
    case L4C_DEG_ACT_D2_RASTER_540P:
        ps->degrade_state = L4C_DEGRADED_540P;
        if (pipeline_reconfigure_raster(ps, L4C_PROFILE_540P, now) != L4C_OK) {
            ps->fallback_reason = L4C_HIGH_LOAD;
            l4c_safety_stop(gate, L4C_ERR_FATAL);
            return;
        }
        break;
    case L4C_DEG_ACT_D3_RASTER_480P:
        ps->degrade_state = L4C_DEGRADED_480P;
        if (pipeline_reconfigure_raster(ps, L4C_PROFILE_480P, now) != L4C_OK) {
            ps->fallback_reason = L4C_HIGH_LOAD;
            l4c_safety_stop(gate, L4C_ERR_FATAL);
            return;
        }
        break;
    case L4C_DEG_ACT_STOP_HIGH_LOAD:
        ps->fallback_reason = L4C_HIGH_LOAD;
        memset(&deg, 0, sizeof(deg));
        deg.type = L4C_EVENT_DEGRADED;
        deg.body.degraded.degrade_state = ps->degrade_state;
        deg.body.degraded.reason = L4C_HIGH_LOAD;
        l4c_pipe_enqueue(pipe, &deg, now);
        _snprintf_s(logline, sizeof(logline), _TRUNCATE,
                    "ACT STOP_HIGH_LOAD t=%llu actual=%u fps=%u state=%u gen=%u",
                    (unsigned long long)now, ps->actual_profile, ps->current_fps,
                    ps->degrade_state, ps->config_gen);
        local_log(logline);
        l4c_safety_stop(gate, L4C_ERR_FATAL);
        return;
    default:
        break;
    }
    _snprintf_s(logline, sizeof(logline), _TRUNCATE,
                "ACT %u t=%llu actual=%u fps=%u state=%u gen=%u",
                (unsigned)act, (unsigned long long)now, ps->actual_profile,
                ps->current_fps, ps->degrade_state, ps->config_gen);
    local_log(logline);
    memset(&deg, 0, sizeof(deg));
    deg.type = L4C_EVENT_DEGRADED;
    deg.body.degraded.degrade_state = ps->degrade_state;
    deg.body.degraded.reason = (act == L4C_DEG_ACT_STOP_HIGH_LOAD) ? L4C_HIGH_LOAD : ps->fallback_reason;
    l4c_pipe_enqueue(pipe, &deg, now);
}

static l4c_status_t pipeline_start(l4c_pipeline_state_t *ps, const l4c_start_t *start_params) {
    l4c_status_t status;
    l4c_capture_config_t cap_cfg;
    l4c_encoder_config_t enc_cfg;
    l4c_rtp_config_t rtp_cfg;
    l4c_profile_resolved_t resolved;
    const l4c_profile_params_t *pp;
    uint8_t request = 0;
    bool mft_720p = false, oh264_720p = false, win7 = false;

    memset(ps, 0, sizeof(*ps));

    if (!l4c_profile_parse_request(start_params->profile_id, &request)) {
        return L4C_ERR_INVALID_ARG;
    }
    /* Не определять ОС по выбору GDI/OpenH264. Win7 — явный legacy-путь. */
    win7 = os_is_win7_or_older();
    if (!win7 && request == L4C_PROFILE_REQ_DEFAULT) {
        mft_720p = l4c_mf_encoder_is_supported();
        oh264_720p = true; /* OpenH264 Baseline 3.1 поддерживает 720p; подтверждается init/SPS */
    }
    if (l4c_profile_resolve(request, win7, mft_720p, oh264_720p, &resolved) != L4C_OK) {
        return L4C_ERR_INVALID_ARG;
    }
    pp = l4c_profile_params(resolved.actual_id);
    if (!pp) return L4C_ERR_INVALID_ARG;
    ps->requested_profile = request;
    ps->actual_profile = resolved.actual_id;
    ps->current_fps = resolved.start_fps;
    ps->target_width = pp->width;
    ps->target_height = pp->height;
    ps->bitrate_min = pp->bitrate_min_kbps;
    ps->bitrate_target = pp->bitrate_target_kbps;
    ps->bitrate_max = pp->bitrate_max_kbps;
    ps->degrade_state = L4C_NOMINAL;
    ps->config_gen = 1;
    ps->force_next_idr = true;

    /* Allocate working buffers */
    ps->scaled_buf = (uint8_t *)malloc((size_t)ps->target_width * (size_t)ps->target_height * 4u);
    if (!ps->scaled_buf) return L4C_ERR_OUT_OF_MEMORY;

    /* Color converter */
    status = l4c_color_converter_create(ps->target_width, ps->target_height, &ps->converter);
    if (status != L4C_OK) return status;

    memset(&cap_cfg, 0, sizeof(cap_cfg));
    cap_cfg.target_rect.left = start_params->source_rect.left;
    cap_cfg.target_rect.top = start_params->source_rect.top;
    cap_cfg.target_rect.right = start_params->source_rect.right;
    cap_cfg.target_rect.bottom = start_params->source_rect.bottom;
    cap_cfg.capture_cursor = true;
    ps->cap_cfg = cap_cfg;

    /* Preferred: DXGI Desktop Duplication if supported and single physical output */
    ps->active_capture_backend = 0;
    ps->fallback_reason = L4C_FALLBACK_NONE;
    if (l4c_dxgi_capture_is_supported() && is_single_output(&cap_cfg.target_rect)) {
        status = l4c_dxgi_capture_create(&ps->capture);
        if (status == L4C_OK) {
            status = ps->capture->vtable->init(ps->capture, &cap_cfg);
            if (status == L4C_OK) {
                ps->active_capture_backend = L4C_CAPTURE_DXGI; /* 2 */
            } else {
                ps->capture->vtable->destroy(ps->capture);
                ps->capture = NULL;
            }
        }
    }

    /* Fallback to GDI if DXGI not used or failed */
    if (!ps->capture) {
        status = l4c_gdi_capture_create(&ps->capture);
        if (status != L4C_OK) return status;
        status = ps->capture->vtable->init(ps->capture, &cap_cfg);
        if (status != L4C_OK) return status;
        ps->active_capture_backend = L4C_CAPTURE_GDI; /* 1 */
        if (l4c_dxgi_capture_is_supported()) {
            ps->fallback_reason = L4C_DXGI_ACCESS_LOST;
        }
    }

    /* Encoder selection: prefer Media Foundation Hardware MFT, fallback to OpenH264 */
    ps->active_encoder_backend = 0;
    if (l4c_mf_encoder_is_supported()) {
        status = l4c_mf_encoder_create(&ps->encoder);
        if (status == L4C_OK) {
            memset(&enc_cfg, 0, sizeof(enc_cfg));
            enc_cfg.width = ps->target_width;
            enc_cfg.height = ps->target_height;
            enc_cfg.target_fps = ps->current_fps;
            enc_cfg.target_bitrate_kbps = ps->bitrate_target;
            enc_cfg.max_bitrate_kbps = ps->bitrate_max;
            enc_cfg.input_format = L4C_PIX_FMT_NV12;
            status = ps->encoder->vtable->init(ps->encoder, &enc_cfg);
            if (status == L4C_OK) {
                ps->active_encoder_backend = 2; /* MF_HARDWARE */
            } else {
                ps->encoder->vtable->destroy(ps->encoder);
                ps->encoder = NULL;
            }
        }
    }

    /* Fallback to OpenH264 if MFT not used or failed */
    if (!ps->encoder) {
        status = l4c_openh264_encoder_create(&ps->encoder);
        if (status != L4C_OK) return status;
        memset(&enc_cfg, 0, sizeof(enc_cfg));
        enc_cfg.width = ps->target_width;
        enc_cfg.height = ps->target_height;
        enc_cfg.target_fps = ps->current_fps;
        enc_cfg.target_bitrate_kbps = ps->bitrate_target;
        enc_cfg.max_bitrate_kbps = ps->bitrate_max;
        enc_cfg.input_format = L4C_PIX_FMT_I420;
        status = ps->encoder->vtable->init(ps->encoder, &enc_cfg);
        if (status != L4C_OK) return status;

        ps->active_encoder_backend = 1; /* OPENH264_SOFTWARE */
        if (l4c_mf_encoder_is_supported()) {
            ps->fallback_reason = L4C_FALLBACK_MFT_UNAVAILABLE;
        }
    }

    /* RTP sender */
    memset(&rtp_cfg, 0, sizeof(rtp_cfg));
    rtp_cfg.dest_ip = "127.0.0.1";
    rtp_cfg.rtp_port = start_params->rtp_port ? start_params->rtp_port : 5004;
    rtp_cfg.rtcp_port = start_params->rtcp_port ? start_params->rtcp_port : 5005;
    rtp_cfg.payload_type = 96;
    rtp_cfg.ssrc = 0xDEADBEEF;
    rtp_cfg.cname = "l4capture@terminal";
    status = l4c_rtp_sender_create(&rtp_cfg, &ps->rtp);
    if (status != L4C_OK) return status;

    ps->active = true;
    {
        char logline[160];
        _snprintf_s(logline, sizeof(logline), _TRUNCATE,
                    "START requested=%u actual=%ux%u fps=%u cap=%u enc=%u",
                    ps->requested_profile, ps->target_width, ps->target_height,
                    ps->current_fps, ps->active_capture_backend, ps->active_encoder_backend);
        local_log(logline);
    }
    return L4C_OK;
}

static int run(l4c_args_t *args) {
    l4c_safety_gate_t gate;
    l4c_ipc_pipe_t pipe;
    l4c_session_probe_t session;
    l4c_pipeline_state_t ps;
    l4c_status_t status;
    int exit_code = 0;
    bool started_reported = false;

    memset(&ps, 0, sizeof(ps));

    if (l4c_check_job() != L4C_OK) {
        fprintf(stderr, "l4capture: not in JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE\n");
        return (int)L4C_ERR_FATAL;
    }
    if (l4c_session_open(&session) != L4C_OK) {
        fprintf(stderr, "l4capture: session unavailable (Session 0 or locked)\n");
        return (int)L4C_ERR_SESSION_UNAVAILABLE;
    }
    status = l4c_safety_init(&gate);
    if (status != L4C_OK) { l4c_session_close(&session); return (int)status; }

    status = l4c_pipe_open(&pipe, args->pipe_in, args->pipe_out, &gate);
    if (status != L4C_OK) {
        fprintf(stderr, "l4capture: pipe open failed (%d)\n", (int)status);
        l4c_safety_destroy(&gate); l4c_session_close(&session);
        return (int)status;
    }
    args->pipe_in = NULL; args->pipe_out = NULL;

    /* Main loop */
    while (WaitForSingleObject(gate.stop_event, 0) == WAIT_TIMEOUT) {
        uint64_t now = l4c_now_monotonic_ms();
        bool session_ok = l4c_session_available(&session);

        /* Poll IPC commands */
        status = l4c_pipe_poll(&pipe, now);
        if (status != L4C_OK && status != L4C_ERR_PIPE_BROKEN) break;
        if (WaitForSingleObject(gate.stop_event, 0) != WAIT_TIMEOUT) break;

        /* Safety check — pass session_ok so safety gate can handle session loss */
        status = l4c_safety_check(&gate, now, session_ok);
        if (status != L4C_OK) break;

        /* Check pipe stall */
        if (l4c_pipe_stalled(&pipe, now)) {
            l4c_safety_stop(&gate, L4C_ERR_OVERFLOW);
            break;
        }

        /* Start pipeline when CMD_START received */
        if (gate.started && !ps.active) {
            status = pipeline_start(&ps, &gate.last_start);
            if (status == L4C_OK) {
                send_event_ready(&pipe, now, ps.active_capture_backend, ps.active_encoder_backend,
                                 ps.target_width, ps.target_height, ps.current_fps);
                l4c_degrade_init(&ps.degrade, ps.actual_profile, ps.current_fps, now);
                ps.win_start_ms = now;
                memset(&ps.win_acc, 0, sizeof(ps.win_acc));
                started_reported = true;
            } else {
                fprintf(stderr, "l4capture: pipeline start failed (%d)\n", (int)status);
                l4c_safety_stop(&gate, status);
                break;
            }
        }

        /* Capture → Encode → Send RTP (skip frame if session unavailable) */
        if (ps.active && session_ok && l4c_safety_can_send(&gate, now, session_ok)) {
            uint64_t frame_t0 = now;
            uint8_t tfps = ps.current_fps;
            accumulate_window_sample(&ps, now);
            /* Закрытие окна детектора каждые 3000 мс */
            if (ps.win_start_ms != 0 && (now - ps.win_start_ms) >= L4C_DEGRADE_WINDOW_MS) {
                l4c_degrade_window_t wcopy = ps.win_acc;
                l4c_degrade_action_t act = l4c_degrade_on_window(&ps.degrade, ps.win_start_ms + L4C_DEGRADE_WINDOW_MS, &wcopy);
                memset(&ps.win_acc, 0, sizeof(ps.win_acc));
                ps.win_start_ms += L4C_DEGRADE_WINDOW_MS;
                if (act != L4C_DEG_ACT_NONE) {
                    apply_degrade_action(&ps, &pipe, &gate, act, now);
                    if (WaitForSingleObject(gate.stop_event, 0) != WAIT_TIMEOUT) break;
                }
            }
            if (l4c_pipeline_due(&gate.pipeline, now, ps.current_fps)) {
                l4c_frame_view_t frame;
                uint32_t tw, th;
                frame_t0 = now;
                tfps = ps.current_fps;
                profile_bounds(&ps, &tw, &th, &tfps);
                status = ps.capture->vtable->acquire_frame(ps.capture, &frame, 50);

                if (status == L4C_ERR_SESSION_UNAVAILABLE) {
                    l4c_safety_stop(&gate, L4C_ERR_SESSION_UNAVAILABLE);
                    break;
                }

                if (status == L4C_ERR_DEVICE_LOST && ps.active_capture_backend == L4C_CAPTURE_DXGI) {
                    /* DXGI ACCESS_LOST 3 retries failed -> controlled fallback to GDI */
                    ps.capture->vtable->destroy(ps.capture);
                    ps.capture = NULL;
                    l4c_status_t gdi_s = l4c_gdi_capture_create(&ps.capture);
                    if (gdi_s == L4C_OK) {
                        gdi_s = ps.capture->vtable->init(ps.capture, &ps.cap_cfg);
                        if (gdi_s == L4C_OK) {
                            ps.active_capture_backend = L4C_CAPTURE_GDI;
                            ps.fallback_reason = L4C_DXGI_ACCESS_LOST;
                            l4c_message_t deg_msg;
                            memset(&deg_msg, 0, sizeof(deg_msg));
                            deg_msg.type = L4C_EVENT_DEGRADED;
                            deg_msg.body.degraded.degrade_state = L4C_NOMINAL;
                            deg_msg.body.degraded.reason = L4C_DXGI_ACCESS_LOST;
                            l4c_pipe_enqueue(&pipe, &deg_msg, now);
                            status = ps.capture->vtable->acquire_frame(ps.capture, &frame, 50);
                        }
                    }
                }

                if (status == L4C_OK && frame.data) {
                    ps.frames_captured++;
                    count_raw_pass(&ps);
                    l4c_status_t scale_status;
                    scale_status = l4c_scale_bilinear_bgra(frame.data, frame.width, frame.height, frame.stride,
                                                           ps.scaled_buf, tw, th, tw * 4);
                    ps.capture->vtable->release_frame(ps.capture, &frame);

                    if (scale_status == L4C_OK) {
                        l4c_raw_frame_t raw;
                        l4c_access_unit_t au;
                        uint64_t pts_uniform = (uint64_t)ps.frames_captured * 1000u / (tfps ? tfps : 10u);

                        if (ps.active_encoder_backend == 2) {
                            status = l4c_color_convert_bgra_to_nv12_frame(ps.converter, ps.scaled_buf,
                                                                          tw * 4, pts_uniform, &raw);
                        } else {
                            status = l4c_color_convert_bgra_to_i420(ps.converter, ps.scaled_buf,
                                                                    tw * 4, pts_uniform, &raw);
                        }
                        if (status != L4C_OK) { count_raw_drop(&ps); continue; }

                        raw.force_idr = ps.force_next_idr || l4c_safety_take_idr(&gate, now);
                        ps.force_next_idr = false;

                        memset(&au, 0, sizeof(au));
                        {
                            uint32_t delay_ms = read_test_encode_delay_ms();
                            if (delay_ms) Sleep(delay_ms);
                        }
                        {
                            uint64_t enc_t0 = l4c_now_monotonic_ms();
                            status = ps.encoder->vtable->encode(ps.encoder, &raw, &au);
                            {
                                uint32_t enc_ms = (uint32_t)(l4c_now_monotonic_ms() - enc_t0);
                                ps.win_acc.has_processing = true;
                                if (ps.win_acc.processing_samples < 0xFFFFFFFFu) ps.win_acc.processing_samples++;
                                if (enc_ms > ps.win_acc.processing_p95_ms) ps.win_acc.processing_p95_ms = enc_ms;
                            }
                        }

                        /* Check for hardware MFT runtime failure -> controlled fallback to OpenH264 */
                        if (status == L4C_ERR_DEVICE_LOST && ps.active_encoder_backend == 2) {
                            ps.encoder->vtable->destroy(ps.encoder);
                            ps.encoder = NULL;

                            l4c_status_t fb_status = l4c_openh264_encoder_create(&ps.encoder);
                            if (fb_status == L4C_OK) {
                                l4c_encoder_config_t fb_cfg;
                                memset(&fb_cfg, 0, sizeof(fb_cfg));
                                fb_cfg.width = ps.target_width;
                                fb_cfg.height = ps.target_height;
                                fb_cfg.target_fps = ps.current_fps;
                                fb_cfg.target_bitrate_kbps = ps.bitrate_target;
                                fb_cfg.max_bitrate_kbps = ps.bitrate_max;
                                fb_cfg.input_format = L4C_PIX_FMT_I420;
                                fb_status = ps.encoder->vtable->init(ps.encoder, &fb_cfg);
                                if (fb_status == L4C_OK) {
                                    ps.active_encoder_backend = 1; /* OPENH264_SOFTWARE */
                                    ps.fallback_reason = L4C_FALLBACK_MFT_UNAVAILABLE;

                                    l4c_message_t deg_msg;
                                    memset(&deg_msg, 0, sizeof(deg_msg));
                                    deg_msg.type = L4C_EVENT_DEGRADED;
                                    deg_msg.body.degraded.degrade_state = L4C_NOMINAL;
                                    deg_msg.body.degraded.reason = L4C_FALLBACK_MFT_UNAVAILABLE;
                                    l4c_pipe_enqueue(&pipe, &deg_msg, now);

                                    /* Re-convert current frame to I420 and retry encode */
                                    status = l4c_color_convert_bgra_to_i420(ps.converter, ps.scaled_buf,
                                                                            tw * 4, pts_uniform, &raw);
                                    if (status == L4C_OK) {
                                        raw.force_idr = true;
                                        status = ps.encoder->vtable->encode(ps.encoder, &raw, &au);
                                    }
                                }
                            }
                        }

                        if (status == L4C_OK && au.nal_count > 0) {
                            ps.frames_encoded++;
                            count_enc_pass(&ps);
                            ps.au_bytes_10s += (uint32_t)au.total_bytes;
                            {
                                l4c_status_t rtp_status = l4c_rtp_send_au(ps.rtp, &au, ps.encoder);
                                if (rtp_status == L4C_OK) {
                                    ps.frames_sent++;
                                    count_tr_pass(&ps);
                                } else {
                                    count_tr_drop(&ps);
                                    l4c_safety_take_idr(&gate, now);
                                }
                            }
                            ps.encoder->vtable->release_au(ps.encoder, &au);
                        } else if (status == L4C_ERR_NO_FRAME) {
                            ps.frames_skipped++;
                            count_enc_drop(&ps);
                        } else {
                            count_enc_drop(&ps);
                        }
                    }
                } else {
                    count_raw_drop(&ps);
                }
            }

            /* Overload accounting: пропущенные pacing-слоты = raw drops (latest-frame). */
            if (ps.active && tfps) {
                uint64_t after = l4c_now_monotonic_ms();
                uint32_t interval = 1000u / (tfps ? tfps : 10u);
                if (interval && after > frame_t0 + interval) {
                    uint32_t missed = (uint32_t)((after - frame_t0) / interval);
                    uint32_t k;
                    if (missed > 0) --missed;
                    if (missed > 20u) missed = 20u;
                    for (k = 0; k < missed; ++k) count_raw_drop(&ps);
                }
            }

            /* Periodic metrics */
            if (now - ps.last_metrics_ms >= 1000) {
                send_event_metrics(&pipe, &ps, now);
                ps.last_metrics_ms = now;
            }
            if (ps.bitrate_win_start_ms == 0) ps.bitrate_win_start_ms = now;
            if (now - ps.bitrate_win_start_ms >= 10000) {
                ps.bitrate_win_start_ms = now;
                ps.au_bytes_10s = 0;
            }

            /* RTCP Sender Reports (keepalive for l4media ingress) */
            if (ps.rtp) {
                bool pli = false;
                l4c_rtp_sender_poll_rtcp(ps.rtp, &pli);
                if (pli) l4c_safety_take_idr(&gate, now);
            }
        }

        /* Sleep to pace the loop */
        Sleep(5);
    }

    pipeline_cleanup(&ps);
    l4c_pipe_close(&pipe);
    exit_code = (int)l4c_safety_reason(&gate);
    l4c_safety_destroy(&gate);
    l4c_session_close(&session);
    return exit_code;
}

int main(int argc, char *argv[]) {
    l4c_args_t args = parse_args(argc, argv);
    if (!args.valid) {
        fprintf(stderr, "Usage: l4capture.exe --pipe-in=<HANDLE> --pipe-out=<HANDLE>\n");
        return (int)L4C_ERR_INVALID_ARG;
    }
    return run(&args);
}
