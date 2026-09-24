#ifndef L4C_DEGRADE_CONTROLLER_H
#define L4C_DEGRADE_CONTROLLER_H
#include "types.h"
#include "telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

#define L4C_DEGRADE_WINDOW_MS           3000u
#define L4C_DEGRADE_HOLD_OFF_MS         6000u
#define L4C_DEGRADE_FLOOR_BAD_WINDOWS   5u    /* 5 * 3s = 15s на 480p */
#define L4C_DEGRADE_DROP_THRESHOLD_PCT  20u   /* строго > 20% */
#define L4C_DEGRADE_MAX_P95_SAMPLES     256u
/* p95 при малой выборке = max и ложно валит окно одиночным IDR.
 * n>=20: ceil(0.95*n) < n — один выброс не попадает в p95.
 * Ниже порога p95-критерий не применяется (редкие апдейты != перегрузка). */
#define L4C_DEGRADE_P95_MIN_SAMPLES     20u

typedef enum {
    L4C_DEG_ACT_NONE = 0,
    L4C_DEG_ACT_D0_DROP_LATE_RAW = 1,
    L4C_DEG_ACT_D1_FPS_THROTTLE = 2,
    L4C_DEG_ACT_D2_RASTER_540P = 3,
    L4C_DEG_ACT_D3_RASTER_480P = 4,
    L4C_DEG_ACT_STOP_HIGH_LOAD = 5
} l4c_degrade_action_t;

typedef struct {
    uint32_t passed;
    uint32_t dropped;
} l4c_drop_class_t;

/* Полный неперекрывающийся интервал 3000 мс одной активной конфигурации. */
typedef struct {
    l4c_drop_class_t raw;
    l4c_drop_class_t encoder;
    l4c_drop_class_t transport; /* единица — AU */
    bool has_processing;
    uint32_t processing_p95_ms;
    uint32_t processing_samples;
    /* Сырые выборки encode-времён; p95 считается при закрытии окна. */
    uint32_t processing_ms[L4C_DEGRADE_MAX_P95_SAMPLES];
} l4c_degrade_window_t;

typedef struct {
    /* Действующая конфигурация (меняется pipeline при D1/D2/D3). */
    uint16_t actual_id;
    uint8_t current_fps;
    uint8_t start_fps;
    uint16_t degrade_state; /* L4C_NOMINAL / FPS_THROTTLED / DEGRADED_* */

    /* Лестница: внутренние признаки отделены от диагностического enum. */
    uint16_t start_actual_id;
    bool d0_done;
    bool d1_done;
    bool d2_done;
    bool d3_done;
    uint32_t config_generation;

    /* Окна детектора */
    uint64_t window_start_ms;
    bool window_open;
    uint8_t consecutive_bad;
    bool floor_counting;   /* на 480p после D0/D3 */
    uint8_t floor_bad_streak;

    /* Hold-off после успешного действия */
    uint64_t holdoff_until_ms;
    bool holdoff_active;

    /* Срез битрейта — отдельное действие, не D0..D3 */
    bool bitrate_cut_done;
    uint64_t bitrate_holdoff_until_ms;
    bool bitrate_holdoff_active;
} l4c_degrade_controller_t;

/* Инициализация после resolve профиля; 540p на старте запрещён. */
l4c_status_t l4c_degrade_init(
    l4c_degrade_controller_t *ctl,
    uint16_t actual_id,
    uint8_t start_fps,
    uint64_t now_ms);

/*
 * Закрыть полное окно [window_start, window_end) с агрегатами классов.
 * window_end_ms - window_start_ms обязано быть 3000 (или контроллер отклонит окно).
 * Возвращает одно действие либо NONE. После действия история обнулена, hold-off 6000 мс.
 */
l4c_degrade_action_t l4c_degrade_on_window(
    l4c_degrade_controller_t *ctl,
    uint64_t window_end_ms,
    const l4c_degrade_window_t *wm);

/* Явное применение ступени pipeline'ом (после успешного D1/D2/D3). */
void l4c_degrade_notify_applied(
    l4c_degrade_controller_t *ctl,
    uint16_t new_actual_id,
    uint8_t new_fps,
    uint16_t new_state,
    uint32_t new_config_generation,
    uint64_t now_ms);

/* true: 20% не является превышением; >20% — является. 0/0 — no-data. */
bool l4c_degrade_class_over_threshold(l4c_drop_class_t c);

/* p95: элемент с рангом ceil(0.95*N) (1-based) в упорядоченной выборке. N=0 — false. */
bool l4c_degrade_p95_from_samples(const uint32_t *samples_ms, uint32_t count, uint32_t *out_p95_ms);

/* Срез bitrate к min: только measured exceed (полное 10с окно) либо delivery constraint. */
typedef struct {
    bool has_full_10s_window;
    bool measured_exceeds_max;
    bool delivery_constraint_confirmed;
    uint16_t current_target_kbps;
    uint16_t min_kbps;
} l4c_bitrate_cut_input_t;

bool l4c_degrade_should_cut_bitrate(const l4c_bitrate_cut_input_t *in);

#ifdef __cplusplus
}
#endif
#endif /* L4C_DEGRADE_CONTROLLER_H */
