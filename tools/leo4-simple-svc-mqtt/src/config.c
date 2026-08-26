/**
 * @file config.c
 * @brief Configuration and CLI argument parser for leo4-simple-svc-mqtt.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_init_defaults(AppConfig* config) {
    if (!config) return;
    memset(config, 0, sizeof(AppConfig));

    strncpy_s(config->mqtt_host, sizeof(config->mqtt_host), DEFAULT_MQTT_HOST, _TRUNCATE);
    config->mqtt_port = DEFAULT_MQTT_PORT;
    config->proxy_http_port = DEFAULT_PROXY_HTTP_PORT;
    strncpy_s(config->role, sizeof(config->role), DEFAULT_ROLE, _TRUNCATE);
    config->device_sn[0] = '\0';
    config->keepalive_sec = DEFAULT_KEEPALIVE_SEC;
    config->reconnect_sec = DEFAULT_RECONNECT_SEC;
    config->run_seconds = 0;
    config->verbose = false;
    config->foreground = false;

    // Check environment variables
    const char* env_role = getenv("MQTT_ROLE");
    if (env_role && strlen(env_role) > 0) {
        strncpy_s(config->role, sizeof(config->role), env_role, _TRUNCATE);
    }

    const char* env_sn = getenv("DEVICE_SN");
    if (env_sn && strlen(env_sn) > 0) {
        strncpy_s(config->device_sn, sizeof(config->device_sn), env_sn, _TRUNCATE);
    }

    const char* env_host = getenv("MQTT_HOST");
    if (env_host && strlen(env_host) > 0) {
        strncpy_s(config->mqtt_host, sizeof(config->mqtt_host), env_host, _TRUNCATE);
    }

    const char* env_port = getenv("MQTT_PORT");
    if (env_port && strlen(env_port) > 0) {
        int p = atoi(env_port);
        if (p > 0 && p <= 65535) config->mqtt_port = p;
    }
}

static void parse_uri(AppConfig* config, const char* uri) {
    if (!uri || !config) return;

    const char* p = uri;
    if (_strnicmp(p, "tcp://", 6) == 0) {
        p += 6;
    } else if (_strnicmp(p, "mqtt://", 7) == 0) {
        p += 7;
    }

    const char* colon = strchr(p, ':');
    if (colon) {
        size_t host_len = colon - p;
        if (host_len > 0 && host_len < sizeof(config->mqtt_host)) {
            memcpy(config->mqtt_host, p, host_len);
            config->mqtt_host[host_len] = '\0';
        }
        int port = atoi(colon + 1);
        if (port > 0 && port <= 65535) {
            config->mqtt_port = port;
        }
    } else {
        strncpy_s(config->mqtt_host, sizeof(config->mqtt_host), p, _TRUNCATE);
    }
}

bool config_parse_args(AppConfig* config, int argc, char* argv[], bool* out_is_service_cmd) {
    if (out_is_service_cmd) *out_is_service_cmd = false;

    for (int i = 1; i < argc; i++) {
        if (_stricmp(argv[i], "--help") == 0 || _stricmp(argv[i], "-h") == 0 || _stricmp(argv[i], "/?") == 0) {
            config_print_help(argv[0]);
            exit(0);
        }
        if (_stricmp(argv[i], "--version") == 0 || _stricmp(argv[i], "-v") == 0) {
            config_print_version();
            exit(0);
        }
        if (_stricmp(argv[i], "--install") == 0 ||
            _stricmp(argv[i], "--uninstall") == 0 ||
            _stricmp(argv[i], "--start") == 0 ||
            _stricmp(argv[i], "--stop") == 0 ||
            _stricmp(argv[i], "--restart") == 0 ||
            _stricmp(argv[i], "--status") == 0 ||
            _stricmp(argv[i], "--service") == 0) {
            if (out_is_service_cmd) *out_is_service_cmd = true;
            continue;
        }
        if (_stricmp(argv[i], "--console") == 0 || _stricmp(argv[i], "-f") == 0 || _stricmp(argv[i], "--foreground") == 0) {
            config->foreground = true;
            continue;
        }
        if (_stricmp(argv[i], "--once") == 0 || _stricmp(argv[i], "-1") == 0) {
            config->run_seconds = 1;
            continue;
        }
        if (_stricmp(argv[i], "--timeout-sec") == 0 && i + 1 < argc) {
            int t = atoi(argv[++i]);
            if (t > 0) config->run_seconds = t;
            continue;
        }
        if (_stricmp(argv[i], "--verbose") == 0) {
            config->verbose = true;
            continue;
        }
        if (_stricmp(argv[i], "--host") == 0 && i + 1 < argc) {
            strncpy_s(config->mqtt_host, sizeof(config->mqtt_host), argv[++i], _TRUNCATE);
            continue;
        }
        if (_stricmp(argv[i], "--port") == 0 && i + 1 < argc) {
            int p = atoi(argv[++i]);
            if (p > 0 && p <= 65535) config->mqtt_port = p;
            continue;
        }
        if (_stricmp(argv[i], "--uri") == 0 && i + 1 < argc) {
            parse_uri(config, argv[++i]);
            continue;
        }
        if (_stricmp(argv[i], "--proxy-port") == 0 && i + 1 < argc) {
            int p = atoi(argv[++i]);
            if (p > 0 && p <= 65535) config->proxy_http_port = p;
            continue;
        }
        if (_stricmp(argv[i], "--sn") == 0 && i + 1 < argc) {
            strncpy_s(config->device_sn, sizeof(config->device_sn), argv[++i], _TRUNCATE);
            continue;
        }
        if (_stricmp(argv[i], "--role") == 0 && i + 1 < argc) {
            strncpy_s(config->role, sizeof(config->role), argv[++i], _TRUNCATE);
            continue;
        }
        if (_stricmp(argv[i], "--keepalive") == 0 && i + 1 < argc) {
            int k = atoi(argv[++i]);
            if (k >= 5) config->keepalive_sec = k;
            continue;
        }
        if (_stricmp(argv[i], "--reconnect") == 0 && i + 1 < argc) {
            int r = atoi(argv[++i]);
            if (r >= 1) config->reconnect_sec = r;
            continue;
        }
    }

    return true;
}

void config_print_version(void) {
    printf("%s version %s (x86/x64 unified Windows binary)\n", LEO4_SVC_APP_NAME, LEO4_SVC_APP_VERSION);
    printf("Role: extra_service | Protocol: MQTT 3.1.1 (TCP No-SSL)\n");
}

void config_print_help(const char* exe_name) {
    printf("===============================================================================\n");
    printf("  %s v%s - Leo4 Extra Service MQTT Presence Client\n", LEO4_SVC_APP_NAME, LEO4_SVC_APP_VERSION);
    printf("===============================================================================\n\n");
    printf("USAGE:\n");
    printf("  %s [options]\n\n", exe_name);
    printf("SERVICE MANAGEMENT COMMANDS:\n");
    printf("  --install          Install as Windows Service '%ls' (AUTO_START)\n", LEO4_SVC_SERVICE_NAME);
    printf("  --uninstall        Stop and remove Windows Service\n");
    printf("  --start            Start Windows Service via SCM\n");
    printf("  --stop             Stop running Windows Service via SCM\n");
    printf("  --restart          Restart Windows Service\n");
    printf("  --status           Check Windows Service status\n");
    printf("  --service          Run in Windows Service Dispatcher mode (called by SCM)\n\n");
    printf("EXECUTION OPTIONS:\n");
    printf("  --console, -f      Run in interactive console/foreground mode (Ctrl+C to stop)\n");
    printf("  --host <ip>        MQTT Bridge host (default: %s)\n", DEFAULT_MQTT_HOST);
    printf("  --port <port>      MQTT Bridge port (default: %d)\n", DEFAULT_MQTT_PORT);
    printf("  --uri <uri>        MQTT Bridge URI (e.g. tcp://127.0.0.1:1883)\n");
    printf("  --proxy-port <p>   Leo4Proxy REST port for SN query (default: %d)\n", DEFAULT_PROXY_HTTP_PORT);
    printf("  --sn <SN>          Override Device Serial Number\n");
    printf("  --role <role>      MQTT role / username (default: %s)\n", DEFAULT_ROLE);
    printf("  --keepalive <sec>  MQTT keepalive interval in seconds (default: %d)\n", DEFAULT_KEEPALIVE_SEC);
    printf("  --reconnect <sec>  Reconnect delay in seconds (default: %d)\n", DEFAULT_RECONNECT_SEC);
    printf("  --verbose          Enable verbose debug logging\n");
    printf("  --version, -v      Show version information\n");
    printf("  --help, -h         Show this help message\n\n");
    printf("MQTT PRESENCE SCENARIO (extra_service):\n");
    printf("  CONNECT:    LWT will_topic=dev/{SN}/svc, will_payload=svc_offline, retain=1, QoS=1\n");
    printf("  CONNACK:    PUBLISH dev/{SN}/svc = svc_online (retain=1, QoS=1)\n");
    printf("  SHUTDOWN:   PUBLISH dev/{SN}/svc = svc_offline (retain=1, QoS=1) -> DISCONNECT\n\n");
}
