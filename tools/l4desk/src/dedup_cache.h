#ifndef L4DESK_DEDUP_CACHE_H
#define L4DESK_DEDUP_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dedup_cache_init(void);
bool dedup_cache_get(const char* command_id, char* out_response, size_t max_len, size_t* out_len);
void dedup_cache_put(const char* command_id, const char* response, size_t resp_len, int64_t expires_at_ms);
void dedup_cache_cleanup(void);

#endif /* L4DESK_DEDUP_CACHE_H */
