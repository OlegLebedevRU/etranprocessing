/**
 * @file config.h
 * @brief Configuration and defaults for Leo4Proxy (C / Windows SChannel TLS Proxy).
 */

#ifndef LEO4_CONFIG_H
#define LEO4_CONFIG_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>

#define LEO4_PROXY_VERSION "1.0.0"
#define LEO4_SERVICE_NAME L"Leo4Proxy"
#define LEO4_SERVICE_DISPLAY_NAME L"Leo4 IoT SChannel Proxy Service"
#define LEO4_SERVICE_DESC L"Leo4 IoT SChannel mTLS Proxy for MQTT (18883) and HTTPS (18443) using Windows Certificate Store."

/* Default endpoints */
#define DEFAULT_MQTT_LOCAL_HOST  "127.0.0.1"
#define DEFAULT_MQTT_LOCAL_PORT  18883
#define DEFAULT_MQTT_REMOTE_HOST "dev.leo4.ru"
#define DEFAULT_MQTT_REMOTE_PORT 8883

#define DEFAULT_HTTP_LOCAL_HOST  "127.0.0.1"
#define DEFAULT_HTTP_LOCAL_PORT  18443
#define DEFAULT_HTTP_REMOTE_HOST "iot-processing.ru"
#define DEFAULT_HTTP_REMOTE_PORT 443

#define DEFAULT_CERT_EMAIL_PRIMARY  ".terminal@leo4.ru"
#define DEFAULT_CERT_EMAIL_FALLBACK ".terminal@forpay.ru"

#define MAX_HOST_LEN        256
#define MAX_SN_LEN          128
#define MAX_EMAIL_LEN       256
#define MAX_THUMBPRINT_LEN  64
#define PROXY_BUFFER_SIZE   65536

typedef struct {
    char mqtt_local_host[MAX_HOST_LEN];
    int  mqtt_local_port;
    char mqtt_remote_host[MAX_HOST_LEN];
    int  mqtt_remote_port;

    char http_local_host[MAX_HOST_LEN];
    int  http_local_port;
    char http_remote_host[MAX_HOST_LEN];
    int  http_remote_port;

    char cert_email_pattern[MAX_EMAIL_LEN];
    char cert_thumbprint[MAX_THUMBPRINT_LEN];
    char cert_store_name[64];
    int  is_machine_store;      /* 1 for LocalMachine, 0 for CurrentUser */
    int  insecure_server_cert;  /* 1 to ignore untrusted server CA (default: 1) */

    int  run_as_service;
    int  run_foreground;
    int  verbose;
} ProxyConfig;

void proxy_config_init_defaults(ProxyConfig* config);

#endif /* LEO4_CONFIG_H */
