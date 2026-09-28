#ifndef L4C_PIPELINE_H
#define L4C_PIPELINE_H
#include "types.h"

/* Слот — индекс заранее выделенного буфера. Метаданные защищены safety gate.
 * Только media-worker освобождает память; stop инвалидирует слоты, не трогая
 * память выполняющегося encode. После stop ни один AU нельзя отправить. */
typedef struct {
    bool processing;
    bool pending;
    bool stopped;
    uint32_t processing_slot;
    uint32_t pending_slot;
    uint64_t raw_drops;
    uint64_t next_frame_ms;
} l4c_pipeline_t;

void l4c_pipeline_init(l4c_pipeline_t *pipeline);
l4c_status_t l4c_pipeline_offer(l4c_pipeline_t *pipeline, uint32_t slot, bool *replaced, uint32_t *retired_slot);
bool l4c_pipeline_take(l4c_pipeline_t *pipeline, uint32_t *slot);
void l4c_pipeline_complete(l4c_pipeline_t *pipeline);
void l4c_pipeline_stop(l4c_pipeline_t *pipeline);
bool l4c_pipeline_due(l4c_pipeline_t *pipeline, uint64_t now, uint32_t fps);
#endif