#include <string.h>
#include "l4capture/degrade_controller.h"
#include "l4capture/video_profile.h"

static void reset_window_history(l4c_degrade_controller_t *ctl, uint64_t now_ms) {
    ctl->window_start_ms = now_ms;
    ctl->window_open = true;
    ctl->consecutive_bad = 0;
}

static void start_holdoff(l4c_degrade_controller_t *ctl, uint64_t now_ms) {
    ctl->holdoff_active = true;
    ctl->holdoff_until_ms = now_ms + L4C_DEGRADE_HOLD_OFF_MS;
}

static bool holdoff_blocks(const l4c_degrade_controller_t *ctl, uint64_t now_ms) {
    return ctl->holdoff_active && now_ms < ctl->holdoff_until_ms;
}

bool l4c_degrade_class_over_threshold(l4c_drop_class_t c) {
    uint64_t total;
    if (c.passed == 0 && c.dropped == 0) return false;
    total = (uint64_t)c.passed + (uint64_t)c.dropped;
    if (total == 0) return false;
    /* строго > 20%  <=>  dropped * 5 > total */
    return ((uint64_t)c.dropped * 5u) > total;
}

static bool window_is_nodata(const l4c_degrade_window_t *wm) {
    return wm->raw.passed == 0 && wm->raw.dropped == 0 &&
           wm->encoder.passed == 0 && wm->encoder.dropped == 0 &&
           wm->transport.passed == 0 && wm->transport.dropped == 0 &&
           !(wm->has_processing && wm->processing_samples > 0);
}

static bool window_is_bad(const l4c_degrade_window_t *wm, uint8_t current_fps) {
    if (l4c_degrade_class_over_threshold(wm->raw) ||
        l4c_degrade_class_over_threshold(wm->encoder) ||
        l4c_degrade_class_over_threshold(wm->transport)) {
        return true;
    }
    if (wm->has_processing && wm->processing_samples > 0 && current_fps > 0) {
        /* p95 > 1000/fps  <=>  p95 * fps > 1000 (без усечения 15 FPS до 66 мс) */
        if ((uint64_t)wm->processing_p95_ms * (uint64_t)current_fps > 1000u) {
            return true;
        }
    }
    return false;
}

bool l4c_degrade_p95_from_samples(const uint32_t *samples_ms, uint32_t count, uint32_t *out_p95_ms) {
    uint32_t sorted[L4C_DEGRADE_MAX_P95_SAMPLES];
    uint32_t i, j, rank, n;
    if (!samples_ms || !out_p95_ms || count == 0 || count > L4C_DEGRADE_MAX_P95_SAMPLES) {
        return false;
    }
    n = count;
    memcpy(sorted, samples_ms, n * sizeof(uint32_t));
    for (i = 1; i < n; ++i) {
        uint32_t key = sorted[i];
        j = i;
        while (j > 0 && sorted[j - 1] > key) {
            sorted[j] = sorted[j - 1];
            --j;
        }
        sorted[j] = key;
    }
    rank = (n * 95u + 99u) / 100u; /* ceil(0.95*N) */
    if (rank == 0) rank = 1;
    if (rank > n) rank = n;
    *out_p95_ms = sorted[rank - 1];
    return true;
}

bool l4c_degrade_should_cut_bitrate(const l4c_bitrate_cut_input_t *in) {
    if (!in) return false;
    if (in->current_target_kbps <= in->min_kbps) return false;
    if (in->delivery_constraint_confirmed) return true;
    if (in->has_full_10s_window && in->measured_exceeds_max) return true;
    return false;
}

l4c_status_t l4c_degrade_init(
    l4c_degrade_controller_t *ctl,
    uint16_t actual_id,
    uint8_t start_fps,
    uint64_t now_ms)
{
    const l4c_profile_params_t *p;
    if (!ctl || start_fps == 0) return L4C_ERR_INVALID_ARG;
    p = l4c_profile_params(actual_id);
    if (!p) return L4C_ERR_INVALID_ARG;
    if (actual_id == L4C_PROFILE_540P) return L4C_ERR_INVALID_ARG;
    memset(ctl, 0, sizeof(*ctl));
    ctl->actual_id = actual_id;
    ctl->start_actual_id = actual_id;
    ctl->current_fps = start_fps;
    ctl->start_fps = start_fps;
    ctl->degrade_state = L4C_NOMINAL;
    /* 10 FPS: D1 неприменим, фиксируем без фиктивного действия. */
    ctl->d1_done = (start_fps <= 10);
    if (actual_id != L4C_PROFILE_720P) {
        ctl->d2_done = true;
        ctl->d3_done = true;
    }
    if (actual_id == L4C_PROFILE_480P) {
        ctl->floor_counting = true;
        ctl->floor_bad_streak = 0;
    }
    ctl->config_generation = 1;
    reset_window_history(ctl, now_ms);
    return L4C_OK;
}

void l4c_degrade_notify_applied(
    l4c_degrade_controller_t *ctl,
    uint16_t new_actual_id,
    uint8_t new_fps,
    uint16_t new_state,
    uint32_t new_config_generation,
    uint64_t now_ms)
{
    if (!ctl) return;
    if (new_fps > ctl->current_fps) {
        /* Монотонность: FPS не повышается в сессии. */
        new_fps = ctl->current_fps;
    }
    ctl->actual_id = new_actual_id;
    ctl->current_fps = new_fps;
    ctl->degrade_state = new_state;
    ctl->config_generation = new_config_generation;
    if (new_fps <= 10) ctl->d1_done = true;
    if (new_actual_id == L4C_PROFILE_480P) {
        if (!ctl->floor_counting) {
            ctl->floor_counting = true;
            ctl->floor_bad_streak = 0;
        }
    }
    /* Смена конфигурации: новые окна, без наследия старых метрик. */
    ctl->consecutive_bad = 0;
    reset_window_history(ctl, now_ms);
}

static l4c_degrade_action_t choose_ladder_step(l4c_degrade_controller_t *ctl) {
    if (!ctl->d0_done) {
        return L4C_DEG_ACT_D0_DROP_LATE_RAW;
    }
    if (!ctl->d1_done) {
        if (ctl->current_fps > 10 || ctl->start_fps > 10) {
            return L4C_DEG_ACT_D1_FPS_THROTTLE;
        }
        ctl->d1_done = true;
    }
    if (!ctl->d2_done) {
        if (ctl->actual_id == L4C_PROFILE_720P) {
            return L4C_DEG_ACT_D2_RASTER_540P;
        }
        ctl->d2_done = true;
    }
    if (!ctl->d3_done) {
        if (ctl->actual_id == L4C_PROFILE_540P) {
            return L4C_DEG_ACT_D3_RASTER_480P;
        }
        ctl->d3_done = true;
    }
    return L4C_DEG_ACT_NONE;
}

static void mark_action_taken(l4c_degrade_controller_t *ctl, l4c_degrade_action_t act, uint64_t end_ms) {
    /* Только однократность ступени и hold-off. actual/FPS/state — через notify_applied. */
    switch (act) {
    case L4C_DEG_ACT_D0_DROP_LATE_RAW:
        ctl->d0_done = true;
        /* Floor-overload: 5 окон ПОСЛЕ D0 (15 с), не с начала сессии. */
        ctl->floor_bad_streak = 0;
        if (ctl->actual_id == L4C_PROFILE_480P) ctl->floor_counting = true;
        break;
    case L4C_DEG_ACT_D1_FPS_THROTTLE:
        ctl->d1_done = true;
        break;
    case L4C_DEG_ACT_D2_RASTER_540P:
        ctl->d2_done = true;
        break;
    case L4C_DEG_ACT_D3_RASTER_480P:
        ctl->d3_done = true;
        ctl->floor_counting = true;
        ctl->floor_bad_streak = 0;
        break;
    default:
        break;
    }
    reset_window_history(ctl, end_ms);
    start_holdoff(ctl, end_ms);
}

l4c_degrade_action_t l4c_degrade_on_window(
    l4c_degrade_controller_t *ctl,
    uint64_t window_end_ms,
    const l4c_degrade_window_t *wm)
{
    bool bad;
    l4c_degrade_action_t act;

    if (!ctl || !wm) return L4C_DEG_ACT_NONE;
    if (!ctl->window_open) {
        if (window_end_ms >= L4C_DEGRADE_WINDOW_MS) {
            reset_window_history(ctl, window_end_ms - L4C_DEGRADE_WINDOW_MS);
        } else {
            reset_window_history(ctl, 0);
        }
    }
    if (window_end_ms < ctl->window_start_ms) return L4C_DEG_ACT_NONE;
    if ((window_end_ms - ctl->window_start_ms) != L4C_DEGRADE_WINDOW_MS) {
        return L4C_DEG_ACT_NONE;
    }

    if (window_is_nodata(wm)) {
        ctl->consecutive_bad = 0;
        ctl->floor_bad_streak = 0;
        reset_window_history(ctl, window_end_ms);
        return L4C_DEG_ACT_NONE;
    }

    bad = window_is_bad(wm, ctl->current_fps);
    if (bad) {
        if (ctl->consecutive_bad < 255) ctl->consecutive_bad++;
        if (ctl->floor_counting && ctl->floor_bad_streak < 255) ctl->floor_bad_streak++;
    } else {
        ctl->consecutive_bad = 0;
        ctl->floor_bad_streak = 0;
    }

    /* Floor STOP: только после D0 (старт 480p) либо D3 (спуск). 5 полных bad-окон. */
    if (ctl->floor_counting && (ctl->d0_done || ctl->d3_done) &&
        ctl->floor_bad_streak >= L4C_DEGRADE_FLOOR_BAD_WINDOWS) {
        reset_window_history(ctl, window_end_ms);
        return L4C_DEG_ACT_STOP_HIGH_LOAD;
    }

    if (ctl->consecutive_bad >= 2 && !holdoff_blocks(ctl, window_end_ms)) {
        act = choose_ladder_step(ctl);
        if (act != L4C_DEG_ACT_NONE) {
            mark_action_taken(ctl, act, window_end_ms);
            return act;
        }
    }

    ctl->window_start_ms = window_end_ms;
    return L4C_DEG_ACT_NONE;
}
