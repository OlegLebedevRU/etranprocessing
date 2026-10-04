#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "../../l4pin/src/http_client.h"
#include "../../leo4proxy/src/policy_json.h"

#pragma comment(lib, "winhttp.lib")

void config_init_defaults(AppConfig* config) {
    if (!config) return;
    memset(config, 0, sizeof(AppConfig));
    strncpy(config->mqtt_host, DEFAULT_MQTT_HOST, sizeof(config->mqtt_host) - 1);
    config->mqtt_port = DEFAULT_MQTT_PORT;
    config->proxy_http_port = DEFAULT_PROXY_HTTP_PORT;
    strncpy(config->role, DEFAULT_ROLE, sizeof(config->role) - 1);
    config->keepalive_sec = DEFAULT_KEEPALIVE_SEC;
    config->reconnect_sec = DEFAULT_RECONNECT_SEC;
    config->default_cmd_timeout = DEFAULT_CMD_TIMEOUT_SEC;
    config->max_output_chunk = 3072;
    config->enable_blacklist = true;
    config->verbose = false;
    config->foreground = false;
    config->is_service = false;
    config->sn_explicitly_set = false;
}

int config_query_sn_from_proxy(int proxy_port, char* out_sn, size_t out_sn_size) {
    if (!out_sn || out_sn_size == 0 || proxy_port < 1 || proxy_port > 65535) return -1;
    out_sn[0] = '\0';
    char url[128]; sprintf_s(url, sizeof(url), "http://127.0.0.1:%d/_leo4/sn", proxy_port);
    char* body = NULL; size_t size = 0;
    if (!http_get_simple(url, 1200, &body, &size)) return -1;
    while (size && (body[size-1] == '\r' || body[size-1] == '\n' || body[size-1] == ' ' || body[size-1] == '\t')) size--;
    bool valid = size > 0 && size < out_sn_size;
    for (size_t i=0; i<size; i++) {
        unsigned char ch = (unsigned char)body[i];
        if (ch <= 32 || ch >= 127 || ch == '/' || ch == '\\' || ch == '"') valid = false;
    }
    if (valid) { memcpy(out_sn, body, size); out_sn[size] = '\0'; }
    free(body); return valid ? 0 : -1;
}

int config_query_identity_from_proxy(int proxy_port, ProxyIdentity* out_identity) {
    if (!out_identity || proxy_port < 1 || proxy_port > 65535) return -1;
    memset(out_identity, 0, sizeof(*out_identity));
    char url[128]; sprintf_s(url,sizeof(url),"http://127.0.0.1:%d/_leo4/info",proxy_port);
    char* body = NULL; size_t length = 0;
    if (!http_get_simple(url,1200,&body,&length)) return -1;
    PolicyJson json; bool found = false; char status[32]; ProxyIdentity identity = {0};
    bool valid = policy_json_parse(&json,body,length) &&
        policy_json_string(&json,policy_json_field(&json,0,"status"),status,sizeof(status)) && !strcmp(status,"ready") &&
        policy_json_bool(&json,policy_json_field(&json,0,"certificate_found"),&found) && found &&
        policy_json_string(&json,policy_json_field(&json,0,"sn"),identity.sn,sizeof(identity.sn)) && identity.sn[0] &&
        policy_json_string(&json,policy_json_field(&json,0,"thumbprint"),identity.thumbprint,sizeof(identity.thumbprint)) &&
        strlen(identity.thumbprint) == 40 &&
        policy_json_string(&json,policy_json_field(&json,0,"serial"),identity.serial,sizeof(identity.serial)) && identity.serial[0] &&
        policy_json_string(&json,policy_json_field(&json,0,"not_after"),identity.not_after,sizeof(identity.not_after)) && identity.not_after[0];
    if (valid) *out_identity = identity;
    free(body); return valid ? 0 : -1;
}
static void parse_uri(const char* uri, char* host, size_t host_len, int* port) {
    if (!uri) return;
    const char* p = uri;
    if (strncmp(p, "tcp://", 6) == 0) {
        p += 6;
    } else if (strncmp(p, "mqtt://", 7) == 0) {
        p += 7;
    }
    const char* colon = strchr(p, ':');
    if (colon) {
        size_t hlen = (size_t)(colon - p);
        if (hlen >= host_len) hlen = host_len - 1;
        strncpy(host, p, hlen);
        host[hlen] = '\0';
        *port = atoi(colon + 1);
    } else {
        strncpy(host, p, host_len - 1);
        host[host_len - 1] = '\0';
    }
}

bool config_parse_args(AppConfig* config, int argc, char* argv[], bool* out_is_service_cmd) {
    if (!config) return false;
    if (out_is_service_cmd) *out_is_service_cmd = false;

    for (int i = 1; i < argc; i++) {
        if (_stricmp(argv[i], "--install") == 0 ||
            _stricmp(argv[i], "--uninstall") == 0 ||
            _stricmp(argv[i], "--start") == 0 ||
            _stricmp(argv[i], "--stop") == 0 ||
            _stricmp(argv[i], "--restart") == 0 ||
            _stricmp(argv[i], "--status") == 0 ||
            _stricmp(argv[i], "--service") == 0) {
            if (out_is_service_cmd) *out_is_service_cmd = true;
        }

        if (_stricmp(argv[i], "--host") == 0 && i + 1 < argc) {
            strncpy(config->mqtt_host, argv[++i], sizeof(config->mqtt_host) - 1);
        } else if (_stricmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config->mqtt_port = atoi(argv[++i]);
        } else if (_stricmp(argv[i], "--uri") == 0 && i + 1 < argc) {
            parse_uri(argv[++i], config->mqtt_host, sizeof(config->mqtt_host), &config->mqtt_port);
        } else if (_stricmp(argv[i], "--proxy-port") == 0 && i + 1 < argc) {
            config->proxy_http_port = atoi(argv[++i]);
        } else if (_stricmp(argv[i], "--sn") == 0 && i + 1 < argc) {
            strncpy(config->device_sn, argv[++i], sizeof(config->device_sn) - 1);
            config->sn_explicitly_set = true;
        } else if ((_stricmp(argv[i], "--client-id") == 0 || _stricmp(argv[i], "--clientid") == 0 || _stricmp(argv[i], "-c") == 0) && i + 1 < argc) {
            strncpy(config->client_id, argv[++i], sizeof(config->client_id) - 1);
        } else if (_stricmp(argv[i], "--role") == 0 && i + 1 < argc) {
            strncpy(config->role, argv[++i], sizeof(config->role) - 1);
        } else if (_stricmp(argv[i], "--keepalive") == 0 && i + 1 < argc) {
            config->keepalive_sec = atoi(argv[++i]);
        } else if (_stricmp(argv[i], "--reconnect") == 0 && i + 1 < argc) {
            config->reconnect_sec = atoi(argv[++i]);
        } else if (_stricmp(argv[i], "--timeout") == 0) {
            if (i+1>=argc) return false;
            const char* value=argv[++i];
            if (!*value || strlen(value)>4) return false;
            for (const char* p=value;*p;p++) if (*p<'0' || *p>'9') return false;
            config->default_cmd_timeout=atoi(value);
            if (config->default_cmd_timeout<1 || config->default_cmd_timeout>3600) return false;
        } else if (_stricmp(argv[i], "--no-blacklist") == 0) {
            config->enable_blacklist = false;
        } else if (_stricmp(argv[i], "--verbose") == 0) {
            config->verbose = true;
        } else if (_stricmp(argv[i], "--console") == 0 || _stricmp(argv[i], "-f") == 0) {
            config->foreground = true;
        } else if (_stricmp(argv[i], "--version") == 0 || _stricmp(argv[i], "-v") == 0) {
            config_print_version();
            exit(0);
        } else if (_stricmp(argv[i], "--help") == 0 || _stricmp(argv[i], "-h") == 0) {
            config_print_help(argv[0]);
            exit(0);
        }
    }
    return config->default_cmd_timeout>=1 && config->default_cmd_timeout<=3600 &&
        config->reconnect_sec>=1 && config->reconnect_sec<=300 &&
        config->keepalive_sec>=5 && config->keepalive_sec<=3600 &&
        config->mqtt_port>=1 && config->mqtt_port<=65535 &&
        config->proxy_http_port>=1 && config->proxy_http_port<=65535;
}

void config_print_version(void) {
    printf("%s v%s (Windows C / WinSock2 / MSVC /MT)\n", L4CON_APP_NAME, L4CON_APP_VERSION);
    printf("Leo4 Diagnostic Console MQTT Client (extra_service)\n");
}

void config_print_help(const char* exe_name) {
    printf("Usage: %s [options]\n\n", exe_name);
    printf("Windows Service Commands:\n");
    printf("  --install          Install as Windows Service (%ls)\n", L4CON_SERVICE_NAME);
    printf("  --uninstall        Uninstall Windows Service\n");
    printf("  --start            Start Windows Service\n");
    printf("  --stop             Stop Windows Service\n");
    printf("  --restart          Restart Windows Service\n");
    printf("  --status           Check status of Windows Service\n");
    printf("  --console, -f      Run in interactive console (foreground) mode\n\n");
    printf("Connection Options:\n");
    printf("  --host <ip>        MQTT Broker host (default: %s)\n", DEFAULT_MQTT_HOST);
    printf("  --port <port>      MQTT Broker port (default: %d)\n", DEFAULT_MQTT_PORT);
    printf("  --uri <uri>        MQTT Broker URI (e.g. tcp://127.0.0.1:1883)\n");
    printf("  --proxy-port <p>   Leo4Proxy HTTP port for SN query (default: %d)\n", DEFAULT_PROXY_HTTP_PORT);
    printf("  --sn <SN>          Explicit device serial number\n");
    printf("  --client-id, -c <id> MQTT Client ID (default: <SN>_extra)\n");
    printf("  --role <role>      MQTT role / client username (default: %s)\n", DEFAULT_ROLE);
    printf("  --keepalive <sec>  Keepalive interval (default: %d)\n", DEFAULT_KEEPALIVE_SEC);
    printf("  --reconnect <sec>  Reconnect delay (default: %d)\n", DEFAULT_RECONNECT_SEC);
    printf("  --timeout <sec>    Default command timeout in seconds (default: %d)\n", DEFAULT_CMD_TIMEOUT_SEC);
    printf("  --no-blacklist     Disable dangerous command blacklist\n");
    printf("  --verbose          Enable verbose debug output\n");
    printf("  --version, -v      Show version information\n");
    printf("  --help, -h         Show this help message\n");
    printf("  --send-event -event-code=999 -event-payload=<text> -event-exit-code=0\n");
    printf("                    Quiet IPC send; only descendants of active remote commands\n");
    printf("                    Codes 900..999; text <=1024 UTF-8 bytes; exit code is int32\n");
    printf("                    Optional -event-correlation-id=<UUID> is tag 448\n");
}
