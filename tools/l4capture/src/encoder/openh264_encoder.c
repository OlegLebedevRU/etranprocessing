#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "l4capture/openh264_encoder.h"
#include "l4capture/clock.h"
#include "l4capture/limits.h"
#include "wels/codec_api.h"

typedef struct {
    ISVCEncoder *encoder;
    uint32_t width, height, fps;
    uint32_t target_bitrate, max_bitrate;
    uint64_t last_idr_tick_ms;
    uint64_t last_force_idr_tick_ms;
    bool pending_force_idr;
    bool first_frame;
    /* Pre-allocated AU buffer */
    uint8_t *au_buffer;
    size_t au_buffer_size;
    l4c_nal_desc_t *nal_descs;
    uint32_t max_nals;
} oh264_ctx_t;

static l4c_status_t oh264_init(struct l4c_encoder_backend *self, const l4c_encoder_config_t *config) {
    oh264_ctx_t *ctx;
    SEncParamExt param;
    int rv;
    if (!self || !config || !config->width || !config->height || !config->target_fps) return L4C_ERR_INVALID_ARG;
    if (config->input_format != L4C_PIX_FMT_I420) return L4C_ERR_INVALID_ARG;
    ctx = (oh264_ctx_t *)self->impl_ctx;
    if (!ctx) return L4C_ERR_INVALID_ARG;
    ctx->width = config->width;
    ctx->height = config->height;
    ctx->fps = config->target_fps;
    ctx->target_bitrate = config->target_bitrate_kbps * 1000;
    ctx->max_bitrate = config->max_bitrate_kbps * 1000;
    /* Create encoder */
    rv = WelsCreateSVCEncoder(&ctx->encoder);
    if (rv != 0 || !ctx->encoder) return L4C_ERR_FATAL;
    /* Get defaults and configure */
    memset(&param, 0, sizeof(param));
    (*ctx->encoder)->GetDefaultParams(ctx->encoder, &param);
    param.iUsageType = CAMERA_VIDEO_REAL_TIME;
    param.iPicWidth = (int)ctx->width;
    param.iPicHeight = (int)ctx->height;
    param.iTargetBitrate = (int)ctx->target_bitrate;
    param.iMaxBitrate = (int)ctx->max_bitrate;
    param.iRCMode = RC_BITRATE_MODE;
    param.fMaxFrameRate = (float)ctx->fps;
    param.iTemporalLayerNum = 1;
    param.iSpatialLayerNum = 1;
    param.sSpatialLayers[0].iVideoWidth = (int)ctx->width;
    param.sSpatialLayers[0].iVideoHeight = (int)ctx->height;
    param.sSpatialLayers[0].fFrameRate = (float)ctx->fps;
    param.sSpatialLayers[0].iSpatialBitrate = (int)ctx->target_bitrate;
    param.sSpatialLayers[0].iMaxSpatialBitrate = (int)ctx->max_bitrate;
    param.sSpatialLayers[0].uiProfileIdc = PRO_BASELINE;
    param.sSpatialLayers[0].uiLevelIdc = LEVEL_3_1;
    param.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
    param.iComplexityMode = LOW_COMPLEXITY;
    param.uiIntraPeriod = (unsigned int)(ctx->fps * 2);
    param.eSpsPpsIdStrategy = CONSTANT_ID;
    param.bEnableFrameSkip = 0;
    param.bEnableDenoise = 0;
    /* BGD/AQ добавляли CPU на каждом кадре без выигрыша для desktop-ROI. */
    param.bEnableBackgroundDetection = 0;
    param.bEnableAdaptiveQuant = 0;
    param.bEnableLongTermReference = 0;
    param.iMultipleThreadIdc = 1;
    param.iEntropyCodingModeFlag = 0; /* CAVLC */
    rv = (*ctx->encoder)->InitializeExt(ctx->encoder, &param);
    if (rv != 0) {
        WelsDestroySVCEncoder(ctx->encoder);
        ctx->encoder = NULL;
        return L4C_ERR_INVALID_ARG;
    }
    /* Force first frame to be IDR */
    (*ctx->encoder)->ForceIntraFrame(ctx->encoder, 1);
    ctx->first_frame = true;
    ctx->last_idr_tick_ms = 0;
    ctx->last_force_idr_tick_ms = 0;
    ctx->pending_force_idr = false;
    return L4C_OK;
}

static l4c_status_t oh264_encode(struct l4c_encoder_backend *self, const l4c_raw_frame_t *raw, l4c_access_unit_t *out_au) {
    oh264_ctx_t *ctx;
    SSourcePicture pic;
    SFrameBSInfo bs;
    int rv, layer, nal_idx;
    uint64_t now;
    size_t total_bytes;
    if (!self || !raw || !out_au) return L4C_ERR_INVALID_ARG;
    ctx = (oh264_ctx_t *)self->impl_ctx;
    if (!ctx || !ctx->encoder) return L4C_ERR_INVALID_ARG;
    if (raw->format != L4C_PIX_FMT_I420) return L4C_ERR_INVALID_ARG;
    now = l4c_now_monotonic_ms();
    /* IDR cadence check */
    if (!ctx->first_frame && (now - ctx->last_idr_tick_ms >= 2000 || ctx->pending_force_idr || raw->force_idr)) {
        (*ctx->encoder)->ForceIntraFrame(ctx->encoder, 1);
        ctx->pending_force_idr = false;
    }
    /* Prepare source picture */
    memset(&pic, 0, sizeof(pic));
    pic.iColorFormat = videoFormatI420;
    pic.iPicWidth = (int)raw->width;
    pic.iPicHeight = (int)raw->height;
    pic.pData[0] = (unsigned char *)raw->planes[0];
    pic.pData[1] = (unsigned char *)raw->planes[1];
    pic.pData[2] = (unsigned char *)raw->planes[2];
    pic.iStride[0] = (int)raw->strides[0];
    pic.iStride[1] = (int)raw->strides[1];
    pic.iStride[2] = (int)raw->strides[2];
    pic.uiTimeStamp = (long long)raw->pts_ms;
    /* Encode */
    memset(&bs, 0, sizeof(bs));
    rv = (*ctx->encoder)->EncodeFrame(ctx->encoder, &pic, &bs);
    if (rv != 0) return L4C_ERR_FATAL;
    /* Frame skip */
    if (bs.eFrameType == videoFrameTypeSkip) {
        out_au->nal_count = 0;
        out_au->total_bytes = 0;
        out_au->pts_ms = raw->pts_ms;
        out_au->is_idr = false;
        return L4C_ERR_NO_FRAME;
    }
    /* Parse NAL units and strip Annex B start codes */
    nal_idx = 0;
    total_bytes = 0;
    for (layer = 0; layer < bs.iLayerNum && nal_idx < (int)ctx->max_nals; ++layer) {
        SLayerBSInfo *layer_info = &bs.sLayerInfo[layer];
        uint8_t *buf = layer_info->pBsBuf;
        int nal;
        int nals_in_layer = layer_info->iNalCount;
        for (nal = 0; nal < nals_in_layer && nal_idx < (int)ctx->max_nals; ++nal) {
            int nal_len = layer_info->pNalLengthInByte[nal];
            uint8_t *nal_start = buf;
            int sc_len = 0;
            /* Find and skip start code */
            if (nal_len >= 4 && nal_start[0] == 0 && nal_start[1] == 0 && nal_start[2] == 0 && nal_start[3] == 1) {
                sc_len = 4;
            } else if (nal_len >= 3 && nal_start[0] == 0 && nal_start[1] == 0 && nal_start[2] == 1) {
                sc_len = 3;
            }
            if (sc_len > 0 && nal_len > sc_len) {
                ctx->nal_descs[nal_idx].data = nal_start + sc_len;
                ctx->nal_descs[nal_idx].length = (uint32_t)(nal_len - sc_len);
            } else {
                ctx->nal_descs[nal_idx].data = nal_start;
                ctx->nal_descs[nal_idx].length = (uint32_t)nal_len;
            }
            ctx->nal_descs[nal_idx].nal_type = ctx->nal_descs[nal_idx].data[0] & 0x1F;
            total_bytes += ctx->nal_descs[nal_idx].length;
            nal_idx++;
            buf += nal_len;
        }
    }
    if (total_bytes > L4C_MAX_AU_SIZE) {
        out_au->nal_count = 0;
        return L4C_ERR_OVERFLOW;
    }
    /* Fill output AU */
    out_au->nals = ctx->nal_descs;
    out_au->nal_count = (uint32_t)nal_idx;
    out_au->pts_ms = raw->pts_ms;
    out_au->total_bytes = total_bytes;
    out_au->is_idr = false;
    /* Check for IDR */
    {
        int i;
        for (i = 0; i < nal_idx; ++i) {
            if (ctx->nal_descs[i].nal_type == 5) {
                out_au->is_idr = true;
                ctx->last_idr_tick_ms = now;
                break;
            }
        }
    }
    if (ctx->first_frame) {
        ctx->first_frame = false;
        ctx->last_idr_tick_ms = now;
    }
    return L4C_OK;
}

static l4c_status_t oh264_force_idr(struct l4c_encoder_backend *self) {
    oh264_ctx_t *ctx;
    uint64_t now;
    if (!self) return L4C_ERR_INVALID_ARG;
    ctx = (oh264_ctx_t *)self->impl_ctx;
    if (!ctx) return L4C_ERR_INVALID_ARG;
    now = l4c_now_monotonic_ms();
    if (now - ctx->last_force_idr_tick_ms < L4C_FORCE_IDR_MIN_INTERVAL_MS) return L4C_OK;
    ctx->pending_force_idr = true;
    ctx->last_force_idr_tick_ms = now;
    return L4C_OK;
}

static void oh264_release_au(struct l4c_encoder_backend *self, l4c_access_unit_t *au) {
    (void)self;
    if (au) {
        au->nal_count = 0;
        au->total_bytes = 0;
        au->is_idr = false;
    }
}

static void oh264_destroy(struct l4c_encoder_backend *self) {
    oh264_ctx_t *ctx;
    if (!self) return;
    ctx = (oh264_ctx_t *)self->impl_ctx;
    if (!ctx) return;
    if (ctx->encoder) {
        WelsDestroySVCEncoder(ctx->encoder);
        ctx->encoder = NULL;
    }
    free(ctx->au_buffer);
    free(ctx->nal_descs);
    free(ctx);
    self->impl_ctx = NULL;
}

static const l4c_encoder_backend_vtable_t oh264_vtable = {
    oh264_init, oh264_encode, oh264_force_idr, oh264_release_au, oh264_destroy
};

l4c_status_t l4c_openh264_encoder_create(l4c_encoder_backend_t **out_backend) {
    l4c_encoder_backend_t *backend;
    oh264_ctx_t *ctx;
    if (!out_backend) return L4C_ERR_INVALID_ARG;
    ctx = (oh264_ctx_t *)calloc(1, sizeof(oh264_ctx_t));
    if (!ctx) return L4C_ERR_OUT_OF_MEMORY;
    /* Pre-allocate NAL descriptors */
    ctx->max_nals = 16;
    ctx->nal_descs = (l4c_nal_desc_t *)calloc(ctx->max_nals, sizeof(l4c_nal_desc_t));
    if (!ctx->nal_descs) { free(ctx); return L4C_ERR_OUT_OF_MEMORY; }
    /* Pre-allocate AU buffer (not used directly for pointer-based NALs, but reserved) */
    ctx->au_buffer_size = L4C_MAX_AU_SIZE;
    ctx->au_buffer = (uint8_t *)malloc(ctx->au_buffer_size);
    if (!ctx->au_buffer) { free(ctx->nal_descs); free(ctx); return L4C_ERR_OUT_OF_MEMORY; }
    backend = (l4c_encoder_backend_t *)calloc(1, sizeof(l4c_encoder_backend_t));
    if (!backend) { free(ctx->au_buffer); free(ctx->nal_descs); free(ctx); return L4C_ERR_OUT_OF_MEMORY; }
    backend->vtable = &oh264_vtable;
    backend->impl_ctx = ctx;
    *out_backend = backend;
    return L4C_OK;
}
