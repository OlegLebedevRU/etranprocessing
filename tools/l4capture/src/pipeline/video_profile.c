#include <string.h>
#include "l4capture/video_profile.h"

static const l4c_profile_params_t k_profiles[] = {
    { L4C_PROFILE_480P,  854,  480, 10, 10, 500,  500,  700, true  },
    { L4C_PROFILE_540P,  960,  540, 15, 10, 600,  700,  900, false },
    { L4C_PROFILE_720P, 1280,  720, 15, 10, 600,  800, 1000, false }
};

const l4c_profile_params_t *l4c_profile_params(uint16_t actual_id) {
    size_t i;
    for (i = 0; i < sizeof(k_profiles) / sizeof(k_profiles[0]); ++i) {
        if (k_profiles[i].id == actual_id) return &k_profiles[i];
    }
    return NULL;
}

bool l4c_profile_parse_request(uint16_t wire_profile_id, uint8_t *out_request) {
    if (!out_request) return false;
    if (wire_profile_id == L4C_PROFILE_REQ_LOW) {
        *out_request = L4C_PROFILE_REQ_LOW;
        return true;
    }
    if (wire_profile_id == L4C_PROFILE_REQ_DEFAULT) {
        *out_request = L4C_PROFILE_REQ_DEFAULT;
        return true;
    }
    /* 540p не является стартовым UI-профилем; wire 2 от адаптера трактуем как default. */
    if (wire_profile_id == L4C_PROFILE_REQ_540P_WIRE) {
        *out_request = L4C_PROFILE_REQ_DEFAULT;
        return true;
    }
    return false;
}

l4c_status_t l4c_profile_resolve(
    uint8_t request,
    bool win7_or_legacy,
    bool hardware_mft_720p,
    bool openh264_720p_verified,
    l4c_profile_resolved_t *out)
{
    if (!out) return L4C_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->win7_legacy = win7_or_legacy;

    if (request == L4C_PROFILE_REQ_LOW) {
        out->actual_id = L4C_PROFILE_480P;
        out->start_fps = 10;
        return L4C_OK;
    }
    if (request != L4C_PROFILE_REQ_DEFAULT) {
        return L4C_ERR_INVALID_ARG;
    }
    if (win7_or_legacy) {
        out->actual_id = L4C_PROFILE_480P;
        out->start_fps = 10;
        out->win7_legacy = true;
        return L4C_OK;
    }
    if (hardware_mft_720p) {
        out->actual_id = L4C_PROFILE_720P;
        out->start_fps = 15;
        return L4C_OK;
    }
    if (openh264_720p_verified) {
        out->actual_id = L4C_PROFILE_720P;
        out->start_fps = 10;
        return L4C_OK;
    }
    /* 720p не поддержан проверкой конфигурации — базовый 480p пригоден как fallback resolve. */
    out->actual_id = L4C_PROFILE_480P;
    out->start_fps = 10;
    out->refused_premium = true;
    return L4C_OK;
}

bool l4c_profile_input_gate_allows(uint8_t request, uint16_t actual_id) {
    return (request == L4C_PROFILE_REQ_LOW) && (actual_id == L4C_PROFILE_480P);
}
