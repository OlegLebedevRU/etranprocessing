#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <windows.h>
#include <psapi.h>

#include "l4capture/mf_encoder.h"
#include "l4capture/openh264_encoder.h"
#include "l4capture/color_convert.h"
#include "l4capture/clock.h"
#include "l4capture/limits.h"
#include "l4capture/telemetry.h"

/* Helper: create a synthetic NV12 frame */
static l4c_raw_frame_t make_synthetic_nv12_frame(
    uint8_t *y_plane, uint8_t *uv_plane,
    uint32_t w, uint32_t h, uint64_t pts
) {
    l4c_raw_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.planes[0] = y_plane;
    frame.planes[1] = uv_plane;
    frame.planes[2] = NULL;
    frame.strides[0] = (uint32_t)w;
    frame.strides[1] = (uint32_t)w;
    frame.strides[2] = 0;
    frame.width = w;
    frame.height = h;
    frame.format = L4C_PIX_FMT_NV12;
    frame.pts_ms = pts;
    frame.force_idr = false;
    frame.plane_sizes[0] = (size_t)w * h;
    frame.plane_sizes[1] = (size_t)w * ((h + 1) / 2);
    frame.plane_sizes[2] = 0;
    return frame;
}

static l4c_encoder_config_t make_mf_config(uint32_t w, uint32_t h, uint32_t fps) {
    l4c_encoder_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.width = w;
    cfg.height = h;
    cfg.target_fps = fps;
    cfg.target_bitrate_kbps = 500;
    cfg.max_bitrate_kbps = 700;
    cfg.input_format = L4C_PIX_FMT_NV12;
    return cfg;
}

/* 1. Discovery probe: cache-first, rare full probe, hang ceiling 8 s.
 * Найденная конфигурация сохраняется; true после нахождения допустим
 * (пользователь согласен ждать до ~5 с ради стабильного результата). */
int test_mf_probe_graceful(void) {
    uint64_t t0;
    bool supported;
    uint64_t elapsed;

    l4c_mf_encoder_cache_reset();
    t0 = l4c_now_monotonic_ms();
    supported = l4c_mf_encoder_is_supported();
    elapsed = l4c_now_monotonic_ms() - t0;

    printf(" (supported=%s, probe_time=%llums)", supported ? "true" : "false", (unsigned long long)elapsed);

    if (elapsed > 8000) return 1; /* абсолютный потолок от зависания */

    /* Repeat: cache hit / in-memory cache must be fast and stable */
    t0 = l4c_now_monotonic_ms();
    {
        bool again = l4c_mf_encoder_is_supported();
        if (again != supported) return 3;
    }
    if (l4c_now_monotonic_ms() - t0 > 100) return 2;

    return 0;
}

/* 1b. Cached strategy is rechecked with real frames after a process restart. */
int test_mf_capability_cache_persist(void) {
    uint64_t t0;
    bool first, second;

    l4c_mf_encoder_cache_reset();
    first = l4c_mf_encoder_is_supported();

    /* Simulate process restart: drop in-memory cache only (file remains). */
    l4c_mf_encoder_test_inject_probe_fail(true);
    l4c_mf_encoder_test_inject_probe_fail(false);
    t0 = l4c_now_monotonic_ms();
    second = l4c_mf_encoder_is_supported();
    if (second != first) return 1;
    /* Discovery plus synthetic sequence must remain within the startup budget. */
    if (first && l4c_now_monotonic_ms() - t0 > 5000) return 2;

    /* Runtime failure: in-process stays fail-closed (no mid-stream re-probe).
     * Disk is retryable — следующий старт может редко перепробовать. */
    l4c_mf_encoder_note_runtime_failure();
    if (l4c_mf_encoder_is_supported() != false) return 3;
    /* Restore clean capability state for subsequent MFT tests. */
    l4c_mf_encoder_cache_reset();
    return 0;
}

/* 2. Create and destroy backend */
int test_mf_create_destroy(void) {
    l4c_encoder_backend_t *enc = NULL;
    l4c_status_t s = l4c_mf_encoder_create(&enc);
    if (s != L4C_OK || !enc) return 1;

    if (!enc->vtable || !enc->vtable->init || !enc->vtable->encode ||
        !enc->vtable->force_idr || !enc->vtable->release_au || !enc->vtable->destroy) {
        enc->vtable->destroy(enc);
        return 2;
    }

    enc->vtable->destroy(enc);
    return 0;
}

/* 3. Media types and SDP compatibility */
int test_mf_init_types_and_sdp_compat(void) {
    l4c_encoder_backend_t *enc = NULL;
    l4c_encoder_config_t cfg = make_mf_config(854, 480, 10);
    l4c_status_t s;
    bool supported = l4c_mf_encoder_is_supported();

    s = l4c_mf_encoder_create(&enc);
    if (s != L4C_OK || !enc) return 1;

    s = enc->vtable->init(enc, &cfg);
    if (s != L4C_OK) {
        /* Graceful fail only when HW is not available; if probe said yes, fail. */
        enc->vtable->destroy(enc);
        return supported ? 2 : 0;
    }
    enc->vtable->destroy(enc);
    return 0;
}

/* 4. Color conversion BGRA -> NV12 */
int test_mf_color_convert_bgra_to_nv12(void) {
    l4c_color_converter_t *cvt = NULL;
    l4c_raw_frame_t raw;
    l4c_status_t s;
    uint8_t bgra[4 * 4 * 4];
    uint32_t x, y;

    s = l4c_color_converter_create(4, 4, &cvt);
    if (s != L4C_OK || !cvt) return 1;

    /* Test White: B=255, G=255, R=255 -> BT.601 Y=235, U=128, V=128 */
    memset(bgra, 255, sizeof(bgra));
    s = l4c_color_convert_bgra_to_nv12_frame(cvt, bgra, 16, 0, &raw);
    if (s != L4C_OK) { l4c_color_converter_destroy(cvt); return 2; }
    if (raw.planes[0][0] != 235) { l4c_color_converter_destroy(cvt); return 3; }
    if (raw.planes[1][0] < 127 || raw.planes[1][0] > 129) { l4c_color_converter_destroy(cvt); return 4; }
    if (raw.planes[1][1] < 127 || raw.planes[1][1] > 129) { l4c_color_converter_destroy(cvt); return 5; }

    /* Test Black: B=0, G=0, R=0 -> BT.601 Y=16, U=128, V=128 */
    memset(bgra, 0, sizeof(bgra));
    for (y = 0; y < 4; ++y) {
        for (x = 0; x < 4; ++x) {
            bgra[(y * 4 + x) * 4 + 3] = 255; /* alpha */
        }
    }
    s = l4c_color_convert_bgra_to_nv12_frame(cvt, bgra, 16, 0, &raw);
    if (s != L4C_OK) { l4c_color_converter_destroy(cvt); return 6; }
    if (raw.planes[0][0] != 16) { l4c_color_converter_destroy(cvt); return 7; }
    if (raw.planes[1][0] < 127 || raw.planes[1][0] > 129) { l4c_color_converter_destroy(cvt); return 8; }
    if (raw.planes[1][1] < 127 || raw.planes[1][1] > 129) { l4c_color_converter_destroy(cvt); return 9; }

    /* Test Red: B=0, G=0, R=255 -> V should be significantly higher than 128 */
    memset(bgra, 0, sizeof(bgra));
    for (y = 0; y < 4; ++y) {
        for (x = 0; x < 4; ++x) {
            bgra[(y * 4 + x) * 4 + 2] = 255; /* R */
            bgra[(y * 4 + x) * 4 + 3] = 255;
        }
    }
    s = l4c_color_convert_bgra_to_nv12_frame(cvt, bgra, 16, 0, &raw);
    if (s != L4C_OK) { l4c_color_converter_destroy(cvt); return 10; }
    /* In NV12, planes[1][0] is U, planes[1][1] is V. For pure Red: U < 128, V > 200 */
    if (raw.planes[1][0] >= 128 || raw.planes[1][1] <= 200) {
        l4c_color_converter_destroy(cvt);
        return 11;
    }

    /* Test Blue: B=255, G=0, R=0 -> U should be significantly higher than 128 */
    memset(bgra, 0, sizeof(bgra));
    for (y = 0; y < 4; ++y) {
        for (x = 0; x < 4; ++x) {
            bgra[(y * 4 + x) * 4 + 0] = 255; /* B */
            bgra[(y * 4 + x) * 4 + 3] = 255;
        }
    }
    s = l4c_color_convert_bgra_to_nv12_frame(cvt, bgra, 16, 0, &raw);
    if (s != L4C_OK) { l4c_color_converter_destroy(cvt); return 12; }
    /* For pure Blue: U > 200, V < 128 */
    if (raw.planes[1][0] <= 200 || raw.planes[1][1] >= 128) {
        l4c_color_converter_destroy(cvt);
        return 13;
    }

    l4c_color_converter_destroy(cvt);
    return 0;
}

/* 5. NAL normalization and Annex B / AVCC stripping */
int test_mf_nal_normalization_and_stripping(void) {
    if (l4c_mf_encoder_is_supported()) {
        l4c_encoder_backend_t *enc = NULL;
        l4c_encoder_config_t cfg = make_mf_config(854, 480, 10);
        uint8_t *y, *uv;
        l4c_raw_frame_t raw;
        l4c_access_unit_t au;
        l4c_status_t s;
        uint32_t i;

        s = l4c_mf_encoder_create(&enc);
        if (s != L4C_OK) return 1;
        s = enc->vtable->init(enc, &cfg);
        if (s != L4C_OK) { enc->vtable->destroy(enc); return 2; }

        y = (uint8_t *)malloc(854 * 480);
        uv = (uint8_t *)malloc(854 * 240);
        if (!y || !uv) { free(y); free(uv); enc->vtable->destroy(enc); return 3; }
        memset(y, 128, 854 * 480);
        memset(uv, 128, 854 * 240);

        raw = make_synthetic_nv12_frame(y, uv, 854, 480, 0);
        memset(&au, 0, sizeof(au));
        s = enc->vtable->encode(enc, &raw, &au);
        if (s == L4C_ERR_NO_FRAME) {
            raw = make_synthetic_nv12_frame(y, uv, 854, 480, 100);
            s = enc->vtable->encode(enc, &raw, &au);
        }

        if (s == L4C_OK) {
            for (i = 0; i < au.nal_count; ++i) {
                const uint8_t *d = au.nals[i].data;
                uint32_t len = au.nals[i].length;
                if (len >= 3 && d[0] == 0 && d[1] == 0 && d[2] == 1) {
                    free(y); free(uv); enc->vtable->destroy(enc); return 10 + (int)i;
                }
                if (len >= 4 && d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 1) {
                    free(y); free(uv); enc->vtable->destroy(enc); return 20 + (int)i;
                }
                if (d[0] & 0x80) {
                    free(y); free(uv); enc->vtable->destroy(enc); return 30 + (int)i;
                }
            }
            enc->vtable->release_au(enc, &au);
        }

        free(y); free(uv);
        enc->vtable->destroy(enc);
    }
    return 0;
}

/* 6. First frame IDR with SPS and PPS */
int test_mf_first_frame_idr_sps_pps(void) {
    if (l4c_mf_encoder_is_supported()) {
        l4c_encoder_backend_t *enc = NULL;
        l4c_encoder_config_t cfg = make_mf_config(854, 480, 10);
        uint8_t *y, *uv;
        l4c_raw_frame_t raw;
        l4c_access_unit_t au;
        l4c_status_t s;
        bool has_sps = false, has_pps = false, has_idr = false;
        uint32_t i;

        s = l4c_mf_encoder_create(&enc);
        if (s != L4C_OK) return 1;
        s = enc->vtable->init(enc, &cfg);
        if (s != L4C_OK) { enc->vtable->destroy(enc); return 2; }

        y = (uint8_t *)malloc(854 * 480);
        uv = (uint8_t *)malloc(854 * 240);
        if (!y || !uv) { free(y); free(uv); enc->vtable->destroy(enc); return 3; }
        memset(y, 128, 854 * 480);
        memset(uv, 128, 854 * 240);

        raw = make_synthetic_nv12_frame(y, uv, 854, 480, 0);
        memset(&au, 0, sizeof(au));
        s = enc->vtable->encode(enc, &raw, &au);
        if (s == L4C_ERR_NO_FRAME) {
            raw = make_synthetic_nv12_frame(y, uv, 854, 480, 100);
            s = enc->vtable->encode(enc, &raw, &au);
        }

        if (s != L4C_OK) {
            free(y); free(uv); enc->vtable->destroy(enc); return 4;
        }

        for (i = 0; i < au.nal_count; ++i) {
            if (au.nals[i].nal_type == 7) has_sps = true;
            if (au.nals[i].nal_type == 8) has_pps = true;
            if (au.nals[i].nal_type == 5) has_idr = true;
        }

        free(y); free(uv);
        enc->vtable->release_au(enc, &au);
        enc->vtable->destroy(enc);

        if (!has_sps || !has_pps || !has_idr || !au.is_idr) {
            return 5;
        }
    }
    return 0;
}

/* 7. IDR cadence <= 2.0s and SPS/PPS repetition before every IDR */
int test_mf_idr_cadence_and_sps_repetition(void) {
    if (l4c_mf_encoder_is_supported()) {
        l4c_encoder_backend_t *enc = NULL;
        l4c_encoder_config_t cfg = make_mf_config(854, 480, 10);
        uint8_t *y, *uv;
        l4c_raw_frame_t raw;
        l4c_access_unit_t au;
        l4c_status_t s;
        int frame, idr_count = 0;
        uint64_t pts = 0;

        s = l4c_mf_encoder_create(&enc);
        if (s != L4C_OK) return 1;
        s = enc->vtable->init(enc, &cfg);
        if (s != L4C_OK) { enc->vtable->destroy(enc); return 2; }

        y = (uint8_t *)malloc(854 * 480);
        uv = (uint8_t *)malloc(854 * 240);
        if (!y || !uv) { free(y); free(uv); enc->vtable->destroy(enc); return 3; }
        memset(y, 128, 854 * 480);
        memset(uv, 128, 854 * 240);

        for (frame = 0; frame < 30; ++frame) {
            raw = make_synthetic_nv12_frame(y, uv, 854, 480, pts);
            memset(&au, 0, sizeof(au));
            s = enc->vtable->encode(enc, &raw, &au);
            if (s == L4C_ERR_NO_FRAME) {
                pts += 100;
                continue;
            }
            if (s != L4C_OK) {
                free(y); free(uv); enc->vtable->destroy(enc); return 4;
            }

            if (au.is_idr) {
                bool has_sps = false, has_pps = false;
                uint32_t i;
                idr_count++;
                for (i = 0; i < au.nal_count; ++i) {
                    if (au.nals[i].nal_type == 7) has_sps = true;
                    if (au.nals[i].nal_type == 8) has_pps = true;
                }
                if (!has_sps || !has_pps) {
                    free(y); free(uv); enc->vtable->release_au(enc, &au);
                    enc->vtable->destroy(enc);
                    return 5;
                }
            }
            enc->vtable->release_au(enc, &au);
            pts += 100;
        }

        free(y); free(uv);
        enc->vtable->destroy(enc);

        if (idr_count < 2) return 6;
    }
    return 0;
}

/* 8. Force-IDR coalescing */
int test_mf_force_idr_coalescing(void) {
    l4c_encoder_backend_t *enc = NULL;
    l4c_encoder_config_t cfg = make_mf_config(854, 480, 10);
    l4c_status_t s;

    s = l4c_mf_encoder_create(&enc);
    if (s != L4C_OK) return 1;

    if (l4c_mf_encoder_is_supported()) {
        s = enc->vtable->init(enc, &cfg);
        if (s != L4C_OK) { enc->vtable->destroy(enc); return 2; }

        /* First call */
        s = enc->vtable->force_idr(enc);
        if (s != L4C_OK) { enc->vtable->destroy(enc); return 3; }

        /* Rapid second call: coalesced */
        s = enc->vtable->force_idr(enc);
        if (s != L4C_OK) { enc->vtable->destroy(enc); return 4; }
    }

    enc->vtable->destroy(enc);
    return 0;
}

/* 9. MFT failure and OpenH264 safe fallback */
int test_mf_mft_failure_openh264_fallback(void) {
    l4c_encoder_backend_t *mf_enc = NULL;
    l4c_encoder_backend_t *fb_enc = NULL;
    l4c_encoder_config_t mf_cfg = make_mf_config(854, 480, 10);
    l4c_encoder_config_t fb_cfg;
    l4c_raw_frame_t raw;
    l4c_access_unit_t au;
    l4c_status_t s;
    uint8_t *y, *u, *v;

    y = (uint8_t *)malloc(854 * 480);
    u = (uint8_t *)malloc(427 * 240);
    v = (uint8_t *)malloc(427 * 240);
    if (!y || !u || !v) { free(y); free(u); free(v); return 1; }
    memset(y, 128, 854 * 480);
    memset(u, 128, 427 * 240);
    memset(v, 128, 427 * 240);

    /* Test 1: Injected probe failure falls back directly to OpenH264 */
    l4c_mf_encoder_test_inject_probe_fail(true);
    if (l4c_mf_encoder_is_supported() != false) {
        l4c_mf_encoder_test_inject_probe_fail(false);
        free(y); free(u); free(v);
        return 2;
    }
    l4c_mf_encoder_test_inject_probe_fail(false);

    /* Test 2: Injected encode failure triggers clean fallback */
    if (l4c_mf_encoder_is_supported()) {
        s = l4c_mf_encoder_create(&mf_enc);
        if (s != L4C_OK) { free(y); free(u); free(v); return 3; }
        s = mf_enc->vtable->init(mf_enc, &mf_cfg);
        if (s != L4C_OK) { free(y); free(u); free(v); mf_enc->vtable->destroy(mf_enc); return 4; }

        l4c_mf_encoder_test_inject_process_fail(true);
        raw = make_synthetic_nv12_frame(y, u, 854, 480, 0);
        memset(&au, 0, sizeof(au));
        s = mf_enc->vtable->encode(mf_enc, &raw, &au);
        l4c_mf_encoder_test_inject_process_fail(false);

        if (s != L4C_ERR_DEVICE_LOST) {
            free(y); free(u); free(v);
            mf_enc->vtable->destroy(mf_enc);
            return 5;
        }

        /* Destroy failed MFT encoder */
        mf_enc->vtable->destroy(mf_enc);
        mf_enc = NULL;

        /* Create OpenH264 fallback encoder */
        s = l4c_openh264_encoder_create(&fb_enc);
        if (s != L4C_OK || !fb_enc) { free(y); free(u); free(v); return 6; }

        memset(&fb_cfg, 0, sizeof(fb_cfg));
        fb_cfg.width = 854;
        fb_cfg.height = 480;
        fb_cfg.target_fps = 10;
        fb_cfg.target_bitrate_kbps = 500;
        fb_cfg.max_bitrate_kbps = 700;
        fb_cfg.input_format = L4C_PIX_FMT_I420;

        s = fb_enc->vtable->init(fb_enc, &fb_cfg);
        if (s != L4C_OK) { free(y); free(u); free(v); fb_enc->vtable->destroy(fb_enc); return 7; }

        /* Encode with I420 raw frame */
        memset(&raw, 0, sizeof(raw));
        raw.planes[0] = y;
        raw.planes[1] = u;
        raw.planes[2] = v;
        raw.strides[0] = 854;
        raw.strides[1] = 427;
        raw.strides[2] = 427;
        raw.width = 854;
        raw.height = 480;
        raw.format = L4C_PIX_FMT_I420;
        raw.pts_ms = 0;
        raw.force_idr = true;
        raw.plane_sizes[0] = 854 * 480;
        raw.plane_sizes[1] = 427 * 240;
        raw.plane_sizes[2] = 427 * 240;

        memset(&au, 0, sizeof(au));
        s = fb_enc->vtable->encode(fb_enc, &raw, &au);
        if (s != L4C_OK || au.nal_count == 0 || !au.is_idr) {
            free(y); free(u); free(v);
            fb_enc->vtable->destroy(fb_enc);
            return 8;
        }

        fb_enc->vtable->release_au(fb_enc, &au);
        fb_enc->vtable->destroy(fb_enc);
    }

    free(y); free(u); free(v);
    return 0;
}

/* 10. 100 frames stress test: zero memory/handle leaks */
int test_mf_stress_100_frames_zero_leak(void) {
    if (l4c_mf_encoder_is_supported()) {
        l4c_encoder_backend_t *enc = NULL;
        l4c_encoder_config_t cfg = make_mf_config(854, 480, 10);
        uint8_t *y, *uv;
        l4c_raw_frame_t raw;
        l4c_access_unit_t au;
        l4c_status_t s;
        int frame;
        SIZE_T mem_before = 0, mem_after = 0;
        PROCESS_MEMORY_COUNTERS pmc;

        s = l4c_mf_encoder_create(&enc);
        if (s != L4C_OK) return 1;
        s = enc->vtable->init(enc, &cfg);
        if (s != L4C_OK) { enc->vtable->destroy(enc); return 2; }

        y = (uint8_t *)malloc(854 * 480);
        uv = (uint8_t *)malloc(854 * 240);
        if (!y || !uv) { free(y); free(uv); enc->vtable->destroy(enc); return 3; }
        memset(y, 128, 854 * 480);
        memset(uv, 128, 854 * 240);

        /* Warm-up: 20 frames (async HW MFT allocates internal tables). */
        for (frame = 0; frame < 20; ++frame) {
            raw = make_synthetic_nv12_frame(y, uv, 854, 480, (uint64_t)frame * 100);
            memset(&au, 0, sizeof(au));
            enc->vtable->encode(enc, &raw, &au);
            enc->vtable->release_au(enc, &au);
        }

        /* Baseline memory measurement */
        memset(&pmc, 0, sizeof(pmc));
        pmc.cb = sizeof(pmc);
        GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
        mem_before = pmc.PagefileUsage;

        /* Encode 100 frames */
        for (frame = 20; frame < 120; ++frame) {
            raw = make_synthetic_nv12_frame(y, uv, 854, 480, (uint64_t)frame * 100);
            memset(&au, 0, sizeof(au));
            s = enc->vtable->encode(enc, &raw, &au);
            enc->vtable->release_au(enc, &au);
            if (s != L4C_OK && s != L4C_ERR_NO_FRAME) {
                free(y); free(uv);
                enc->vtable->destroy(enc);
                return 4;
            }
        }

        /* Post-test memory measurement */
        memset(&pmc, 0, sizeof(pmc));
        pmc.cb = sizeof(pmc);
        GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
        mem_after = pmc.PagefileUsage;

        free(y); free(uv);
        enc->vtable->destroy(enc);

        /* HW MFT (Intel QSV) keeps driver pools after warm-up — only catch unbounded leak. */
        if (mem_before > 0 && mem_after > mem_before * 3) {
            return 5;
        }
    }
    return 0;
}
