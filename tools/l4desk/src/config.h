#ifndef L4DESK_CONFIG_H
#define L4DESK_CONFIG_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#define L4DESK_VERSION_STR "1.7.5"
#define DEFAULT_MQTT_HOST "127.0.0.1"
#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_PROXY_HTTP_PORT 18443
#define DEFAULT_CLIENT_ID "svc_desk"
#define DEFAULT_PRESENCE_INTERVAL_SEC 30
#define DEFAULT_KEEPALIVE_SEC 30
#define DEFAULT_RECONNECT_SEC 5
#define DEFAULT_BASE_PATH "C:\\l4tools"
#define DEFAULT_LOG_FILE "C:\\l4tools\\l4desk\\log\\l4desk.log"

typedef struct {
    char base_path[MAX_PATH];
    char mqtt_host[128];
    int mqtt_port;
    int proxy_http_port;
    char client_id[64];
    char sn[64];
    bool sn_explicitly_set;
    int presence_interval_sec;
    int keepalive_sec;
    int reconnect_sec;
    char log_file[MAX_PATH];
    bool verbose;
    bool run_mode;      // true if launched with --run by l4superv
    bool console_mode;  // true if launched with --console or -f
    bool allow_f12;     // default false
    bool allow_alt_f4;  // default false
    bool allow_win_d;   // default false
    wchar_t kiosk_process[MAX_PATH];
} L4DeskConfig;

void config_init_defaults(L4DeskConfig* cfg);
bool config_parse_args(L4DeskConfig* cfg, int argc, char* argv[]);

#endif /* L4DESK_CONFIG_H */
