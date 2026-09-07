/**
 * @file main.c
 * @brief Leo4Proxy - Unified Windows SChannel mTLS Proxy and Reverse HTTPS Gateway for Leo4 & Etranprocessing.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "cert_store.h"
#include "schannel_tls.h"
#include "mqtt_proxy.h"
#include "stream_proxy.h"
#include "rtp_tunnel.h"
#include "http_proxy.h"
#include "reverse_proxy.h"
#include "discovery.h"
#include "firewall.h"
#include "service_mgr.h"
#include "tray_icon.h"
#include "../res/resource.h"

static volatile bool g_consoleRunning = true;

typedef struct {
    ProxyConfig config;
    CertDetails certDetails;
    CredHandle hClientCred;
    CredHandle hServerCred;
    MqttProxyServer mqttServer;
    StreamProxyServer streamServer;
    RtpTunnelServer rtpTunnelServer;
    HttpProxyServer httpServer;
    ReverseProxyServer reverseServer;
    DiscoveryServer discoveryServer;
    TrayIconContext trayCtx;
    bool isForwardRunning;
    bool isStreamRunning;
    bool isRtpTunnelRunning;
    bool isReverseRunning;
    bool isDiscoveryRunning;
} AppState;

static AppState g_app;

static void on_tray_action(int action_id, void* user_data) {
    AppState* app = (AppState*)user_data;
    if (!app) return;

    switch (action_id) {
        case IDM_TRAY_STOP_ALL:
            printf("[TRAY] Stopping all proxies and discovery...\n");
            if (app->isReverseRunning) {
                reverse_proxy_stop(&app->reverseServer);
                app->isReverseRunning = false;
                tray_icon_set_reverse_state(&app->trayCtx, false);
            }
            if (app->isForwardRunning) {
                if (app->isStreamRunning) {
                    stream_proxy_stop(&app->streamServer);
                    app->isStreamRunning = false;
                }
                if (app->isRtpTunnelRunning) {
                    rtp_tunnel_stop(&app->rtpTunnelServer);
                    app->isRtpTunnelRunning = false;
                }
                mqtt_proxy_stop(&app->mqttServer);
                http_proxy_stop(&app->httpServer);
                app->isForwardRunning = false;
                tray_icon_set_forward_state(&app->trayCtx, false);
            }
            if (app->isDiscoveryRunning) {
                discovery_stop(&app->discoveryServer);
                app->isDiscoveryRunning = false;
            }
            tray_icon_set_state(&app->trayCtx, TRAY_STATE_STOPPED);
            printf("[TRAY] All proxies STOPPED.\n");
            break;

        case IDM_TRAY_START_ALL:
            printf("[TRAY] Starting all proxies and discovery...\n");
            if (!app->isForwardRunning) {
                bool mOk = mqtt_proxy_start(&app->mqttServer, &app->config, &app->certDetails, app->hClientCred, app->hServerCred);
                bool hOk = http_proxy_start(&app->httpServer, &app->config, &app->certDetails, app->hClientCred, app->hServerCred);
                if (mOk && hOk) {
                    app->isForwardRunning = true;
                    tray_icon_set_forward_state(&app->trayCtx, true);
                    if (app->config.stream_proxy_enabled) {
                        if (stream_proxy_start(&app->streamServer, &app->config, &app->certDetails, app->hClientCred)) {
                            app->isStreamRunning = true;
                        } else {
                            fprintf(stderr, "[WARNING] Stream forwarder failed to start on %s:%d\n",
                                    app->config.stream_local_host, app->config.stream_local_port);
                        }
                    }
                    if (app->config.rtp_tunnel_enabled) {
                        if (rtp_tunnel_start(&app->rtpTunnelServer, &app->config, &app->certDetails, app->hClientCred)) {
                            app->isRtpTunnelRunning = true;
                        } else {
                            fprintf(stderr, "[WARNING] RTP tunnel failed to start on %s:%d/%d\n",
                                    app->config.rtp_tunnel_local_host, app->config.rtp_tunnel_rtp_port, app->config.rtp_tunnel_rtcp_port);
                        }
                    }
                }
            }
            if (!app->isReverseRunning && app->config.reverse_proxy_enabled && SecIsValidHandle(&app->hServerCred)) {
                if (reverse_proxy_start(&app->reverseServer, &app->config, &app->certDetails, app->hServerCred)) {
                    app->isReverseRunning = true;
                    tray_icon_set_reverse_state(&app->trayCtx, true);
                }
            }
            if (!app->isDiscoveryRunning && app->config.discovery_enabled) {
                if (discovery_start(&app->discoveryServer, &app->config, &app->certDetails)) {
                    app->isDiscoveryRunning = true;
                }
            }
            tray_icon_set_state(&app->trayCtx, TRAY_STATE_RUNNING);
            printf("[TRAY] All proxies RUNNING.\n");
            break;

        case IDM_TRAY_RESTART_ALL:
            printf("[TRAY] Restarting all proxies...\n");
            if (app->isReverseRunning) reverse_proxy_stop(&app->reverseServer);
            if (app->isForwardRunning) {
                if (app->isStreamRunning) {
                    stream_proxy_stop(&app->streamServer);
                    app->isStreamRunning = false;
                }
                if (app->isRtpTunnelRunning) {
                    rtp_tunnel_stop(&app->rtpTunnelServer);
                    app->isRtpTunnelRunning = false;
                }
                mqtt_proxy_stop(&app->mqttServer);
                http_proxy_stop(&app->httpServer);
            }
            if (app->isDiscoveryRunning) discovery_stop(&app->discoveryServer);
            app->isReverseRunning = false;
            app->isForwardRunning = false;
            app->isDiscoveryRunning = false;
            Sleep(500);

            mqtt_proxy_start(&app->mqttServer, &app->config, &app->certDetails, app->hClientCred, app->hServerCred);
            http_proxy_start(&app->httpServer, &app->config, &app->certDetails, app->hClientCred, app->hServerCred);
            app->isForwardRunning = true;
            tray_icon_set_forward_state(&app->trayCtx, true);
            if (app->config.stream_proxy_enabled) {
                if (stream_proxy_start(&app->streamServer, &app->config, &app->certDetails, app->hClientCred)) {
                    app->isStreamRunning = true;
                } else {
                    fprintf(stderr, "[WARNING] Stream forwarder failed to start on %s:%d\n",
                            app->config.stream_local_host, app->config.stream_local_port);
                }
            }
            if (app->config.rtp_tunnel_enabled) {
                if (rtp_tunnel_start(&app->rtpTunnelServer, &app->config, &app->certDetails, app->hClientCred)) {
                    app->isRtpTunnelRunning = true;
                } else {
                    fprintf(stderr, "[WARNING] RTP tunnel failed to start on %s:%d/%d\n",
                            app->config.rtp_tunnel_local_host, app->config.rtp_tunnel_rtp_port, app->config.rtp_tunnel_rtcp_port);
                }
            }

            if (app->config.reverse_proxy_enabled && SecIsValidHandle(&app->hServerCred)) {
                reverse_proxy_start(&app->reverseServer, &app->config, &app->certDetails, app->hServerCred);
                app->isReverseRunning = true;
                tray_icon_set_reverse_state(&app->trayCtx, true);
            }
            if (app->config.discovery_enabled) {
                discovery_start(&app->discoveryServer, &app->config, &app->certDetails);
                app->isDiscoveryRunning = true;
            }
            tray_icon_set_state(&app->trayCtx, TRAY_STATE_RUNNING);
            printf("[TRAY] All proxies RESTARTED.\n");
            break;

        case IDM_TRAY_TOGGLE_REVERSE:
            if (app->isReverseRunning) {
                printf("[TRAY] Stopping Reverse HTTPS Proxy...\n");
                reverse_proxy_stop(&app->reverseServer);
                app->isReverseRunning = false;
                tray_icon_set_reverse_state(&app->trayCtx, false);
                printf("[TRAY] Reverse HTTPS Proxy STOPPED.\n");
            } else {
                printf("[TRAY] Starting Reverse HTTPS Proxy...\n");
                if (reverse_proxy_start(&app->reverseServer, &app->config, &app->certDetails, app->hServerCred)) {
                    app->isReverseRunning = true;
                    tray_icon_set_reverse_state(&app->trayCtx, true);
                    printf("[TRAY] Reverse HTTPS Proxy RUNNING.\n");
                }
            }
            break;

        case IDM_TRAY_TOGGLE_FORWARD:
            if (app->isForwardRunning) {
                printf("[TRAY] Stopping Forward Proxies...\n");
                if (app->isStreamRunning) {
                    stream_proxy_stop(&app->streamServer);
                    app->isStreamRunning = false;
                }
                if (app->isRtpTunnelRunning) {
                    rtp_tunnel_stop(&app->rtpTunnelServer);
                    app->isRtpTunnelRunning = false;
                }
                mqtt_proxy_stop(&app->mqttServer);
                http_proxy_stop(&app->httpServer);
                app->isForwardRunning = false;
                tray_icon_set_forward_state(&app->trayCtx, false);
                printf("[TRAY] Forward Proxies STOPPED.\n");
            } else {
                printf("[TRAY] Starting Forward Proxies...\n");
                bool mOk = mqtt_proxy_start(&app->mqttServer, &app->config, &app->certDetails, app->hClientCred, app->hServerCred);
                bool hOk = http_proxy_start(&app->httpServer, &app->config, &app->certDetails, app->hClientCred, app->hServerCred);
                if (mOk && hOk) {
                    app->isForwardRunning = true;
                    tray_icon_set_forward_state(&app->trayCtx, true);
                    if (app->config.stream_proxy_enabled) {
                        if (stream_proxy_start(&app->streamServer, &app->config, &app->certDetails, app->hClientCred)) {
                            app->isStreamRunning = true;
                        } else {
                            fprintf(stderr, "[WARNING] Stream forwarder failed to start on %s:%d\n",
                                    app->config.stream_local_host, app->config.stream_local_port);
                        }
                    }
                    if (app->config.rtp_tunnel_enabled) {
                        if (rtp_tunnel_start(&app->rtpTunnelServer, &app->config, &app->certDetails, app->hClientCred)) {
                            app->isRtpTunnelRunning = true;
                        } else {
                            fprintf(stderr, "[WARNING] RTP tunnel failed to start on %s:%d/%d\n",
                                    app->config.rtp_tunnel_local_host, app->config.rtp_tunnel_rtp_port, app->config.rtp_tunnel_rtcp_port);
                        }
                    }
                    printf("[TRAY] Forward Proxies RUNNING.\n");
                }
            }
            break;

        case IDM_TRAY_EXIT:
            printf("[TRAY] Exit requested from System Tray.\n");
            g_consoleRunning = false;
            break;
    }
}

static void pause_if_explorer(void) {
    DWORD pids[2];
    DWORD count = GetConsoleProcessList(pids, 2);
    if (count <= 1) {
        printf("\n[LEO4PROXY] Press Enter to exit...\n");
        fflush(stdout);
        getchar();
    }
}

static LONG WINAPI unhandled_exception_handler(EXCEPTION_POINTERS* pExp) {
    FILE* f = NULL;
    fopen_s(&f, "leo4proxy_crash.log", "a");
    if (f) {
        fprintf(f, "\n=== LEO4PROXY CRASH REPORT ===\n");
        fprintf(f, "Exception Code:    0x%08lX\n", pExp->ExceptionRecord->ExceptionCode);
        fprintf(f, "Exception Address: 0x%p\n", pExp->ExceptionRecord->ExceptionAddress);
        fprintf(f, "Exception Flags:   0x%08lX\n", pExp->ExceptionRecord->ExceptionFlags);
        fclose(f);
    }
    fprintf(stderr, "\n===============================================================================\n");
    fprintf(stderr, "[FATAL] Unhandled Exception: 0x%08lX at address 0x%p\n",
            pExp->ExceptionRecord->ExceptionCode, pExp->ExceptionRecord->ExceptionAddress);
    fprintf(stderr, "Crash details saved to leo4proxy_crash.log\n");
    fprintf(stderr, "===============================================================================\n");

    pause_if_explorer();
    return EXCEPTION_EXECUTE_HANDLER;
}

static BOOL WINAPI console_ctrl_handler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            printf("\n[LEO4PROXY] Termination signal received. Stopping proxies...\n");
            g_consoleRunning = false;
            return TRUE;
        default:
            return FALSE;
    }
}

void proxy_config_init_defaults(ProxyConfig* config) {
    if (!config) return;
    memset(config, 0, sizeof(ProxyConfig));

    strncpy_s(config->mqtt_local_host, sizeof(config->mqtt_local_host), DEFAULT_MQTT_LOCAL_HOST, _TRUNCATE);
    config->mqtt_local_port = DEFAULT_MQTT_LOCAL_PORT;
    strncpy_s(config->mqtt_remote_host, sizeof(config->mqtt_remote_host), DEFAULT_MQTT_REMOTE_HOST, _TRUNCATE);
    config->mqtt_remote_port = DEFAULT_MQTT_REMOTE_PORT;

    strncpy_s(config->http_local_host, sizeof(config->http_local_host), DEFAULT_HTTP_LOCAL_HOST, _TRUNCATE);
    config->http_local_port = DEFAULT_HTTP_LOCAL_PORT;
    strncpy_s(config->http_remote_host, sizeof(config->http_remote_host), DEFAULT_HTTP_REMOTE_HOST, _TRUNCATE);
    config->http_remote_port = DEFAULT_HTTP_REMOTE_PORT;

    config->stream_proxy_enabled = 0;
    strncpy_s(config->stream_local_host, sizeof(config->stream_local_host), DEFAULT_STREAM_LOCAL_HOST, _TRUNCATE);
    config->stream_local_port = DEFAULT_STREAM_LOCAL_PORT;
    strncpy_s(config->stream_remote_host, sizeof(config->stream_remote_host), DEFAULT_STREAM_REMOTE_HOST, _TRUNCATE);
    config->stream_remote_port = DEFAULT_STREAM_REMOTE_PORT;
    config->stream_max_clients = DEFAULT_STREAM_MAX_CLIENTS;
    config->stream_idle_timeout_sec = DEFAULT_STREAM_IDLE_TIMEOUT;

    config->rtp_tunnel_enabled = 0;
    strncpy_s(config->rtp_tunnel_local_host, sizeof(config->rtp_tunnel_local_host), DEFAULT_RTP_TUNNEL_LOCAL_HOST, _TRUNCATE);
    config->rtp_tunnel_rtp_port = DEFAULT_RTP_TUNNEL_RTP_PORT;
    config->rtp_tunnel_rtcp_port = DEFAULT_RTP_TUNNEL_RTCP_PORT;
    strncpy_s(config->rtp_tunnel_remote_host, sizeof(config->rtp_tunnel_remote_host), DEFAULT_RTP_TUNNEL_REMOTE_HOST, _TRUNCATE);
    config->rtp_tunnel_remote_port = DEFAULT_RTP_TUNNEL_REMOTE_PORT;
    config->rtp_tunnel_idle_timeout_sec = DEFAULT_RTP_TUNNEL_IDLE_TIMEOUT;
    config->rtp_tunnel_reconnect_sec = DEFAULT_RTP_TUNNEL_RECONNECT_SEC;

    config->reverse_proxy_enabled = 1;
    strncpy_s(config->reverse_local_host, sizeof(config->reverse_local_host), DEFAULT_REVERSE_LOCAL_HOST, _TRUNCATE);
    config->reverse_local_port = DEFAULT_REVERSE_LOCAL_PORT;
    strncpy_s(config->reverse_target_host, sizeof(config->reverse_target_host), DEFAULT_REVERSE_TARGET_HOST, _TRUNCATE);
    config->reverse_target_port = DEFAULT_REVERSE_TARGET_PORT;

    config->discovery_enabled = 1;
    config->custom_local_domain[0] = '\0';
    config->firewall_auto = 1;
    config->auto_elevate = 1;

    strncpy_s(config->cert_store_name, sizeof(config->cert_store_name), "MY", _TRUNCATE);
    config->is_machine_store = 1;      // Default: LocalMachine\MY
    config->insecure_server_cert = 1;  // Default: ignore untrusted server CA for dev/migration
    config->cert_poll_interval = DEFAULT_CERT_POLL_INTERVAL; // Default: 30s poll in service mode
    config->drop_on_expire = 0;        // Default: keep expired cert and let remote server decide

    config->http_local_ssl = 0;
    config->mqtt_local_ssl = 0;
    config->auto_local_ssl = 1;        // Default: auto-detect TLS vs Plain on local listener

    config->run_as_service = 0;
    config->run_foreground = 0;
    config->verbose = 0;
}

static void parse_host_port(const char* str, char* out_host, size_t out_host_size, int* out_port) {
    if (!str || !out_host || !out_port) return;

    const char* p = str;
    const char* scheme = strstr(str, "://");
    if (scheme) {
        p = scheme + 3;
    }

    const char* colon = strrchr(p, ':');
    if (colon) {
        size_t hostLen = (size_t)(colon - p);
        if (hostLen >= out_host_size) hostLen = out_host_size - 1;
        strncpy_s(out_host, out_host_size, p, hostLen);
        out_host[hostLen] = '\0';
        *out_port = atoi(colon + 1);
    } else {
        strncpy_s(out_host, out_host_size, p, _TRUNCATE);
    }
}

static void parse_rtp_local(const char* str, char* out_host, size_t out_host_size, int* out_rtp_port, int* out_rtcp_port) {
    if (!str || !out_host || !out_rtp_port || !out_rtcp_port) return;

    const char* p = str;
    const char* scheme = strstr(str, "://");
    if (scheme) {
        p = scheme + 3;
    }

    char temp[MAX_HOST_LEN];
    strncpy_s(temp, sizeof(temp), p, _TRUNCATE);

    char* slash = strchr(temp, '/');
    if (slash) {
        *slash = '\0';
        *out_rtcp_port = atoi(slash + 1);
    }

    char* colon1 = strchr(temp, ':');
    if (colon1) {
        *colon1 = '\0';
        char* colon2 = strchr(colon1 + 1, ':');
        if (colon2) {
            *colon2 = '\0';
            *out_rtcp_port = atoi(colon2 + 1);
        }
        *out_rtp_port = atoi(colon1 + 1);
        if (!slash && !colon2) {
            *out_rtcp_port = *out_rtp_port + 1;
        }
    }
    strncpy_s(out_host, out_host_size, temp, _TRUNCATE);
}

static void print_usage(const char* exeName) {
    printf("===============================================================================\n");
    printf(" Leo4Proxy v%s - SChannel mTLS Proxy & Reverse HTTPS Gateway\n", LEO4_PROXY_VERSION);
    printf("===============================================================================\n\n");
    printf("USAGE:\n");
    printf("  %s [OPTIONS]\n\n", exeName);
    printf("OPERATIONAL COMMANDS:\n");
    printf("  --get-sn                Query Windows Store and output ONLY Device SN (exit 0)\n");
    printf("  --test-cert             Test certificate discovery and print full details\n");
    printf("  --install               Install/update Windows Service with current arguments\n");
    printf("  --uninstall             Uninstall Windows Service\n");
    printf("  --start                 Start Windows Service\n");
    printf("  --stop                  Stop Windows Service\n");
    printf("  --restart               Restart Windows Service\n");
    printf("  --status                Check Windows Service status\n");
    printf("  -f, --console           Run in foreground console mode\n");
    printf("  -v, --verbose           Enable verbose connection debugging logs\n");
    printf("  -h, --help              Show this help message\n\n");
    printf("REVERSE HTTPS PROXY & LAN DISCOVERY (.local):\n");
    printf("  --reverse-target <h:p>  Internal target server for reverse proxy (default: %s:%d)\n", DEFAULT_REVERSE_TARGET_HOST, DEFAULT_REVERSE_TARGET_PORT);
    printf("  --reverse-listen <i:p>  External listen address for reverse proxy (default: %s:%d)\n", DEFAULT_REVERSE_LOCAL_HOST, DEFAULT_REVERSE_LOCAL_PORT);
    printf("  --reverse-port <port>   External port for reverse HTTPS (default: %d)\n", DEFAULT_REVERSE_LOCAL_PORT);
    printf("  --no-reverse            Disable reverse HTTPS proxy\n");
    printf("  --domain <name>         Override LAN hostname (default: from SAN DNS or leo4-<sn>.local)\n");
    printf("  --no-discovery          Disable mDNS (5353) & LLMNR (5355) LAN announcement\n");
    printf("  --no-firewall           Disable automatic Windows Defender Firewall rules configuration\n");
    printf("  --no-elevate            Do not automatically elevate to Administrator if unprivileged\n\n");
    printf("FORWARD mTLS PROXIES (OUTBOUND):\n");
    printf("  --mqtt-remote <host:p>  Remote MQTT Broker (default: %s:%d)\n", DEFAULT_MQTT_REMOTE_HOST, DEFAULT_MQTT_REMOTE_PORT);
    printf("  --mqtt-local  <ip:port> Local MQTT listener (default: %s:%d)\n", DEFAULT_MQTT_LOCAL_HOST, DEFAULT_MQTT_LOCAL_PORT);
    printf("  --http-remote <host:p>  Remote HTTPS Backend (default: %s:%d)\n", DEFAULT_HTTP_REMOTE_HOST, DEFAULT_HTTP_REMOTE_PORT);
    printf("  --http-local  <ip:port> Local HTTP listener (default: %s:%d)\n", DEFAULT_HTTP_LOCAL_HOST, DEFAULT_HTTP_LOCAL_PORT);
    printf("  --local-ssl             Enforce SSL/TLS on local listeners (default: auto-detect)\n");
    printf("  --secure                Strict server CA validation (default: lax/insecure)\n\n");
    printf("STREAM FORWARDER (Legacy TCP -> mTLS -> cloud media ingress):\n");
    printf("  --stream                   Enable local TCP -> mTLS stream forwarder (default: disabled)\n");
    printf("  --no-stream                Disable stream forwarder\n");
    printf("  --stream-local <ip:port>   Local stream listener (default: %s:%d)\n", DEFAULT_STREAM_LOCAL_HOST, DEFAULT_STREAM_LOCAL_PORT);
    printf("  --stream-remote <host:p>   Remote cloud stream ingress (default: %s:%d)\n", DEFAULT_STREAM_REMOTE_HOST, DEFAULT_STREAM_REMOTE_PORT);
    printf("  --stream-max-clients <n>   Max concurrent streaming clients (default: %d, min: 1)\n", DEFAULT_STREAM_MAX_CLIENTS);
    printf("  --stream-idle-timeout <sec> Idle timeout in seconds before closing tunnel (default: %d, 0 = disabled)\n\n", DEFAULT_STREAM_IDLE_TIMEOUT);
    printf("RTP/RTCP TUNNEL (Primary Video Mode: ffmpeg UDP -> L4RTP/1 mTLS -> dev.leo4.ru:8443):\n");
    printf("  --rtp-tunnel               Enable local RTP/RTCP UDP -> mTLS tunnel (default: disabled)\n");
    printf("  --no-rtp-tunnel            Disable RTP tunnel\n");
    printf("  --rtp-local <ip[:rtp[:rtcp]]> Local UDP listener address (default: %s:%d/%d)\n",
           DEFAULT_RTP_TUNNEL_LOCAL_HOST, DEFAULT_RTP_TUNNEL_RTP_PORT, DEFAULT_RTP_TUNNEL_RTCP_PORT);
    printf("  --rtp-port <port>          Local RTP UDP port (default: %d)\n", DEFAULT_RTP_TUNNEL_RTP_PORT);
    printf("  --rtcp-port <port>         Local RTCP UDP port (default: %d)\n", DEFAULT_RTP_TUNNEL_RTCP_PORT);
    printf("  --rtp-remote <host:p>      Remote cloud video ingress (default: %s:%d)\n",
           DEFAULT_RTP_TUNNEL_REMOTE_HOST, DEFAULT_RTP_TUNNEL_REMOTE_PORT);
    printf("  --rtp-idle-timeout <sec>   Idle timeout before closing mTLS session (default: %d, 0 = disabled)\n",
           DEFAULT_RTP_TUNNEL_IDLE_TIMEOUT);
    printf("  --rtp-reconnect <sec>      Initial reconnect backoff in seconds (default: %d)\n\n",
           DEFAULT_RTP_TUNNEL_RECONNECT_SEC);
    printf("CERTIFICATE SELECTION & ROTATION:\n");
    printf("  --cert-email <pattern>     Filter certs by email (default: newest %s -> %s)\n", DEFAULT_CERT_EMAIL_PRIMARY, DEFAULT_CERT_EMAIL_FALLBACK);
    printf("  --cert-thumbprint <sha>    Select specific certificate by SHA-1 thumbprint\n");
    printf("  --user-store               Search CurrentUser\\MY instead of LocalMachine\\MY\n");
    printf("  --cert-poll-interval <sec> Polling interval in seconds for cert changes in service (default: %d)\n", DEFAULT_CERT_POLL_INTERVAL);
    printf("  --drop-on-expire           Transition to standby mode if cert expires and no valid cert in store\n\n");
    printf("EXAMPLES:\n");
    printf("  1. Standard interactive start (Reverse HTTPS on :443 + Forward mTLS on :18883,:18443):\n");
    printf("       %s\n\n", exeName);
    printf("  2. Forward Reverse HTTPS requests to local Uvicorn on 127.0.0.1:8000:\n");
    printf("       %s --reverse-target 127.0.0.1:8000\n\n", exeName);
    printf("  3. Check device SN for scripts:\n");
    printf("       %s --get-sn\n\n", exeName);
    printf("  4. Primary RTP/RTCP video tunnel for ffmpeg (UDP :5004/:5005 -> mTLS :8443):\n");
    printf("       %s --rtp-tunnel --rtp-remote dev.leo4.ru:8443\n", exeName);
    printf("       ffmpeg -f dshow -i video=\"USB Camera\" -c:v libx264 -preset ultrafast -tune zerolatency -b:v 800k -f rtp rtp://127.0.0.1:5004?rtcpport=5005\n\n");
    printf("  5. Legacy TCP video stream forwarding for ffmpeg (RTSP/MPEG-TS over TCP -> mTLS :8443):\n");
    printf("       %s --stream --stream-remote dev.leo4.ru:8443\n", exeName);
    printf("       ffmpeg -f dshow -i video=\"USB Camera\" -c:v libx264 -preset ultrafast -tune zerolatency -b:v 800k -f mpegts tcp://127.0.0.1:8554\n\n");
}

int main(int argc, char* argv[]) {
    // Install global unhandled exception filter
    SetUnhandledExceptionFilter(unhandled_exception_handler);

    // Disable stdout/stderr buffering for real-time logs
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // Enable UTF-8 console output
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    ProxyConfig config;
    proxy_config_init_defaults(&config);

    bool explicitInstall = false;
    bool explicitUninstall = false;
    bool explicitStart = false;
    bool explicitStop = false;
    bool explicitRestart = false;
    bool explicitStatus = false;
    bool getSnOnly = false;
    bool testCertOnly = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (_stricmp(argv[i], "-h") == 0 || _stricmp(argv[i], "--help") == 0 || _stricmp(argv[i], "/?") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (_stricmp(argv[i], "--version") == 0) {
            printf("Leo4Proxy version %s\n", LEO4_PROXY_VERSION);
            return 0;
        } else if (_stricmp(argv[i], "--service") == 0) {
            config.run_as_service = 1;
        } else if (_stricmp(argv[i], "--install") == 0) {
            explicitInstall = true;
        } else if (_stricmp(argv[i], "--uninstall") == 0) {
            explicitUninstall = true;
        } else if (_stricmp(argv[i], "--start") == 0) {
            explicitStart = true;
        } else if (_stricmp(argv[i], "--stop") == 0) {
            explicitStop = true;
        } else if (_stricmp(argv[i], "--restart") == 0) {
            explicitRestart = true;
        } else if (_stricmp(argv[i], "--status") == 0) {
            explicitStatus = true;
        } else if (_stricmp(argv[i], "--get-sn") == 0) {
            getSnOnly = true;
        } else if (_stricmp(argv[i], "--test-cert") == 0) {
            testCertOnly = true;
        } else if (_stricmp(argv[i], "-f") == 0 || _stricmp(argv[i], "--console") == 0 || _stricmp(argv[i], "--foreground") == 0) {
            config.run_foreground = 1;
        } else if (_stricmp(argv[i], "-v") == 0 || _stricmp(argv[i], "--verbose") == 0) {
            config.verbose = 1;
        } else if (_stricmp(argv[i], "--secure") == 0) {
            config.insecure_server_cert = 0;
        } else if (_stricmp(argv[i], "--user-store") == 0) {
            config.is_machine_store = 0;
        } else if (_stricmp(argv[i], "--mqtt-remote") == 0 && i + 1 < argc) {
            parse_host_port(argv[++i], config.mqtt_remote_host, sizeof(config.mqtt_remote_host), &config.mqtt_remote_port);
        } else if (_stricmp(argv[i], "--mqtt-local") == 0 && i + 1 < argc) {
            parse_host_port(argv[++i], config.mqtt_local_host, sizeof(config.mqtt_local_host), &config.mqtt_local_port);
        } else if (_stricmp(argv[i], "--http-remote") == 0 && i + 1 < argc) {
            parse_host_port(argv[++i], config.http_remote_host, sizeof(config.http_remote_host), &config.http_remote_port);
        } else if (_stricmp(argv[i], "--http-local") == 0 && i + 1 < argc) {
            parse_host_port(argv[++i], config.http_local_host, sizeof(config.http_local_host), &config.http_local_port);
        } else if (_stricmp(argv[i], "--reverse-target") == 0 && i + 1 < argc) {
            parse_host_port(argv[++i], config.reverse_target_host, sizeof(config.reverse_target_host), &config.reverse_target_port);
        } else if ((_stricmp(argv[i], "--reverse-listen") == 0 || _stricmp(argv[i], "--reverse-local") == 0) && i + 1 < argc) {
            parse_host_port(argv[++i], config.reverse_local_host, sizeof(config.reverse_local_host), &config.reverse_local_port);
        } else if (_stricmp(argv[i], "--reverse-port") == 0 && i + 1 < argc) {
            config.reverse_local_port = atoi(argv[++i]);
        } else if (_stricmp(argv[i], "--no-reverse") == 0) {
            config.reverse_proxy_enabled = 0;
        } else if ((_stricmp(argv[i], "--domain") == 0 || _stricmp(argv[i], "--local-domain") == 0) && i + 1 < argc) {
            strncpy_s(config.custom_local_domain, sizeof(config.custom_local_domain), argv[++i], _TRUNCATE);
        } else if (_stricmp(argv[i], "--no-discovery") == 0) {
            config.discovery_enabled = 0;
        } else if (_stricmp(argv[i], "--no-firewall") == 0) {
            config.firewall_auto = 0;
        } else if (_stricmp(argv[i], "--no-elevate") == 0) {
            config.auto_elevate = 0;
        } else if (_stricmp(argv[i], "--local-ssl") == 0) {
            config.http_local_ssl = 1;
            config.mqtt_local_ssl = 1;
        } else if (_stricmp(argv[i], "--http-local-ssl") == 0) {
            config.http_local_ssl = 1;
        } else if (_stricmp(argv[i], "--mqtt-local-ssl") == 0) {
            config.mqtt_local_ssl = 1;
        } else if (_stricmp(argv[i], "--cert-email") == 0 && i + 1 < argc) {
            strncpy_s(config.cert_email_pattern, sizeof(config.cert_email_pattern), argv[++i], _TRUNCATE);
        } else if (_stricmp(argv[i], "--cert-thumbprint") == 0 && i + 1 < argc) {
            strncpy_s(config.cert_thumbprint, sizeof(config.cert_thumbprint), argv[++i], _TRUNCATE);
        } else if (_stricmp(argv[i], "--cert-poll-interval") == 0 && i + 1 < argc) {
            config.cert_poll_interval = atoi(argv[++i]);
            if (config.cert_poll_interval < 1) config.cert_poll_interval = 1;
        } else if (_stricmp(argv[i], "--drop-on-expire") == 0) {
            config.drop_on_expire = 1;
        } else if (_stricmp(argv[i], "--stream") == 0) {
            config.stream_proxy_enabled = 1;
        } else if (_stricmp(argv[i], "--no-stream") == 0) {
            config.stream_proxy_enabled = 0;
        } else if (_stricmp(argv[i], "--stream-local") == 0 && i + 1 < argc) {
            parse_host_port(argv[++i], config.stream_local_host, sizeof(config.stream_local_host), &config.stream_local_port);
        } else if (_stricmp(argv[i], "--stream-remote") == 0 && i + 1 < argc) {
            parse_host_port(argv[++i], config.stream_remote_host, sizeof(config.stream_remote_host), &config.stream_remote_port);
        } else if (_stricmp(argv[i], "--stream-max-clients") == 0 && i + 1 < argc) {
            config.stream_max_clients = atoi(argv[++i]);
            if (config.stream_max_clients < 1) config.stream_max_clients = 1;
        } else if (_stricmp(argv[i], "--stream-idle-timeout") == 0 && i + 1 < argc) {
            config.stream_idle_timeout_sec = atoi(argv[++i]);
            if (config.stream_idle_timeout_sec < 0) config.stream_idle_timeout_sec = 0;
        } else if (_stricmp(argv[i], "--rtp-tunnel") == 0) {
            config.rtp_tunnel_enabled = 1;
        } else if (_stricmp(argv[i], "--no-rtp-tunnel") == 0) {
            config.rtp_tunnel_enabled = 0;
        } else if (_stricmp(argv[i], "--rtp-local") == 0 && i + 1 < argc) {
            parse_rtp_local(argv[++i], config.rtp_tunnel_local_host, sizeof(config.rtp_tunnel_local_host),
                            &config.rtp_tunnel_rtp_port, &config.rtp_tunnel_rtcp_port);
        } else if (_stricmp(argv[i], "--rtp-port") == 0 && i + 1 < argc) {
            config.rtp_tunnel_rtp_port = atoi(argv[++i]);
        } else if (_stricmp(argv[i], "--rtcp-port") == 0 && i + 1 < argc) {
            config.rtp_tunnel_rtcp_port = atoi(argv[++i]);
        } else if (_stricmp(argv[i], "--rtp-remote") == 0 && i + 1 < argc) {
            parse_host_port(argv[++i], config.rtp_tunnel_remote_host, sizeof(config.rtp_tunnel_remote_host), &config.rtp_tunnel_remote_port);
        } else if (_stricmp(argv[i], "--rtp-idle-timeout") == 0 && i + 1 < argc) {
            config.rtp_tunnel_idle_timeout_sec = atoi(argv[++i]);
            if (config.rtp_tunnel_idle_timeout_sec < 0) config.rtp_tunnel_idle_timeout_sec = 0;
        } else if (_stricmp(argv[i], "--rtp-reconnect") == 0 && i + 1 < argc) {
            config.rtp_tunnel_reconnect_sec = atoi(argv[++i]);
            if (config.rtp_tunnel_reconnect_sec < 1) config.rtp_tunnel_reconnect_sec = 1;
        }
    }

    // 1. Quick SN Query Mode
    if (getSnOnly) {
        CertDetails details;
        if (cert_store_find_best_cert(&config, &details)) {
            if (details.sn[0] != '\0') {
                printf("%s\n", details.sn);
                cert_store_free_details(&details);
                return 0;
            }
            cert_store_free_details(&details);
        }
        fprintf(stderr, "[ERROR] No valid terminal certificate found in Windows Store.\n");
        return 1;
    }

    // 2. Certificate Testing Mode
    if (testCertOnly) {
        printf("[LEO4PROXY] Searching Windows Certificate Store (%s\\%s)...\n",
               config.is_machine_store ? "LocalMachine" : "CurrentUser", config.cert_store_name);

        CertDetails details;
        if (!cert_store_find_best_cert(&config, &details)) {
            fprintf(stderr, "[ERROR] Failed to locate a matching certificate with accessible private key.\n");
            return 1;
        }

        cert_store_print_details(&details);

        // Test SChannel Credential Acquisition
        printf("[LEO4PROXY] Testing SChannel credentials handle acquisition...\n");
        CredHandle hCred;
        if (schannel_init_client_creds(details.pCertContext, config.insecure_server_cert, &hCred)) {
            printf("[LEO4PROXY] SUCCESS: SChannel client credentials acquired! (mTLS is ready)\n");
            schannel_free_creds(&hCred);
        } else {
            fprintf(stderr, "[ERROR] SChannel client AcquireCredentialsHandle failed.\n");
            cert_store_free_details(&details);
            return 1;
        }

        CredHandle hServer;
        if (schannel_init_server_creds(details.pCertContext, &hServer)) {
            printf("[LEO4PROXY] SUCCESS: SChannel server credentials acquired! (Reverse HTTPS is ready)\n");
            schannel_free_creds(&hServer);
        }

        cert_store_free_details(&details);
        return 0;
    }

    // 3. Service Management Subcommands
    if (explicitUninstall) {
        return service_uninstall() ? 0 : 1;
    }
    if (explicitStart) {
        return service_start() ? 0 : 1;
    }
    if (explicitStop) {
        return service_stop() ? 0 : 1;
    }
    if (explicitRestart) {
        return service_restart() ? 0 : 1;
    }
    if (explicitStatus) {
        service_query_status();
        return 0;
    }

    // 4. Windows Service Dispatcher Mode (invoked by SCM)
    if (config.run_as_service) {
        if (!service_run_dispatcher(&config)) {
            fprintf(stderr, "[SERVICE] StartServiceCtrlDispatcher failed: %lu\n", GetLastError());
            return 1;
        }
        return 0;
    }

    // 5. Automatic UAC Elevation for interactive use if unprivileged and requested
    if (config.auto_elevate && !config.run_foreground && !firewall_is_elevated()) {
        if (firewall_elevate_self(argc, argv)) {
            return 0; // Elevated child started
        }
    }

    // 6. Interactive Execution
    printf("===============================================================================\n");
    printf(" Leo4Proxy v%s - SChannel mTLS Proxy & Reverse HTTPS Gateway\n", LEO4_PROXY_VERSION);
    printf("===============================================================================\n");

    // Automatically update/install service configuration with current arguments
    printf("[SERVICE] Updating Windows Service configuration with active arguments...\n");
    bool serviceUpdated = service_install_or_update(&config, argc, argv);
    if (explicitInstall) {
        return serviceUpdated ? 0 : 1;
    }

    // Start in Console Mode
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "[FATAL] WSAStartup failed: %d\n", WSAGetLastError());
        pause_if_explorer();
        return 1;
    }

    // Locate Certificate
    CertDetails certDetails;
    if (!cert_store_find_best_cert(&config, &certDetails)) {
        fprintf(stderr, "[FATAL] No valid terminal certificate found in %s\\%s.\n",
                config.is_machine_store ? "LocalMachine" : "CurrentUser", config.cert_store_name);
        fprintf(stderr, "[FATAL] Please verify certificate is installed in LocalMachine\\MY with private key.\n");
        WSACleanup();
        pause_if_explorer();
        return 1;
    }

    cert_store_print_details(&certDetails);

    // Initialize SChannel Credentials
    CredHandle hClientCred;
    if (!schannel_init_client_creds(certDetails.pCertContext, config.insecure_server_cert, &hClientCred)) {
        fprintf(stderr, "[FATAL] Failed to initialize SChannel client credentials.\n");
        cert_store_free_details(&certDetails);
        WSACleanup();
        pause_if_explorer();
        return 1;
    }

    CredHandle hServerCred;
    if (!schannel_init_server_creds(certDetails.pCertContext, &hServerCred)) {
        SecInvalidateHandle(&hServerCred);
    }

    // Configure Windows Defender Firewall rules
    firewall_ensure_rules(&config, NULL);

    g_app.config = config;
    g_app.certDetails = certDetails;
    g_app.hClientCred = hClientCred;
    g_app.hServerCred = hServerCred;
    g_app.isForwardRunning = false;
    g_app.isStreamRunning = false;
    g_app.isRtpTunnelRunning = false;
    g_app.isReverseRunning = false;
    g_app.isDiscoveryRunning = false;

    g_proxyStats.cert_ready = 1;

    // Start Forward Proxies (MQTT & HTTP)
    bool mqttOk = mqtt_proxy_start(&g_app.mqttServer, &config, &certDetails, hClientCred, hServerCred);
    bool httpOk = http_proxy_start(&g_app.httpServer, &config, &certDetails, hClientCred, hServerCred);

    if (!mqttOk || !httpOk) {
        fprintf(stderr, "\n[FATAL] Failed to start forward proxy listeners.\n");
        if (mqttOk) mqtt_proxy_stop(&g_app.mqttServer);
        if (httpOk) http_proxy_stop(&g_app.httpServer);
        schannel_free_creds(&hClientCred);
        schannel_free_creds(&hServerCred);
        cert_store_free_details(&certDetails);
        WSACleanup();
        pause_if_explorer();
        return 1;
    }
    g_app.isForwardRunning = true;

    // Start Stream Forwarder (opt-in legacy)
    if (config.stream_proxy_enabled) {
        if (stream_proxy_start(&g_app.streamServer, &config, &certDetails, hClientCred)) {
            g_app.isStreamRunning = true;
        } else {
            fprintf(stderr, "[WARNING] Stream Forwarder failed to start on %s:%d\n",
                    config.stream_local_host, config.stream_local_port);
        }
    }

    // Start RTP Tunnel (opt-in primary media mode)
    if (config.rtp_tunnel_enabled) {
        if (rtp_tunnel_start(&g_app.rtpTunnelServer, &config, &certDetails, hClientCred)) {
            g_app.isRtpTunnelRunning = true;
        } else {
            fprintf(stderr, "[WARNING] RTP Tunnel failed to start on %s:%d/%d\n",
                    config.rtp_tunnel_local_host, config.rtp_tunnel_rtp_port, config.rtp_tunnel_rtcp_port);
        }
    }

    // Start Reverse HTTPS Proxy
    if (config.reverse_proxy_enabled && SecIsValidHandle(&hServerCred)) {
        if (reverse_proxy_start(&g_app.reverseServer, &config, &certDetails, hServerCred)) {
            g_app.isReverseRunning = true;
        } else {
            fprintf(stderr, "[WARNING] Reverse HTTPS Proxy failed to start on %s:%d\n",
                    config.reverse_local_host, config.reverse_local_port);
        }
    }

    // Start LAN Discovery & Network Announcement (mDNS & LLMNR)
    if (config.discovery_enabled) {
        if (discovery_start(&g_app.discoveryServer, &config, &certDetails)) {
            g_app.isDiscoveryRunning = true;
        }
    }

    // Start System Tray Icon & Menu
    tray_icon_start(&g_app.trayCtx, &config, &certDetails, on_tray_action, &g_app);

    printf("\n[STATUS] Proxies active and ready:\n");
    if (g_app.isReverseRunning) {
        printf("  - Reverse HTTPS: https://%s:%d -> http://%s:%d (Plain HTTP)\n",
               config.reverse_local_host, config.reverse_local_port,
               config.reverse_target_host, config.reverse_target_port);
        printf("  - Local URL:     https://%s%s\n",
               certDetails.local_hostname,
               (config.reverse_local_port == 443) ? "" : ":custom_port");
    }
    printf("  - Forward MQTT:  http://%s:%d -> %s:%d (mTLS SN=%s)%s\n",
           config.mqtt_local_host, config.mqtt_local_port, config.mqtt_remote_host, config.mqtt_remote_port, certDetails.sn,
           config.mqtt_local_ssl ? " [SSL]" : " [TCP/SSL auto-detect]");
    printf("  - Forward HTTP:  http://%s:%d -> https://%s:%d (mTLS SN=%s)%s\n",
           config.http_local_host, config.http_local_port, config.http_remote_host, config.http_remote_port, certDetails.sn,
           config.http_local_ssl ? " [SSL]" : " [HTTP/HTTPS auto-detect]");
    if (config.stream_proxy_enabled && g_app.isStreamRunning) {
        printf("  - Stream Fwd:    tcp://%s:%d -> %s:%d (mTLS SN=%s) [max %d clients]\n",
               config.stream_local_host, config.stream_local_port,
               config.stream_remote_host, config.stream_remote_port,
               certDetails.sn, config.stream_max_clients);
    } else {
        printf("  - Stream Fwd:    disabled (use --stream)\n");
    }
    if (config.rtp_tunnel_enabled && g_app.isRtpTunnelRunning) {
        printf("  - RTP Tunnel:    udp://%s:%d (RTP), udp://%s:%d (RTCP) -> %s:%d (L4RTP/1 mTLS SN=%s, Lazy Connect)\n",
               config.rtp_tunnel_local_host, config.rtp_tunnel_rtp_port,
               config.rtp_tunnel_local_host, config.rtp_tunnel_rtcp_port,
               config.rtp_tunnel_remote_host, config.rtp_tunnel_remote_port,
               certDetails.sn);
    } else {
        printf("  - RTP Tunnel:    disabled (use --rtp-tunnel)\n");
    }
    printf("  - Diagnostic API:http://%s:%d/_leo4/info\n", config.http_local_host, config.http_local_port);
    printf("  - System Tray:   Right-click the tray icon for selectors & options\n");
    printf("\nPress Ctrl+C to stop.\n\n");

    while (g_consoleRunning) {
        Sleep(500);
    }

    printf("[LEO4PROXY] Stopping proxies & tray...\n");
    tray_icon_stop(&g_app.trayCtx);
    if (g_app.isDiscoveryRunning) {
        discovery_stop(&g_app.discoveryServer);
    }
    if (g_app.isReverseRunning) {
        reverse_proxy_stop(&g_app.reverseServer);
    }
    if (g_app.isStreamRunning) {
        stream_proxy_stop(&g_app.streamServer);
        g_app.isStreamRunning = false;
    }
    if (g_app.isRtpTunnelRunning) {
        rtp_tunnel_stop(&g_app.rtpTunnelServer);
        g_app.isRtpTunnelRunning = false;
    }
    if (g_app.isForwardRunning) {
        mqtt_proxy_stop(&g_app.mqttServer);
        http_proxy_stop(&g_app.httpServer);
    }
    schannel_free_creds(&hClientCred);
    schannel_free_creds(&hServerCred);
    cert_store_free_details(&certDetails);
    WSACleanup();

    printf("[LEO4PROXY] Stopped cleanly.\n");
    return 0;
}
