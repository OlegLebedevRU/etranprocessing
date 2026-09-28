#include "media_backend.h"
#include "l4capture_adapter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const l4d_media_backend_vtable_t s_l4capture_vtable = {
    l4d_adapter_start,
    l4d_adapter_stop,
    l4d_adapter_renew_lease,
    l4d_adapter_force_idr,
    l4d_adapter_poll,
    l4d_adapter_get_metrics,
    l4d_adapter_is_running,
    l4d_adapter_destroy
};

l4d_media_backend_t *l4d_media_backend_create(l4d_backend_type_t type, const char *bin_dir) {
    l4d_media_backend_t *backend = (l4d_media_backend_t *)calloc(1, sizeof(l4d_media_backend_t));
    if (!backend) return NULL;

    backend->type = type;

    if (type == L4D_BACKEND_L4CAPTURE) {
        backend->vtable = &s_l4capture_vtable;

        l4d_l4capture_adapter_cfg_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.connect_timeout_ms = 5000;
        cfg.ready_timeout_ms = 10000;
        cfg.event_poll_interval_ms = 50;

        if (bin_dir && bin_dir[0]) {
            char exe_path_a[MAX_PATH];
            snprintf(exe_path_a, sizeof(exe_path_a), "%s\\l4capture.exe", bin_dir);
            MultiByteToWideChar(CP_UTF8, 0, exe_path_a, -1, cfg.exe_path, MAX_PATH);
        }

        if (!l4d_adapter_init(backend, &cfg)) {
            free(backend);
            return NULL;
        }
    } else if (type == L4D_BACKEND_FFMPEG) {
        /* ffmpeg backend: not implemented in this step, placeholder */
        free(backend);
        return NULL;
    } else {
        free(backend);
        return NULL;
    }

    return backend;
}
