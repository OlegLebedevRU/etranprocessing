#include <stdio.h>
#include <string.h>
#include "l4capture/video_profile.h"
#include "l4capture/degrade_controller.h"

/* Helpers: synthetic window with forced drop ratio. */
static l4c_degrade_window_t make_bad_raw_window(void) {
    l4c_degrade_window_t w;
    memset(&w, 0, sizeof(w));
    w.raw.passed = 1;
    w.raw.dropped = 4; /* 80% > 20% */
    return w;
}

static l4c_degrade_window_t make_good_window(void) {
    l4c_degrade_window_t w;
    memset(&w, 0, sizeof(w));
    w.raw.passed = 10;
    w.raw.dropped = 0;
    w.encoder.passed = 10;
    w.transport.passed = 10;
    return w;
}



int test_profile_params_table(void) {
    const l4c_profile_params_t *p480 = l4c_profile_params(L4C_PROFILE_480P);
    const l4c_profile_params_t *p540 = l4c_profile_params(L4C_PROFILE_540P);
    const l4c_profile_params_t *p720 = l4c_profile_params(L4C_PROFILE_720P);
    if (!p480 || !p540 || !p720) return 1;
    if (p480->width != 854 || p480->height != 480) return 2;
    if (p480->fps_nominal != 10 || p480->fps_floor != 10) return 3;
    if (p480->bitrate_min_kbps != 500 || p480->bitrate_target_kbps != 500 || p480->bitrate_max_kbps != 700) return 4;
    if (!p480->input_profile_eligible) return 5;
    if (p540->width != 960 || p540->height != 540) return 6;
    if (p540->fps_nominal != 15 || p540->fps_floor != 10) return 7;
    if (p540->bitrate_min_kbps != 600 || p540->bitrate_target_kbps != 700 || p540->bitrate_max_kbps != 900) return 8;
    if (p540->input_profile_eligible) return 9;
    if (p720->width != 1280 || p720->height != 720) return 10;
    if (p720->fps_nominal != 15 || p720->fps_floor != 10) return 11;
    if (p720->bitrate_min_kbps != 600 || p720->bitrate_target_kbps != 800 || p720->bitrate_max_kbps != 1000) return 12;
    if (p720->input_profile_eligible) return 13;
    if (l4c_profile_params(99) != NULL) return 14;
    return 0;
}

int test_profile_resolve_low_never_upgrades(void) {
    l4c_profile_resolved_t r;
    if (l4c_profile_resolve(L4C_PROFILE_REQ_LOW, false, true, true, &r) != L4C_OK) return 1;
    if (r.actual_id != L4C_PROFILE_480P || r.start_fps != 10) return 2;
    return 0;
}

int test_profile_resolve_default_win7_is_480p(void) {
    l4c_profile_resolved_t r;
    if (l4c_profile_resolve(L4C_PROFILE_REQ_DEFAULT, true, true, true, &r) != L4C_OK) return 1;
    if (r.actual_id != L4C_PROFILE_480P || r.start_fps != 10) return 2;
    if (!r.win7_legacy) return 3;
    return 0;
}

int test_profile_resolve_default_maps_upper_bound(void) {
    l4c_profile_resolved_t r;
    /* MFT 720p / 15 */
    if (l4c_profile_resolve(L4C_PROFILE_REQ_DEFAULT, false, true, false, &r) != L4C_OK) return 1;
    if (r.actual_id != L4C_PROFILE_720P || r.start_fps != 15) return 2;
    /* OpenH264 720p / 10 */
    if (l4c_profile_resolve(L4C_PROFILE_REQ_DEFAULT, false, false, true, &r) != L4C_OK) return 3;
    if (r.actual_id != L4C_PROFILE_720P || r.start_fps != 10) return 4;
    /* 720p unsupported -> 480p refused_premium; GDI/отсутствие MFT != Win7 */
    if (l4c_profile_resolve(L4C_PROFILE_REQ_DEFAULT, false, false, false, &r) != L4C_OK) return 5;
    if (r.actual_id != L4C_PROFILE_480P || !r.refused_premium || r.win7_legacy) return 6;
    /* 540p не стартовый */
    if (r.actual_id == L4C_PROFILE_540P) return 7;
    /* unknown request */
    if (l4c_profile_resolve(99, false, true, true, &r) != L4C_ERR_INVALID_ARG) return 8;
    return 0;
}

int test_input_gate_denied_for_default_even_at_480p(void) {
    if (l4c_profile_input_gate_allows(L4C_PROFILE_REQ_DEFAULT, L4C_PROFILE_480P)) return 1;
    if (l4c_profile_input_gate_allows(L4C_PROFILE_REQ_DEFAULT, L4C_PROFILE_540P)) return 2;
    if (l4c_profile_input_gate_allows(L4C_PROFILE_REQ_DEFAULT, L4C_PROFILE_720P)) return 3;
    return 0;
}

int test_input_gate_allowed_only_low_and_480p(void) {
    uint8_t req = 0;
    if (!l4c_profile_input_gate_allows(L4C_PROFILE_REQ_LOW, L4C_PROFILE_480P)) return 1;
    if (l4c_profile_input_gate_allows(L4C_PROFILE_REQ_LOW, L4C_PROFILE_540P)) return 2;
    if (l4c_profile_input_gate_allows(L4C_PROFILE_REQ_LOW, L4C_PROFILE_720P)) return 3;
    if (l4c_profile_input_gate_allows(99, L4C_PROFILE_480P)) return 4;
    if (l4c_profile_parse_request(99, &req)) return 5; /* unknown не становится low */
    if (!l4c_profile_parse_request(L4C_PROFILE_REQ_LOW, &req) || req != L4C_PROFILE_REQ_LOW) return 6;
    if (!l4c_profile_parse_request(L4C_PROFILE_REQ_DEFAULT, &req) || req != L4C_PROFILE_REQ_DEFAULT) return 7;
    if (!l4c_profile_parse_request(2, &req) || req != L4C_PROFILE_REQ_DEFAULT) return 8;
    return 0;
}

int test_overload_detector_two_windows(void) {
    l4c_degrade_controller_t ctl;
    l4c_degrade_window_t bad = make_bad_raw_window();
    l4c_degrade_action_t act;
    if (l4c_degrade_init(&ctl, L4C_PROFILE_720P, 15, 0) != L4C_OK) return 1;
    act = l4c_degrade_on_window(&ctl, 3000, &bad);
    if (act != L4C_DEG_ACT_NONE) return 2; /* одно окно — нет действия */
    act = l4c_degrade_on_window(&ctl, 6000, &bad);
    if (act != L4C_DEG_ACT_D0_DROP_LATE_RAW) return 3;
    return 0;
}

int test_degrade_ladder_monotonic_single_step(void) {
    l4c_degrade_controller_t ctl;
    l4c_degrade_window_t bad = make_bad_raw_window();
    /* Эталон: 720p/15 t=0 → D0@6 → D1@12 → D2@18 → D3@24 */
    if (l4c_degrade_init(&ctl, L4C_PROFILE_720P, 15, 0) != L4C_OK) return 1;
    if (l4c_degrade_on_window(&ctl, 3000, &bad) != L4C_DEG_ACT_NONE) return 2;
    if (l4c_degrade_on_window(&ctl, 6000, &bad) != L4C_DEG_ACT_D0_DROP_LATE_RAW) return 3;
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_720P, 15, L4C_NOMINAL, 1, 6000);
    /* hold-off до 12000: окна 9k и 12k */
    if (l4c_degrade_on_window(&ctl, 9000, &bad) != L4C_DEG_ACT_NONE) return 4;
    if (l4c_degrade_on_window(&ctl, 12000, &bad) != L4C_DEG_ACT_D1_FPS_THROTTLE) return 5;
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_720P, 10, L4C_FPS_THROTTLED, 1, 12000);
    if (l4c_degrade_on_window(&ctl, 15000, &bad) != L4C_DEG_ACT_NONE) return 6;
    if (l4c_degrade_on_window(&ctl, 18000, &bad) != L4C_DEG_ACT_D2_RASTER_540P) return 7;
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_540P, 10, L4C_DEGRADED_540P, 2, 18000);
    if (l4c_degrade_on_window(&ctl, 21000, &bad) != L4C_DEG_ACT_NONE) return 8;
    if (l4c_degrade_on_window(&ctl, 24000, &bad) != L4C_DEG_ACT_D3_RASTER_480P) return 9;
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_480P, 10, L4C_DEGRADED_480P, 3, 24000);
    if (ctl.actual_id == L4C_PROFILE_720P) return 10; /* монотонность */
    return 0;
}

int test_no_oscillation_upgrade_forbidden(void) {
    l4c_degrade_controller_t ctl;
    l4c_degrade_window_t good = make_good_window();
    l4c_degrade_window_t bad = make_bad_raw_window();
    int i;
    if (l4c_degrade_init(&ctl, L4C_PROFILE_720P, 15, 0) != L4C_OK) return 1;
    l4c_degrade_on_window(&ctl, 3000, &bad);
    l4c_degrade_on_window(&ctl, 6000, &bad); /* D0 */
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_720P, 15, L4C_NOMINAL, 1, 6000);
    l4c_degrade_on_window(&ctl, 9000, &bad);
    l4c_degrade_on_window(&ctl, 12000, &bad); /* D1 */
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_720P, 10, L4C_FPS_THROTTLED, 1, 12000);
    l4c_degrade_on_window(&ctl, 15000, &bad);
    l4c_degrade_on_window(&ctl, 18000, &bad); /* D2 */
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_540P, 10, L4C_DEGRADED_540P, 2, 18000);
    /* «успокоение» */
    for (i = 0; i < 6; ++i) {
        uint64_t end = 18000 + (uint64_t)(i + 1) * L4C_DEGRADE_WINDOW_MS;
        if (l4c_degrade_on_window(&ctl, end, &good) != L4C_DEG_ACT_NONE) return 2;
    }
    if (ctl.actual_id != L4C_PROFILE_540P) return 3;
    if (ctl.current_fps > 10) return 4;
    return 0;
}

int test_raster_down_fps_not_up(void) {
    l4c_degrade_controller_t ctl;
    l4c_degrade_window_t bad = make_bad_raw_window();
    if (l4c_degrade_init(&ctl, L4C_PROFILE_720P, 15, 0) != L4C_OK) return 1;
    l4c_degrade_on_window(&ctl, 3000, &bad);
    l4c_degrade_on_window(&ctl, 6000, &bad);
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_720P, 15, L4C_NOMINAL, 1, 6000);
    l4c_degrade_on_window(&ctl, 9000, &bad);
    if (l4c_degrade_on_window(&ctl, 12000, &bad) != L4C_DEG_ACT_D1_FPS_THROTTLE) return 2;
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_720P, 10, L4C_FPS_THROTTLED, 1, 12000);
    l4c_degrade_on_window(&ctl, 15000, &bad);
    if (l4c_degrade_on_window(&ctl, 18000, &bad) != L4C_DEG_ACT_D2_RASTER_540P) return 3;
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_540P, 10, L4C_DEGRADED_540P, 2, 18000);
    if (ctl.current_fps != 10) return 4; /* FPS не поднялся до 15 */
    return 0;
}

int test_config_change_clears_pending_and_first_idr(void) {
    /* Pure controller: generation bump + state commit; pipeline transaction is integration.
       D0/D1 pacing-only не требует reinit — фиксируем отсутствие смены actual. */
    l4c_degrade_controller_t ctl;
    if (l4c_degrade_init(&ctl, L4C_PROFILE_720P, 15, 0) != L4C_OK) return 1;
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_720P, 15, L4C_NOMINAL, 2, 100);
    if (ctl.config_generation != 2) return 2;
    if (ctl.actual_id != L4C_PROFILE_720P) return 3;
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_540P, 10, L4C_DEGRADED_540P, 3, 200);
    if (ctl.actual_id != L4C_PROFILE_540P || ctl.config_generation != 3) return 4;
    /* После смены — новые окна (история обнулена). */
    if (ctl.consecutive_bad != 0) return 5;
    return 0;
}

int test_stop_on_persistent_overload_at_480p(void) {
    l4c_degrade_controller_t ctl;
    l4c_degrade_window_t bad = make_bad_raw_window();
    l4c_degrade_window_t good = make_good_window();
    int i;
    l4c_degrade_action_t act;
    /* Старт 480p/10: D0@6 → stop@21 */
    if (l4c_degrade_init(&ctl, L4C_PROFILE_480P, 10, 0) != L4C_OK) return 1;
    if (l4c_degrade_on_window(&ctl, 3000, &bad) != L4C_DEG_ACT_NONE) return 2;
    act = l4c_degrade_on_window(&ctl, 6000, &bad);
    if (act != L4C_DEG_ACT_D0_DROP_LATE_RAW) return 3;
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_480P, 10, L4C_NOMINAL, 1, 6000);
    /* floor streak: 5 bad окон, stop на 21000; на 18000 (4 окна) ещё нет */
    for (i = 0; i < 4; ++i) {
        uint64_t end = 6000 + (uint64_t)(i + 1) * L4C_DEGRADE_WINDOW_MS;
        act = l4c_degrade_on_window(&ctl, end, &bad);
        if (act == L4C_DEG_ACT_STOP_HIGH_LOAD) return 4;
    }
    act = l4c_degrade_on_window(&ctl, 21000, &bad);
    if (act != L4C_DEG_ACT_STOP_HIGH_LOAD) return 5;

    /* good разрывает streak */
    if (l4c_degrade_init(&ctl, L4C_PROFILE_480P, 10, 0) != L4C_OK) return 6;
    l4c_degrade_on_window(&ctl, 3000, &bad);
    l4c_degrade_on_window(&ctl, 6000, &bad);
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_480P, 10, L4C_NOMINAL, 1, 6000);
    l4c_degrade_on_window(&ctl, 9000, &bad);
    l4c_degrade_on_window(&ctl, 12000, &bad);
    l4c_degrade_on_window(&ctl, 15000, &bad);
    if (l4c_degrade_on_window(&ctl, 18000, &good) != L4C_DEG_ACT_NONE) return 7;
    act = l4c_degrade_on_window(&ctl, 21000, &bad);
    if (act == L4C_DEG_ACT_STOP_HIGH_LOAD) return 8; /* streak сброшен */
    return 0;
}

int test_bitrate_cut_requires_delivery_or_measured_exceed(void) {
    l4c_bitrate_cut_input_t in;
    memset(&in, 0, sizeof(in));
    in.current_target_kbps = 800;
    in.min_kbps = 600;
    /* CPU overload без feedback/exceed — не режем */
    in.has_full_10s_window = true;
    in.measured_exceeds_max = false;
    in.delivery_constraint_confirmed = false;
    if (l4c_degrade_should_cut_bitrate(&in)) return 1;
    /* measured exceed — можно */
    in.measured_exceeds_max = true;
    if (!l4c_degrade_should_cut_bitrate(&in)) return 2;
    /* delivery constraint — можно */
    in.measured_exceeds_max = false;
    in.delivery_constraint_confirmed = true;
    if (!l4c_degrade_should_cut_bitrate(&in)) return 3;
    /* неполное окно без delivery — нельзя */
    in.delivery_constraint_confirmed = false;
    in.has_full_10s_window = false;
    in.measured_exceeds_max = true;
    if (l4c_degrade_should_cut_bitrate(&in)) return 4;
    /* уже min — no-op */
    in.current_target_kbps = in.min_kbps;
    in.delivery_constraint_confirmed = true;
    if (l4c_degrade_should_cut_bitrate(&in)) return 5;
    return 0;
}

int test_degrade_does_not_break_input_gate(void) {
    l4c_degrade_controller_t ctl;
    l4c_degrade_window_t bad = make_bad_raw_window();
    /* Вся лестница default до 480p не даёт input */
    if (l4c_degrade_init(&ctl, L4C_PROFILE_720P, 15, 0) != L4C_OK) return 1;
    l4c_degrade_on_window(&ctl, 3000, &bad);
    l4c_degrade_on_window(&ctl, 6000, &bad);
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_720P, 15, L4C_NOMINAL, 1, 6000);
    l4c_degrade_on_window(&ctl, 9000, &bad);
    l4c_degrade_on_window(&ctl, 12000, &bad);
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_720P, 10, L4C_FPS_THROTTLED, 1, 12000);
    l4c_degrade_on_window(&ctl, 15000, &bad);
    l4c_degrade_on_window(&ctl, 18000, &bad);
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_540P, 10, L4C_DEGRADED_540P, 2, 18000);
    l4c_degrade_on_window(&ctl, 21000, &bad);
    l4c_degrade_on_window(&ctl, 24000, &bad);
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_480P, 10, L4C_DEGRADED_480P, 3, 24000);
    if (l4c_profile_input_gate_allows(L4C_PROFILE_REQ_DEFAULT, ctl.actual_id)) return 2;
    return 0;
}

int test_safety_stop_latency_preserved_under_degrade(void) {
    /* Контроллер не держит блокирующих ожиданий: tick/window — чистые вызовы.
       Интеграционный stop latency проверяется на стенде; здесь — отсутствие зависаний API. */
    l4c_degrade_controller_t ctl;
    l4c_degrade_window_t bad = make_bad_raw_window();
    if (l4c_degrade_init(&ctl, L4C_PROFILE_720P, 15, 0) != L4C_OK) return 1;
    l4c_degrade_on_window(&ctl, 3000, &bad);
    l4c_degrade_on_window(&ctl, 6000, &bad);
    /* Stop/expiry отменяет переход: pipeline игнорирует pending action после stop.
       Здесь фиксируем, что контроллер не копит очередь действий. */
    if (ctl.holdoff_active && ctl.holdoff_until_ms != 6000 + L4C_DEGRADE_HOLD_OFF_MS) return 2;
    return 0;
}

int test_no_oscillation_100_cycles_forced_overload(void) {
    int c;
    for (c = 0; c < 100; ++c) {
        l4c_degrade_controller_t ctl;
        l4c_degrade_window_t bad = make_bad_raw_window();
        uint16_t prev_fps;
        if (l4c_degrade_init(&ctl, L4C_PROFILE_720P, 15, 0) != L4C_OK) return 1;
        l4c_degrade_on_window(&ctl, 3000, &bad);
        l4c_degrade_on_window(&ctl, 6000, &bad);
        l4c_degrade_notify_applied(&ctl, L4C_PROFILE_720P, 15, L4C_NOMINAL, 1, 6000);
        l4c_degrade_on_window(&ctl, 9000, &bad);
        l4c_degrade_on_window(&ctl, 12000, &bad);
        l4c_degrade_notify_applied(&ctl, L4C_PROFILE_720P, 10, L4C_FPS_THROTTLED, 1, 12000);
        prev_fps = ctl.current_fps;
        l4c_degrade_on_window(&ctl, 15000, &bad);
        l4c_degrade_on_window(&ctl, 18000, &bad);
        l4c_degrade_notify_applied(&ctl, L4C_PROFILE_540P, 10, L4C_DEGRADED_540P, 2, 18000);
        if (ctl.current_fps > prev_fps) return 2;
    }
    return 0;
}

/* --- Границы детектора --- */
int test_detector_threshold_boundaries(void) {
    l4c_drop_class_t exact, over, empty;
    l4c_degrade_window_t w;
    exact.passed = 8; exact.dropped = 2;
    over.passed = 7; over.dropped = 3;
    empty.passed = 0; empty.dropped = 0;
    if (l4c_degrade_class_over_threshold(exact)) return 1; /* 20% — не превышение */
    if (!l4c_degrade_class_over_threshold(over)) return 2;
    if (l4c_degrade_class_over_threshold(empty)) return 3;
    /* p95 == interval — не bad; > interval — bad */
    memset(&w, 0, sizeof(w));
    w.raw.passed = 10;
    w.has_processing = true;
    w.processing_samples = 10;
    w.processing_p95_ms = 100; /* 100 == 1000/10 */
    {
        l4c_degrade_controller_t ctl;
        l4c_degrade_init(&ctl, L4C_PROFILE_480P, 10, 0);
        l4c_degrade_on_window(&ctl, 3000, &w);
        l4c_degrade_on_window(&ctl, 6000, &w);
            if (ctl.d0_done) return 4; /* ровно interval — не bad */
    }
    w.processing_p95_ms = 101;
    {
        l4c_degrade_controller_t ctl;
        l4c_degrade_init(&ctl, L4C_PROFILE_480P, 10, 0);
        l4c_degrade_on_window(&ctl, 3000, &w);
        l4c_degrade_on_window(&ctl, 6000, &w);
        if (!ctl.d0_done) return 5;
    }
    return 0;
}

int test_detector_classes_independent(void) {
    l4c_degrade_window_t w;
    l4c_drop_class_t c;
    c.passed = 8; c.dropped = 2;
    if (l4c_degrade_class_over_threshold(c)) return 1;
    memset(&w, 0, sizeof(w));
    w.raw.passed = 100; w.raw.dropped = 0;
    w.encoder.passed = 1; w.encoder.dropped = 4; /* bad encoder */
    w.transport.passed = 100;
    {
        l4c_degrade_controller_t ctl;
        l4c_degrade_init(&ctl, L4C_PROFILE_480P, 10, 0);
        l4c_degrade_on_window(&ctl, 3000, &w);
        l4c_degrade_on_window(&ctl, 6000, &w);
        if (!ctl.d0_done) return 2;
    }
    return 0;
}

int test_p95_from_samples(void) {
    uint32_t samples[10];
    uint32_t p95 = 0;
    int i;
    for (i = 0; i < 10; ++i) samples[i] = (uint32_t)(i + 1); /* 1..10 */
    if (!l4c_degrade_p95_from_samples(samples, 10, &p95)) return 1;
    /* ceil(0.95*10)=10 → 10-й элемент = 10 */
    if (p95 != 10) return 2;
    if (l4c_degrade_p95_from_samples(samples, 0, &p95)) return 3;
    return 0;
}

int test_window_boundary_rejects_partial(void) {
    l4c_degrade_controller_t ctl;
    l4c_degrade_window_t bad = make_bad_raw_window();
    if (l4c_degrade_init(&ctl, L4C_PROFILE_720P, 15, 0) != L4C_OK) return 1;
    /* 2999 мс — не окно */
    if (l4c_degrade_on_window(&ctl, 2999, &bad) != L4C_DEG_ACT_NONE) return 2;
    if (ctl.d0_done) return 3;
    /* 3000 — окно */
    l4c_degrade_on_window(&ctl, 3000, &bad);
    /* 5999 — не полное следующее */
    if (l4c_degrade_on_window(&ctl, 5999, &bad) != L4C_DEG_ACT_NONE) return 4;
    return 0;
}

int test_holdoff_blocks_second_action(void) {
    l4c_degrade_controller_t ctl;
    l4c_degrade_window_t bad = make_bad_raw_window();
    if (l4c_degrade_init(&ctl, L4C_PROFILE_720P, 15, 0) != L4C_OK) return 1;
    l4c_degrade_on_window(&ctl, 3000, &bad);
    if (l4c_degrade_on_window(&ctl, 6000, &bad) != L4C_DEG_ACT_D0_DROP_LATE_RAW) return 2;
    /* hold-off до 12000: даже 2 bad-окна не дают D1 на 9000 */
    if (l4c_degrade_on_window(&ctl, 9000, &bad) != L4C_DEG_ACT_NONE) return 3;
    /* 12000 — hold-off истёк */
    if (l4c_degrade_on_window(&ctl, 12000, &bad) != L4C_DEG_ACT_D1_FPS_THROTTLE) return 4;
    return 0;
}

int test_start_720p_10_skips_d1(void) {
    l4c_degrade_controller_t ctl;
    l4c_degrade_window_t bad = make_bad_raw_window();
    if (l4c_degrade_init(&ctl, L4C_PROFILE_720P, 10, 0) != L4C_OK) return 1;
    l4c_degrade_on_window(&ctl, 3000, &bad);
    if (l4c_degrade_on_window(&ctl, 6000, &bad) != L4C_DEG_ACT_D0_DROP_LATE_RAW) return 2;
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_720P, 10, L4C_NOMINAL, 1, 6000);
    /* D1 пропускается — сразу D2 после hold-off */
    l4c_degrade_on_window(&ctl, 9000, &bad);
    if (l4c_degrade_on_window(&ctl, 12000, &bad) != L4C_DEG_ACT_D2_RASTER_540P) return 3;
    return 0;
}

int test_nodata_resets_streaks(void) {
    l4c_degrade_controller_t ctl;
    l4c_degrade_window_t bad = make_bad_raw_window();
    l4c_degrade_window_t nodata;
    memset(&nodata, 0, sizeof(nodata));
    if (l4c_degrade_init(&ctl, L4C_PROFILE_480P, 10, 0) != L4C_OK) return 1;
    l4c_degrade_on_window(&ctl, 3000, &bad);
    l4c_degrade_on_window(&ctl, 6000, &bad);
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_480P, 10, L4C_NOMINAL, 1, 6000);
    l4c_degrade_on_window(&ctl, 9000, &bad);
    l4c_degrade_on_window(&ctl, 12000, &bad);
    l4c_degrade_on_window(&ctl, 15000, &bad);
    if (l4c_degrade_on_window(&ctl, 18000, &nodata) != L4C_DEG_ACT_NONE) return 2;
    if (ctl.floor_bad_streak != 0) return 3;
    return 0;
}

int test_reference_timeline_480p_stop_21s(void) {
    l4c_degrade_controller_t ctl;
    l4c_degrade_window_t bad = make_bad_raw_window();
    l4c_degrade_action_t a;
    if (l4c_degrade_init(&ctl, L4C_PROFILE_480P, 10, 0) != L4C_OK) return 1;
    a = l4c_degrade_on_window(&ctl, 3000, &bad);
    if (a != L4C_DEG_ACT_NONE) return 2;
    a = l4c_degrade_on_window(&ctl, 6000, &bad);
    if (a != L4C_DEG_ACT_D0_DROP_LATE_RAW) return 3;
    l4c_degrade_notify_applied(&ctl, L4C_PROFILE_480P, 10, L4C_NOMINAL, 1, 6000);
    a = l4c_degrade_on_window(&ctl, 9000, &bad);  if (a != L4C_DEG_ACT_NONE) return 4;
    a = l4c_degrade_on_window(&ctl, 12000, &bad); if (a != L4C_DEG_ACT_NONE) return 5;
    a = l4c_degrade_on_window(&ctl, 15000, &bad); if (a != L4C_DEG_ACT_NONE) return 6;
    a = l4c_degrade_on_window(&ctl, 18000, &bad); if (a != L4C_DEG_ACT_NONE) return 7;
    a = l4c_degrade_on_window(&ctl, 21000, &bad);
    if (a != L4C_DEG_ACT_STOP_HIGH_LOAD) return 8;
    return 0;
}
