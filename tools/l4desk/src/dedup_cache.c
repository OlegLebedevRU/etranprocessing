#include "dedup_cache.h"
#include <windows.h>
#include <string.h>
#include <time.h>

#define DEDUP_CACHE_SIZE 256

typedef struct {
    char command_id[40];
    char response_payload[1024];
    size_t response_len;
    int64_t expires_at_ms;
    time_t last_accessed;
    bool in_use;
} CacheEntry;

static CacheEntry g_entries[DEDUP_CACHE_SIZE];
static CRITICAL_SECTION g_cache_cs;
static bool g_cs_init = false;

void dedup_cache_init(void) {
    if (!g_cs_init) {
        InitializeCriticalSection(&g_cache_cs);
        g_cs_init = true;
    }
    EnterCriticalSection(&g_cache_cs);
    memset(g_entries, 0, sizeof(g_entries));
    LeaveCriticalSection(&g_cache_cs);
}

bool dedup_cache_get(const char* command_id, char* out_response, size_t max_len, size_t* out_len) {
    if (!command_id || !out_response || max_len == 0) return false;

    if (!g_cs_init) return false;

    bool found = false;
    EnterCriticalSection(&g_cache_cs);

    for (int i = 0; i < DEDUP_CACHE_SIZE; i++) {
        if (g_entries[i].in_use && strcmp(g_entries[i].command_id, command_id) == 0) {
            g_entries[i].last_accessed = time(NULL);
            size_t to_copy = g_entries[i].response_len < max_len - 1 ? g_entries[i].response_len : max_len - 1;
            memcpy(out_response, g_entries[i].response_payload, to_copy);
            out_response[to_copy] = '\0';
            if (out_len) *out_len = to_copy;
            found = true;
            break;
        }
    }

    LeaveCriticalSection(&g_cache_cs);
    return found;
}

void dedup_cache_put(const char* command_id, const char* response, size_t resp_len, int64_t expires_at_ms) {
    if (!command_id || !response) return;

    if (!g_cs_init) {
        InitializeCriticalSection(&g_cache_cs);
        g_cs_init = true;
    }

    EnterCriticalSection(&g_cache_cs);

    int target_idx = -1;
    time_t oldest_time = 0x7FFFFFFF;

    // Check if entry already exists to update
    for (int i = 0; i < DEDUP_CACHE_SIZE; i++) {
        if (g_entries[i].in_use && strcmp(g_entries[i].command_id, command_id) == 0) {
            target_idx = i;
            break;
        }
    }

    // Find first empty slot or oldest slot for LRU
    if (target_idx == -1) {
        for (int i = 0; i < DEDUP_CACHE_SIZE; i++) {
            if (!g_entries[i].in_use) {
                target_idx = i;
                break;
            }
            if (g_entries[i].last_accessed < oldest_time) {
                oldest_time = g_entries[i].last_accessed;
                target_idx = i;
            }
        }
    }

    if (target_idx >= 0 && target_idx < DEDUP_CACHE_SIZE) {
        CacheEntry* entry = &g_entries[target_idx];
        entry->in_use = true;
        strcpy_s(entry->command_id, sizeof(entry->command_id), command_id);
        size_t cpy = resp_len < sizeof(entry->response_payload) - 1 ? resp_len : sizeof(entry->response_payload) - 1;
        memcpy(entry->response_payload, response, cpy);
        entry->response_payload[cpy] = '\0';
        entry->response_len = cpy;
        entry->expires_at_ms = expires_at_ms;
        entry->last_accessed = time(NULL);
    }

    LeaveCriticalSection(&g_cache_cs);
}

void dedup_cache_cleanup(void) {
    if (g_cs_init) {
        DeleteCriticalSection(&g_cache_cs);
        g_cs_init = false;
    }
}
