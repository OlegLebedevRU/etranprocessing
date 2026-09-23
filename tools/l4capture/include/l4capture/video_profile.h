#ifndef L4C_VIDEO_PROFILE_H
#define L4C_VIDEO_PROFILE_H
#include "types.h"
#include "telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wire profile_id из CMD_START (l4c_start_t.profile_id). Неизвестные значения не приводятся к low. */
enum {
    L4C_PROFILE_REQ_LOW = 1,
    L4C_PROFILE_REQ_DEFAULT = 2
};

/* Пригодность профиля для ввода — не авторизация. Gate принадлежит l4desk (L4C-05). */
typedef struct {
    uint16_t id;                 /* L4C_PROFILE_480P / 540P / 720P */
    uint16_t width, height;      /* видимый растр */
    uint8_t fps_nominal;         /* 10 или 15 */
    uint8_t fps_floor;           /* 10 */
    uint16_t bitrate_min_kbps;
    uint16_t bitrate_target_kbps;
    uint16_t bitrate_max_kbps;
    bool input_profile_eligible; /* true только base_480p */
} l4c_profile_params_t;

typedef struct {
    uint16_t actual_id;
    uint8_t start_fps;
    bool refused_premium;  /* default ограничен до 480p проверкой конфигурации */
    bool win7_legacy;
} l4c_profile_resolved_t;

/* NULL для неизвестного id. */
const l4c_profile_params_t *l4c_profile_params(uint16_t actual_id);

/* Парсер requested. false для неизвестного значения (не маппится в low). */
bool l4c_profile_parse_request(uint16_t wire_profile_id, uint8_t *out_request);

/*
 * Матрица старта §4.1:
 *  low                -> 480p / 10
 *  default + Win7     -> 480p / 10
 *  default + MFT 720p -> 720p / 15
 *  default + OH264 720p verified -> 720p / 10
 *  default + 720p unsupported -> 480p / 10 (refused_premium) либо отказ вызывающим init
 * 540p на старте не выбирается.
 */
l4c_status_t l4c_profile_resolve(
    uint8_t request,
    bool win7_or_legacy,
    bool hardware_mft_720p,
    bool openh264_720p_verified,
    l4c_profile_resolved_t *out);

/*
 * Чистый helper пары: true только для (LOW, base_480p).
 * Не заменяет внешний input-gate и не авторизует ввод.
 */
bool l4c_profile_input_gate_allows(uint8_t request, uint16_t actual_id);

#ifdef __cplusplus
}
#endif
#endif /* L4C_VIDEO_PROFILE_H */
