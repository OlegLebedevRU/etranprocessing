#ifndef L4CON_CONFIG_H
#define L4CON_CONFIG_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <stdbool.h>
#include <stdint.h>
#include <winsock2.h>
#include <windows.h>

#define L4CON_APP_NAME        "l4con"
#define L4CON_APP_VERSION     "1.0.0"

#define L4CON_SERVICE_NAME    L"L4Con"
#define L4CON_DISPLAY_NAME    L"Leo4 Remote Diagnostics and Console Agent (l4con)"
#define L4CON_DESC            L"Leo4 lightweight MQTT diagnostic console client (extra_service) executing Windows commands and streaming output."

#define DEFAULT_MQTT_HOST        "127.0.0.1"
#define DEFAULT_MQTT_PORT        1883
#define DEFAULT_PROXY_HTTP_PORT  18443
#define DEFAULT_ROLE             "extra_service"
#define DEFAULT_KEEPALIVE_SEC    60
#define DEFAULT_RECONNECT_SEC    5
#define DEFAULT_CMD_TIMEOUT_SEC  30

typedef struct {
    char mqtt_host[128];
    int  mqtt_port;
    int  proxy_http_port;
    char role[64];
    char client_id[128];
    char device_sn[128];
    bool sn_explicitly_set;
    int  keepalive_sec;
    int  reconnect_sec;
    int  default_cmd_timeout;
    int  max_output_chunk;
    bool enable_blacklist;
    bool verbose;
    bool foreground;
    bool is_service;
} AppConfig;

void config_init_defaults(AppConfig* config);
bool config_parse_args(AppConfig* config, int argc, char* argv[], bool* out_is_service_cmd);
void config_print_help(const char* exe_name);
void config_print_version(void);
int  config_query_sn_from_proxy(int proxy_port, char* out_sn, size_t out_sn_size);

#endif /* L4CON_CONFIG_H */
