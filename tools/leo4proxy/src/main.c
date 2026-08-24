/**
 * @file main.c
 * @brief Leo4Proxy - Unified Windows SChannel mTLS Proxy for MQTT and HTTPS.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "cert_store.h"
#include "schannel_tls.h"
#include "mqtt_proxy.h"
#include "http_proxy.h"
#include "service_mgr.h"

static volatile bool g_consoleRunning = true;

static void pause_if_explorer(void) {
    DWORD pids[2];
    DWORD count = GetConsoleProcessList(pids, 2);
    // If count <= 1, this process is the sole process attached to the console (launched from Explorer)
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

    strncpy_s(config->cert_store_name, sizeof(config->cert_store_name), "MY", _TRUNCATE);
    config->is_machine_store = 1;      // Default: LocalMachine\MY
    config->insecure_server_cert = 1;  // Default: ignore untrusted server CA for dev/migration

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
    // Skip optional scheme
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

static void print_usage(const char* exeName) {
    printf("===============================================================================\n");
    printf(" Leo4Proxy v%s - Windows SChannel mTLS Proxy for MQTT & HTTPS\n", LEO4_PROXY_VERSION);
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
    printf("  -f, --console           Run in foreground console mode (default when run interactively)\n");
    printf("  -v, --verbose           Enable verbose connection debugging logs\n");
    printf("  -h, --help              Show this help message\n\n");
    printf("PROXY ENDPOINT CONFIGURATION:\n");
    printf("  --mqtt-remote <host:port> Remote MQTT Broker (default: %s:%d)\n", DEFAULT_MQTT_REMOTE_HOST, DEFAULT_MQTT_REMOTE_PORT);
    printf("  --mqtt-local  <ip:port>   Local MQTT listener (default: %s:%d)\n", DEFAULT_MQTT_LOCAL_HOST, DEFAULT_MQTT_LOCAL_PORT);
    printf("  --http-remote <host:port> Remote HTTPS Backend (default: %s:%d)\n", DEFAULT_HTTP_REMOTE_HOST, DEFAULT_HTTP_REMOTE_PORT);
    printf("  --http-local  <ip:port>   Local HTTP listener (default: %s:%d)\n", DEFAULT_HTTP_LOCAL_HOST, DEFAULT_HTTP_LOCAL_PORT);
    printf("  --local-ssl               Enforce SSL/TLS on both local listeners (default: auto-detect)\n");
    printf("  --http-local-ssl          Enforce SSL/TLS on local HTTP listener\n");
    printf("  --mqtt-local-ssl          Enforce SSL/TLS on local MQTT listener\n");
    printf("  --secure                  Strict server certificate CA validation (default: lax/insecure)\n\n");
    printf("CERTIFICATE SELECTION:\n");
    printf("  --cert-email <pattern>    Filter certs by email (default: newest %s -> %s)\n", DEFAULT_CERT_EMAIL_PRIMARY, DEFAULT_CERT_EMAIL_FALLBACK);
    printf("  --cert-thumbprint <sha1>  Select specific certificate by SHA-1 thumbprint\n");
    printf("  --user-store              Search CurrentUser\\MY instead of LocalMachine\\MY\n\n");
    printf("EXAMPLES:\n");
    printf("  1. Check device SN for scripts:\n");
    printf("       %s --get-sn\n\n", exeName);
    printf("  2. Test certificate from Windows Store:\n");
    printf("       %s --test-cert\n\n", exeName);
    printf("  3. Run in console mode:\n");
    printf("       %s -f --verbose\n\n", exeName);
    printf("  4. Install/Update Windows Service with custom broker:\n");
    printf("       %s --install --mqtt-remote dev.leo4.ru:8883 --http-remote iot-processing.ru:443\n\n", exeName);
    printf("  5. Query internal metadata from local clients:\n");
    printf("       curl http://127.0.0.1:18443/_leo4/info\n");
    printf("       curl http://127.0.0.1:18443/_leo4/sn\n\n");
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
            printf("[LEO4PROXY] SUCCESS: SChannel credentials acquired successfully! (mTLS is ready)\n");
            schannel_free_creds(&hCred);
        } else {
            fprintf(stderr, "[ERROR] SChannel AcquireCredentialsHandle failed.\n");
            cert_store_free_details(&details);
            return 1;
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

    // 5. Interactive Execution
    printf("===============================================================================\n");
    printf(" Leo4Proxy v%s - Windows SChannel mTLS Proxy for MQTT & HTTPS\n", LEO4_PROXY_VERSION);
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

    // Start Proxies
    MqttProxyServer mqttServer;
    HttpProxyServer httpServer;

    bool mqttOk = mqtt_proxy_start(&mqttServer, &config, &certDetails, hClientCred, hServerCred);
    bool httpOk = http_proxy_start(&httpServer, &config, &certDetails, hClientCred, hServerCred);

    if (!mqttOk || !httpOk) {
        fprintf(stderr, "\n[FATAL] Failed to start proxy listeners.\n");
        if (mqttOk) mqtt_proxy_stop(&mqttServer);
        if (httpOk) http_proxy_stop(&httpServer);
        schannel_free_creds(&hClientCred);
        schannel_free_creds(&hServerCred);
        cert_store_free_details(&certDetails);
        WSACleanup();
        pause_if_explorer();
        return 1;
    }

    printf("\n[STATUS] Proxies active and ready for client connections:\n");
    printf("  - MQTT Proxy: http://%s:%d -> %s:%d (mTLS SN=%s)%s\n",
           config.mqtt_local_host, config.mqtt_local_port, config.mqtt_remote_host, config.mqtt_remote_port, certDetails.sn,
           config.mqtt_local_ssl ? " [SSL]" : " [TCP/SSL auto-detect]");
    printf("  - HTTP Proxy: http://%s:%d -> https://%s:%d (mTLS SN=%s)%s\n",
           config.http_local_host, config.http_local_port, config.http_remote_host, config.http_remote_port, certDetails.sn,
           config.http_local_ssl ? " [SSL]" : " [HTTP/HTTPS auto-detect]");
    printf("  - Info API:   http://%s:%d/_leo4/info\n", config.http_local_host, config.http_local_port);
    printf("  - Device SN:  http://%s:%d/_leo4/sn\n", config.http_local_host, config.http_local_port);
    printf("\nPress Ctrl+C to stop.\n\n");

    while (g_consoleRunning) {
        Sleep(500);
    }

    printf("[LEO4PROXY] Stopping proxies...\n");
    mqtt_proxy_stop(&mqttServer);
    http_proxy_stop(&httpServer);
    schannel_free_creds(&hClientCred);
    schannel_free_creds(&hServerCred);
    cert_store_free_details(&certDetails);
    WSACleanup();

    printf("[LEO4PROXY] Stopped cleanly.\n");
    return 0;
}
