#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "l4capture/clock.h"
#include "l4capture/safety_gate.h"
#include "l4capture/ipc_pipe.h"
#include "l4capture/gdi_capture.h"
#include "l4capture/openh264_encoder.h"
#include "l4capture/rtp_sender.h"
#include "l4capture/scale.h"
#include "l4capture/color_convert.h"
#include "l4capture/types.h"

#define TARGET_WIDTH  854
#define TARGET_HEIGHT 480
#define TARGET_FPS    10

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
} l4c_pipeline_state_t;

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

static void send_event_ready(l4c_ipc_pipe_t *pipe, uint64_t now) {
    l4c_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = L4C_EVENT_READY;
    msg.request_seq = 0;
    msg.body.ready.actual_width = TARGET_WIDTH;
    msg.body.ready.actual_height = TARGET_HEIGHT;
    msg.body.ready.actual_fps = TARGET_FPS;
    msg.body.ready.capture_backend = 1; /* GDI */
    msg.body.ready.encoder_backend = 1; /* OpenH264 */
    l4c_pipe_enqueue(pipe, &msg, now);
}

static void send_event_metrics(l4c_ipc_pipe_t *pipe, const l4c_pipeline_state_t *ps, uint64_t now) {
    l4c_message_t msg;
    l4c_rtp_stats_t rtp_stats;
    memset(&msg, 0, sizeof(msg));
    msg.type = L4C_EVENT_METRICS;
    msg.body.metrics.fps = TARGET_FPS;
    msg.body.metrics.bitrate_kbps = 500;
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

static l4c_status_t pipeline_start(l4c_pipeline_state_t *ps, const l4c_start_t *start_params) {
    l4c_status_t status;
    l4c_capture_config_t cap_cfg;
    l4c_encoder_config_t enc_cfg;
    l4c_rtp_config_t rtp_cfg;

    memset(ps, 0, sizeof(*ps));

    /* Allocate working buffers */
    ps->scaled_buf = (uint8_t *)malloc(TARGET_WIDTH * TARGET_HEIGHT * 4);
    if (!ps->scaled_buf) return L4C_ERR_OUT_OF_MEMORY;

    /* Color converter */
    status = l4c_color_converter_create(TARGET_WIDTH, TARGET_HEIGHT, &ps->converter);
    if (status != L4C_OK) return status;

    /* GDI capture */
    status = l4c_gdi_capture_create(&ps->capture);
    if (status != L4C_OK) return status;
    memset(&cap_cfg, 0, sizeof(cap_cfg));
    cap_cfg.target_rect.left = start_params->source_rect.left;
    cap_cfg.target_rect.top = start_params->source_rect.top;
    cap_cfg.target_rect.right = start_params->source_rect.right;
    cap_cfg.target_rect.bottom = start_params->source_rect.bottom;
    cap_cfg.capture_cursor = true;
    status = ps->capture->vtable->init(ps->capture, &cap_cfg);
    if (status != L4C_OK) return status;

    /* OpenH264 encoder */
    status = l4c_openh264_encoder_create(&ps->encoder);
    if (status != L4C_OK) return status;
    memset(&enc_cfg, 0, sizeof(enc_cfg));
    enc_cfg.width = TARGET_WIDTH;
    enc_cfg.height = TARGET_HEIGHT;
    enc_cfg.target_fps = TARGET_FPS;
    enc_cfg.target_bitrate_kbps = 500;
    enc_cfg.max_bitrate_kbps = 700;
    enc_cfg.input_format = L4C_PIX_FMT_I420;
    status = ps->encoder->vtable->init(ps->encoder, &enc_cfg);
    if (status != L4C_OK) return status;

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
                send_event_ready(&pipe, now);
                started_reported = true;
            } else {
                fprintf(stderr, "l4capture: pipeline start failed (%d)\n", (int)status);
                l4c_safety_stop(&gate, status);
                break;
            }
        }

        /* Capture → Encode → Send RTP (skip frame if session unavailable) */
        if (ps.active && session_ok && l4c_safety_can_send(&gate, now, session_ok)) {
            if (l4c_pipeline_due(&gate.pipeline, now, TARGET_FPS)) {
                l4c_frame_view_t frame;
                static uint32_t s_frame_count = 0;
                status = ps.capture->vtable->acquire_frame(ps.capture, &frame, 50);
                if (status == L4C_OK && frame.data) {
                    ps.frames_captured++;
                    /* Scale to 854x480 */
                    l4c_status_t scale_status;
                    scale_status = l4c_scale_bilinear_bgra(frame.data, frame.width, frame.height, frame.stride,
                                                           ps.scaled_buf, TARGET_WIDTH, TARGET_HEIGHT, TARGET_WIDTH * 4);
                    ps.capture->vtable->release_frame(ps.capture, &frame);

                    if (scale_status == L4C_OK) {
                        /* Convert BGRA → I420. Use frame-counter PTS for uniform timestamps
                         * (ffmpeg-style constant frame rate), not wall-clock which has jitter. */
                        l4c_raw_frame_t raw;
                        l4c_access_unit_t au;
                        uint64_t pts_uniform = (uint64_t)ps.frames_captured * 1000u / TARGET_FPS;
                        status = l4c_color_convert_bgra_to_i420(ps.converter, ps.scaled_buf,
                                                                TARGET_WIDTH * 4, pts_uniform, &raw);
                        if (status != L4C_OK) { ps.raw_drops++; continue; }

                        raw.force_idr = l4c_safety_take_idr(&gate, now);

                        memset(&au, 0, sizeof(au));
                        status = ps.encoder->vtable->encode(ps.encoder, &raw, &au);
                        if (status == L4C_OK && au.nal_count > 0) {
                            ps.frames_encoded++;
                            /* Send RTP */
                            l4c_status_t rtp_status = l4c_rtp_send_au(ps.rtp, &au, ps.encoder);
                            if (rtp_status == L4C_OK) {
                                ps.frames_sent++;
                            } else {
                                ps.transport_drops++;
                                l4c_safety_take_idr(&gate, now);
                            }
                            ps.encoder->vtable->release_au(ps.encoder, &au);
                        } else if (status == L4C_ERR_NO_FRAME) {
                            ps.frames_skipped++;
                        } else {
                            ps.encoder_drops++;
                        }
                    }
                } else {
                    ps.raw_drops++;
                }
            }

            /* Periodic metrics */
            if (now - ps.last_metrics_ms >= 1000) {
                send_event_metrics(&pipe, &ps, now);
                ps.last_metrics_ms = now;
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
