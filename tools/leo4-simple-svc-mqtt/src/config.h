#ifndef LEO4_SVC_CONFIG_H
#define LEO4_SVC_CONFIG_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <stdbool.h>
#include <stdint.h>
#include <winsock2.h>
#include <windows.h>

#define LEO4_SVC_APP_NAME        "leo4-simple-svc-mqtt"
#define LEO4_SVC_APP_VERSION     "1.0.0"

#define LEO4_SVC_SERVICE_NAME    L"Leo4SimpleSvcMqtt"
#define LEO4_SVC_DISPLAY_NAME    L"Leo4 Simple Extra Service MQTT Client"
#define LEO4_SVC_DESC            L"Leo4 lightweight MQTT presence and LWT client (extra_service) for local Mosquitto bridge."

#define DEFAULT_MQTT_HOST        "127.0.0.1"
#define DEFAULT_MQTT_PORT        1883
#define DEFAULT_PROXY_HTTP_PORT  18443
#define DEFAULT_ROLE             "extra_service"
#define DEFAULT_KEEPALIVE_SEC    60
#define DEFAULT_RECONNECT_SEC    5
#define DEFAULT_FALLBACK_SN      "a3b1234567c10221d290825"

typedef struct {
    char mqtt_host[128];
    int  mqtt_port;
    int  proxy_http_port;
    char role[64];
    char device_sn[128];
    int  keepalive_sec;
    int  reconnect_sec;
    int  run_seconds;
    bool verbose;
    bool foreground;
} AppConfig;

void config_init_defaults(AppConfig* config);
bool config_parse_args(AppConfig* config, int argc, char* argv[], bool* out_is_service_cmd);
void config_print_help(const char* exe_name);
void config_print_version(void);

#endif /* LEO4_SVC_CONFIG_H */
