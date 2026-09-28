#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_init_defaults(L4DeskConfig* cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(L4DeskConfig));

    const char* env_base = getenv("L4_TOOLS_BASE_PATH");
    if (env_base && env_base[0] != '\0') {
        strcpy_s(cfg->base_path, sizeof(cfg->base_path), env_base);
    } else {
        strcpy_s(cfg->base_path, sizeof(cfg->base_path), DEFAULT_BASE_PATH);
    }

    strcpy_s(cfg->mqtt_host, sizeof(cfg->mqtt_host), DEFAULT_MQTT_HOST);
    cfg->mqtt_port = DEFAULT_MQTT_PORT;
    cfg->proxy_http_port = DEFAULT_PROXY_HTTP_PORT;
    strcpy_s(cfg->client_id, sizeof(cfg->client_id), DEFAULT_CLIENT_ID);
    cfg->presence_interval_sec = DEFAULT_PRESENCE_INTERVAL_SEC;
    cfg->keepalive_sec = DEFAULT_KEEPALIVE_SEC;
    cfg->reconnect_sec = DEFAULT_RECONNECT_SEC;
    snprintf(cfg->log_file, sizeof(cfg->log_file), "%s\\l4desk\\log\\l4desk.log", cfg->base_path);
    cfg->verbose = false;
    cfg->run_mode = false;
    cfg->console_mode = true;
    cfg->sn_explicitly_set = false;
    cfg->allow_f12 = false;
    cfg->allow_alt_f4 = false;
    cfg->allow_win_d = false;
    cfg->kiosk_process[0] = L'\0';
    strcpy_s(cfg->media_backend, sizeof(cfg->media_backend), "l4capture");

    // Check environment variable DEVICE_SN as fallback for console/debug
    char* env_sn = getenv("DEVICE_SN");
    if (env_sn && strlen(env_sn) > 0) {
        strcpy_s(cfg->sn, sizeof(cfg->sn), env_sn);
        cfg->sn_explicitly_set = true;
    }
}

bool config_parse_args(L4DeskConfig* cfg, int argc, char* argv[]) {
    if (!cfg) return false;

    for (int i = 1; i < argc; i++) {
        if (_stricmp(argv[i], "--help") == 0 || _stricmp(argv[i], "-h") == 0) {
            printf("Usage: l4desk [options]\n");
            printf("Options:\n");
            printf("  --run                      Run hidden under l4superv supervision\n");
            printf("  --console, -f              Run in foreground console mode\n");
            printf("  --host <ip>                MQTT broker host (default: %s)\n", DEFAULT_MQTT_HOST);
            printf("  --port <port>              MQTT broker port (default: %d)\n", DEFAULT_MQTT_PORT);
            printf("  --proxy-port <port>        Leo4Proxy HTTP port (default: %d)\n", DEFAULT_PROXY_HTTP_PORT);
            printf("  --client-id <id>           MQTT client identifier (default: %s)\n", DEFAULT_CLIENT_ID);
            printf("  --sn <sn>                  Terminal SN (console/debug only)\n");
            printf("  --presence-interval <sec>  Periodic presence publish interval (default: %d)\n", DEFAULT_PRESENCE_INTERVAL_SEC);
            printf("  --keepalive <sec>          MQTT keepalive seconds (default: %d)\n", DEFAULT_KEEPALIVE_SEC);
            printf("  --reconnect <sec>          Initial reconnect backoff seconds (default: %d)\n", DEFAULT_RECONNECT_SEC);
            printf("  --log <path>               Log file path (default: %s)\n", DEFAULT_LOG_FILE);
            printf("  --verbose, -v              Verbose logging\n");
            printf("  --version                  Show version\n");
            exit(0);
        }
        if (_stricmp(argv[i], "--version") == 0) {
            printf("l4desk version %s\n", L4DESK_VERSION_STR);
            exit(0);
        }
        if (_stricmp(argv[i], "--run") == 0) {
            cfg->run_mode = true;
            cfg->console_mode = false;
            continue;
        }
        if (_stricmp(argv[i], "--console") == 0 || _stricmp(argv[i], "-f") == 0) {
            cfg->console_mode = true;
            continue;
        }
        if (_stricmp(argv[i], "--verbose") == 0 || _stricmp(argv[i], "-v") == 0) {
            cfg->verbose = true;
            continue;
        }
        if (_stricmp(argv[i], "--base-path") == 0 && i + 1 < argc) {
            strcpy_s(cfg->base_path, sizeof(cfg->base_path), argv[++i]);
            snprintf(cfg->log_file, sizeof(cfg->log_file), "%s\\l4desk\\log\\l4desk.log", cfg->base_path);
            continue;
        }
        if (_stricmp(argv[i], "--host") == 0 && i + 1 < argc) {
            strcpy_s(cfg->mqtt_host, sizeof(cfg->mqtt_host), argv[++i]);
            continue;
        }
        if (_stricmp(argv[i], "--port") == 0 && i + 1 < argc) {
            cfg->mqtt_port = atoi(argv[++i]);
            continue;
        }
        if (_stricmp(argv[i], "--proxy-port") == 0 && i + 1 < argc) {
            cfg->proxy_http_port = atoi(argv[++i]);
            continue;
        }
        if (_stricmp(argv[i], "--client-id") == 0 && i + 1 < argc) {
            strcpy_s(cfg->client_id, sizeof(cfg->client_id), argv[++i]);
            continue;
        }
        if (_stricmp(argv[i], "--sn") == 0 && i + 1 < argc) {
            strcpy_s(cfg->sn, sizeof(cfg->sn), argv[++i]);
            cfg->sn_explicitly_set = true;
            continue;
        }
        if (_stricmp(argv[i], "--presence-interval") == 0 && i + 1 < argc) {
            cfg->presence_interval_sec = atoi(argv[++i]);
            if (cfg->presence_interval_sec < 5) cfg->presence_interval_sec = 5;
            continue;
        }
        if (_stricmp(argv[i], "--keepalive") == 0 && i + 1 < argc) {
            cfg->keepalive_sec = atoi(argv[++i]);
            continue;
        }
        if (_stricmp(argv[i], "--reconnect") == 0 && i + 1 < argc) {
            cfg->reconnect_sec = atoi(argv[++i]);
            continue;
        }
        if (_stricmp(argv[i], "--allow-f12") == 0) {
            cfg->allow_f12 = true;
            continue;
        }
        if (_stricmp(argv[i], "--allow-alt-f4") == 0) {
            cfg->allow_alt_f4 = true;
            continue;
        }
        if (_stricmp(argv[i], "--allow-win-d") == 0) {
            cfg->allow_win_d = true;
            continue;
        }
        if (_stricmp(argv[i], "--kiosk-process") == 0 && i + 1 < argc) {
            MultiByteToWideChar(CP_UTF8, 0, argv[++i], -1, cfg->kiosk_process, MAX_PATH);
            continue;
        }
        if (_stricmp(argv[i], "--media-backend") == 0 && i + 1 < argc) {
            strcpy_s(cfg->media_backend, sizeof(cfg->media_backend), argv[++i]);
            continue;
        }
        if (_stricmp(argv[i], "--log") == 0 && i + 1 < argc) {
            strcpy_s(cfg->log_file, sizeof(cfg->log_file), argv[++i]);
            continue;
        }
    }

    return true;
}
