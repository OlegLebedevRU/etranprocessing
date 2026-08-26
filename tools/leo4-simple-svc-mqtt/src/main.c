/**
 * @file main.c
 * @brief Entry point for leo4-simple-svc-mqtt (extra_service MQTT presence & LWT).
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "service_mgr.h"
#include "mqtt_client.h"

static HANDLE g_consoleStopEvent = NULL;

static BOOL WINAPI console_ctrl_handler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            printf("\n[CONSOLE] Stop signal received. Initiating graceful shutdown...\n");
            if (g_consoleStopEvent) {
                SetEvent(g_consoleStopEvent);
            }
            return TRUE;
        default:
            return FALSE;
    }
}

int main(int argc, char* argv[]) {
    // Set console code page to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    AppConfig config;
    config_init_defaults(&config);

    bool is_service_cmd = false;
    config_parse_args(&config, argc, argv, &is_service_cmd);

    // 1. Service Management Commands
    for (int i = 1; i < argc; i++) {
        if (_stricmp(argv[i], "--install") == 0) {
            return service_install_or_update(&config, argc, argv) ? 0 : 1;
        }
        if (_stricmp(argv[i], "--uninstall") == 0) {
            return service_uninstall() ? 0 : 1;
        }
        if (_stricmp(argv[i], "--start") == 0) {
            return service_start() ? 0 : 1;
        }
        if (_stricmp(argv[i], "--stop") == 0) {
            return service_stop() ? 0 : 1;
        }
        if (_stricmp(argv[i], "--restart") == 0) {
            return service_restart() ? 0 : 1;
        }
        if (_stricmp(argv[i], "--status") == 0) {
            service_query_status();
            return 0;
        }
        if (_stricmp(argv[i], "--service") == 0) {
            return service_run_dispatcher(&config) ? 0 : 1;
        }
    }

    // 2. Interactive / Foreground Console Mode
    // If not invoked as --service, try running as service dispatcher first;
    // if that fails with ERROR_FAILED_SERVICE_CONTROLLER_CONNECT, run in console mode.
    if (!config.foreground) {
        if (service_run_dispatcher(&config)) {
            return 0;
        }
        if (GetLastError() != ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            // Unexpected service dispatcher error
            fprintf(stderr, "[FATAL] Service dispatcher failed: %lu\n", GetLastError());
            return 1;
        }
    }

    // Interactive console mode
    printf("===============================================================================\n");
    printf("  %s v%s - Interactive Mode (Role: %s)\n",
           LEO4_SVC_APP_NAME, LEO4_SVC_APP_VERSION, config.role);
    printf("  Target: %s:%d | Leo4Proxy Port: %d\n",
           config.mqtt_host, config.mqtt_port, config.proxy_http_port);
    printf("  Press Ctrl+C to trigger graceful shutdown (svc_offline retain=1 -> DISCONNECT)\n");
    printf("===============================================================================\n\n");

    g_consoleStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_consoleStopEvent) {
        fprintf(stderr, "[FATAL] CreateEvent failed: %lu\n", GetLastError());
        return 1;
    }

    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "[FATAL] WSAStartup failed: %d\n", WSAGetLastError());
        CloseHandle(g_consoleStopEvent);
        return 1;
    }

    int rc = mqtt_client_run(&config, g_consoleStopEvent);

    WSACleanup();
    CloseHandle(g_consoleStopEvent);
    g_consoleStopEvent = NULL;

    return rc;
}
