#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "config.h"
#include "mqtt_client.h"
#include "desktop_state.h"
#include "log.h"
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static HANDLE g_stop_event = NULL;

static BOOL WINAPI console_ctrl_handler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            log_info("Console stop signal received. Initiating graceful shutdown...");
            if (g_stop_event) {
                SetEvent(g_stop_event);
            }
            return TRUE;
        default:
            return FALSE;
    }
}

// Background watcher for supervisor stop event (Global\L4Desk_Stop_<SN> or Global\L4Desk_Stop)
static DWORD WINAPI supervisor_stop_watcher_thread(LPVOID param) {
    const L4DeskConfig* cfg = (const L4DeskConfig*)param;
    wchar_t event_name[128] = { 0 };

    if (cfg->sn[0] != '\0') {
        swprintf_s(event_name, sizeof(event_name) / sizeof(wchar_t), L"Global\\L4Desk_Stop_%hs", cfg->sn);
    } else {
        swprintf_s(event_name, sizeof(event_name) / sizeof(wchar_t), L"Global\\L4Desk_Stop");
    }

    HANDLE hNamedStop = CreateEventW(NULL, TRUE, FALSE, event_name);
    if (!hNamedStop) {
        hNamedStop = OpenEventW(SYNCHRONIZE, FALSE, event_name);
    }

    if (hNamedStop) {
        HANDLE wait_handles[2] = { g_stop_event, hNamedStop };
        DWORD wait_rc = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        if (wait_rc == WAIT_OBJECT_0 + 1) {
            log_info("Supervisor stop event (%ls) signaled. Stopping l4desk...", event_name);
            SetEvent(g_stop_event);
        }
        CloseHandle(hNamedStop);
    }

    return 0;
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    L4DeskConfig config;
    config_init_defaults(&config);
    config_parse_args(&config, argc, argv);

    // Initialize logger
    log_init(config.log_file, config.verbose, config.console_mode);

    log_info("===============================================================================");
    log_info("  l4desk v%s - Terminal Remote Input Agent", L4DESK_VERSION_STR);
    log_info("  Mode: %s | Broker: %s:%d | Proxy Port: %d | Client ID: %s",
             config.run_mode ? "Supervised (--run)" : "Console",
             config.mqtt_host, config.mqtt_port, config.proxy_http_port, config.client_id);
    log_info("===============================================================================");

    // Single-instance enforcement (per session)
    wchar_t mutex_name[128] = L"Local\\L4Desk_SingleInstance";
    if (config.sn_explicitly_set && config.sn[0] != '\0') {
        swprintf_s(mutex_name, sizeof(mutex_name) / sizeof(wchar_t), L"Local\\L4Desk_SingleInstance_%hs", config.sn);
    }
    HANDLE hSingleMutex = CreateMutexW(NULL, FALSE, mutex_name);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        log_warn("Another instance of l4desk is already running in this session (%ls). Exiting.", mutex_name);
        if (hSingleMutex) CloseHandle(hSingleMutex);
        log_close();
        return 0;
    }

    // Check Session 0
    DWORD sessionId = desktop_get_current_session_id();
    log_info("Process running in Session ID: %lu", sessionId);
    if (sessionId == 0) {
        log_warn("WARNING: Running in session 0 — interactive desktop input injection will fail!");
    }

    g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_stop_event) {
        log_error("CreateEvent failed: %lu", GetLastError());
        CloseHandle(hSingleMutex);
        log_close();
        return 1;
    }

    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    // Start named stop event watcher for l4superv
    HANDLE hWatcherThread = CreateThread(NULL, 0, supervisor_stop_watcher_thread, &config, 0, NULL);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        log_error("WSAStartup failed: %d", WSAGetLastError());
        CloseHandle(g_stop_event);
        CloseHandle(hSingleMutex);
        log_close();
        return 1;
    }

    int rc = mqtt_client_run(&config, g_stop_event);

    if (hWatcherThread) {
        WaitForSingleObject(hWatcherThread, 1000);
        CloseHandle(hWatcherThread);
    }

    WSACleanup();
    CloseHandle(g_stop_event);
    CloseHandle(hSingleMutex);

    log_info("l4desk terminated cleanly.");
    log_close();
    return rc;
}
