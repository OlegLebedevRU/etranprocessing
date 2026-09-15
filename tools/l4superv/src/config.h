#pragma once
#include <windows.h>
#include <stdbool.h>

#define L4_SUPERV_VERSION_STR L"1.7.5"
#define L4_DEFAULT_BASE_PATH  L"C:\\l4tools"
#define L4_DEFAULT_PROXY_URL  L"http://127.0.0.1:18443/_leo4/info"
#define L4_DEFAULT_MOSQUITTO_PORT 1883
#define L4_DEFAULT_POLL_INTERVAL_SEC 15
#define L4_DEFAULT_WATCHDOG_INTERVAL_SEC 10
#define L4_DEFAULT_STANDBY_POLL_SEC 5
#define L4_DEFAULT_PENDING_PIN_CHECK_SEC 30

typedef struct {
    wchar_t base_path[MAX_PATH];
    wchar_t config_file[MAX_PATH];
    wchar_t proxy_url[256];
    int     poll_interval_sec;
    int     watchdog_interval_sec;
    int     standby_poll_sec;
    int     pending_pin_check_sec;
    bool    watchdog_enabled;
    int     mosquitto_port;
    wchar_t mosquitto_template_path[MAX_PATH];
    bool    auto_reset_on_clone;
    bool    auto_start_leo4proxy;
    bool    auto_start_mosquitto;
    bool    auto_start_l4con;
    bool    auto_start_l4desk;
    wchar_t leo4proxy_args[512];
    wchar_t l4con_args[512];
    wchar_t l4desk_args[512];
    wchar_t l4desk_mode[64];
} L4SupervConfig;

/**
 * Initialize config with defaults, detecting base path from environment or executable path.
 */
void config_init_defaults(L4SupervConfig* cfg, const wchar_t* exe_path);

/**
 * Load configuration from l4superv.json if it exists.
 */
bool config_load_json(L4SupervConfig* cfg, const wchar_t* json_path);

/**
 * Save configuration to l4superv.json.
 */
bool config_save_json(const L4SupervConfig* cfg, const wchar_t* json_path);
