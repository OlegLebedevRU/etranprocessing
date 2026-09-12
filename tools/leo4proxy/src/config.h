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
#include <stdbool.h>

#define LEO4_PROXY_VERSION "1.2.0"
#define LEO4_SERVICE_NAME L"Leo4Proxy"
#define LEO4_SERVICE_DISPLAY_NAME L"Leo4 IoT SChannel Proxy Service"
#define LEO4_SERVICE_DESC L"Leo4 IoT SChannel mTLS Proxy for MQTT (18883), HTTPS (18443), Stream (8554) and RTP Tunnel (5004/5005) using Windows Certificate Store."

/* Default endpoints */
#define DEFAULT_MQTT_LOCAL_HOST  "127.0.0.1"
#define DEFAULT_MQTT_LOCAL_PORT  18883
#define DEFAULT_MQTT_REMOTE_HOST "dev.leo4.ru"
#define DEFAULT_MQTT_REMOTE_PORT 8883

#define DEFAULT_HTTP_LOCAL_HOST  "127.0.0.1"
#define DEFAULT_HTTP_LOCAL_PORT  18443
#define DEFAULT_HTTP_REMOTE_HOST "iot-processing.ru"
#define DEFAULT_HTTP_REMOTE_PORT 443

/* Stream forwarder (RTP/RTSP-over-mTLS, TCP) */
#define DEFAULT_STREAM_LOCAL_HOST   "127.0.0.1"
#define DEFAULT_STREAM_LOCAL_PORT   8554
#define DEFAULT_STREAM_REMOTE_HOST  "dev.leo4.ru"
#define DEFAULT_STREAM_REMOTE_PORT  8443
#define DEFAULT_STREAM_MAX_CLIENTS  2
#define DEFAULT_STREAM_IDLE_TIMEOUT 30

/* Primary media mode: local RTP/RTCP UDP -> framed mTLS/TCP tunnel (L4RTP/1) */
#define DEFAULT_RTP_TUNNEL_LOCAL_HOST        "127.0.0.1"
#define DEFAULT_RTP_TUNNEL_RTP_PORT          5004
#define DEFAULT_RTP_TUNNEL_RTCP_PORT         5005
#define DEFAULT_RTP_TUNNEL_REMOTE_HOST       "dev.leo4.ru"
#define DEFAULT_RTP_TUNNEL_REMOTE_PORT       8443
#define DEFAULT_RTP_TUNNEL_IDLE_TIMEOUT      30      /* sec; 0 = disabled */
#define DEFAULT_RTP_TUNNEL_RECONNECT_SEC     3       /* initial backoff */
#define DEFAULT_RTP_TUNNEL_RECONNECT_MAX_SEC 30      /* backoff cap */
#define DEFAULT_RTP_TUNNEL_CONNECT_TIMEOUT   5000    /* ms, schannel_connect */
#define DEFAULT_RTP_TUNNEL_MAX_PACKET_SIZE   65535

#define DEFAULT_REVERSE_LOCAL_HOST   "0.0.0.0"
#define DEFAULT_REVERSE_LOCAL_PORT   443
#define DEFAULT_REVERSE_TARGET_HOST  "127.0.0.1"
#define DEFAULT_REVERSE_TARGET_PORT  8000

#define DEFAULT_CERT_EMAIL_PRIMARY  ".terminal@leo4.ru"
#define DEFAULT_CERT_EMAIL_FALLBACK ".terminal@forpay.ru"
#define DEFAULT_CERT_POLL_INTERVAL  30

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

    /* Stream forwarder settings */
    int  stream_proxy_enabled;              /* 1 to enable local TCP -> mTLS stream forwarder (default: 0, opt-in) */
    char stream_local_host[MAX_HOST_LEN];
    int  stream_local_port;
    char stream_remote_host[MAX_HOST_LEN];
    int  stream_remote_port;
    int  stream_max_clients;
    int  stream_idle_timeout_sec;

    /* RTP tunnel settings (L4RTP/1) */
    int  rtp_tunnel_enabled;                /* default 1 (Lazy Connect), opt-out via --no-rtp-tunnel */
    char rtp_tunnel_local_host[MAX_HOST_LEN];
    int  rtp_tunnel_rtp_port;
    int  rtp_tunnel_rtcp_port;
    char rtp_tunnel_remote_host[MAX_HOST_LEN];
    int  rtp_tunnel_remote_port;
    int  rtp_tunnel_idle_timeout_sec;
    int  rtp_tunnel_reconnect_sec;

    /* Reverse Proxy settings */
    int  reverse_proxy_enabled;            /* 1 to enable reverse HTTPS proxy (default: 1) */
    char reverse_local_host[MAX_HOST_LEN]; /* Listener host, e.g. "0.0.0.0" */
    int  reverse_local_port;               /* Listener port, default 443 */
    char reverse_target_host[MAX_HOST_LEN];/* Internal target backend host, e.g. "127.0.0.1" */
    int  reverse_target_port;              /* Internal target backend port, default 8000 */

    /* Discovery & Network Announcement */
    int  discovery_enabled;                /* 1 to enable mDNS (5353) & LLMNR (5355) (default: 1) */
    char custom_local_domain[MAX_HOST_LEN];/* Custom domain override if specified */

    /* Windows Firewall & Elevation automation */
    int  firewall_auto;                    /* 1 to automatically manage Windows Defender Firewall rules (default: 1) */
    int  auto_elevate;                     /* 1 to auto-elevate to admin if launched unprivileged (default: 1) */

    char cert_email_pattern[MAX_EMAIL_LEN];
    char cert_thumbprint[MAX_THUMBPRINT_LEN];
    char cert_store_name[64];
    int  is_machine_store;      /* 1 for LocalMachine, 0 for CurrentUser */
    int  insecure_server_cert;  /* 1 to ignore untrusted server CA (default: 1) */
    int  cert_poll_interval;    /* Interval in seconds for certificate change polling (default: 30) */
    int  drop_on_expire;        /* 1 to transition to standby mode if cert expires and no valid cert in store (default: 0) */

    int  http_local_ssl;        /* 1 to enforce SSL on local HTTP listener */
    int  mqtt_local_ssl;        /* 1 to enforce SSL on local MQTT listener */
    int  auto_local_ssl;        /* 1 to auto-detect TLS vs Plain on local listener (default: 1) */

    int  run_as_service;
    int  run_foreground;
    int  verbose;
} ProxyConfig;

/* Runtime proxy metrics and status */
typedef struct {
    volatile LONG     mqtt_active_clients;
    volatile LONG     mqtt_total_connections;
    volatile LONG     http_total_requests;
    volatile LONG     reverse_total_requests;
    volatile LONG     stream_active_clients;
    volatile LONG     stream_total_connections;
    volatile LONGLONG stream_bytes_up;
    volatile LONGLONG stream_bytes_down;
    volatile LONG     rtp_tunnel_active;
    volatile LONG     rtp_tunnel_total_connections;
    volatile LONGLONG rtp_tunnel_bytes_up;
    volatile LONGLONG rtp_tunnel_packets_rtp;
    volatile LONGLONG rtp_tunnel_packets_rtcp;
    volatile LONGLONG rtp_tunnel_dropped_no_upstream;
    volatile LONG     cert_ready;
} ProxyStats;

extern ProxyStats g_proxyStats;

void proxy_config_init_defaults(ProxyConfig* config);

/* Network utility helpers */
bool tcp_probe_connect(const char* host, int port, int timeout_ms);
bool is_loopback_sockaddr(const struct sockaddr* sa);

#endif /* LEO4_CONFIG_H */
