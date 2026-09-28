#include <string.h>
#include "l4capture/pipeline.h"

void l4c_pipeline_init(l4c_pipeline_t *pipeline) {
    memset(pipeline, 0, sizeof(*pipeline));
}

l4c_status_t l4c_pipeline_offer(l4c_pipeline_t *pipeline, uint32_t slot, bool *replaced, uint32_t *retired_slot) {
    if (!pipeline || !replaced || !retired_slot) return L4C_ERR_INVALID_ARG;
    if (pipeline->stopped) return L4C_ERR_FATAL;
    if (pipeline->processing && pipeline->processing_slot == slot) return L4C_ERR_INVALID_ARG;
    if (pipeline->pending && pipeline->pending_slot == slot) return L4C_ERR_INVALID_ARG;
    *replaced = pipeline->pending;
    *retired_slot = pipeline->pending_slot;
    if (*replaced) ++pipeline->raw_drops;
    pipeline->pending_slot = slot;
    pipeline->pending = true;
    return L4C_OK;
}

bool l4c_pipeline_take(l4c_pipeline_t *pipeline, uint32_t *slot) {
    if (!pipeline || !slot || pipeline->stopped || pipeline->processing || !pipeline->pending) return false;
    *slot = pipeline->pending_slot;
    pipeline->processing_slot = *slot;
    pipeline->pending = false;
    pipeline->processing = true;
    return true;
}

void l4c_pipeline_complete(l4c_pipeline_t *pipeline) {
    pipeline->processing = false;
}

void l4c_pipeline_stop(l4c_pipeline_t *pipeline) {
    pipeline->stopped = true;
    pipeline->pending = false;
    pipeline->processing = false;
}

bool l4c_pipeline_due(l4c_pipeline_t *pipeline, uint64_t now, uint32_t fps) {
    uint64_t interval;
    if (!pipeline || pipeline->stopped || !fps || fps > 1000 || now < pipeline->next_frame_ms) return false;
    interval = (1000u + (uint64_t)fps - 1u) / fps;
    if (now > UINT64_MAX - interval) return false;
    pipeline->next_frame_ms = now + interval; /* Без догоняющей серии кадров. */
    return true;
}