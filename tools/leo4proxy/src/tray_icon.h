#pragma once

#include "config.h"
#include "cert_store.h"
#include <windows.h>
#include <stdbool.h>

typedef enum {
    TRAY_STATE_RUNNING = 1,
    TRAY_STATE_STOPPED = 2,
    TRAY_STATE_ERROR   = 3
} TrayState;

typedef void (*TrayActionCallback)(int action_id, void* user_data);

typedef struct {
    const ProxyConfig* config;
    const CertDetails* certDetails;
    TrayState currentState;
    TrayActionCallback onAction;
    void* userData;
    HWND hWnd;
    HANDLE hThread;
    volatile bool isRunning;
} TrayIconContext;

/**
 * @brief Starts the System Tray icon and message loop thread.
 */
bool tray_icon_start(TrayIconContext* ctx, const ProxyConfig* config, const CertDetails* certDetails, TrayActionCallback onAction, void* userData);

/**
 * @brief Updates the tray icon state (Running / Stopped / Error) and tooltip.
 */
void tray_icon_set_state(TrayIconContext* ctx, TrayState state);

/**
 * @brief Displays the Information & Details dialog box.
 */
void tray_icon_show_info_dialog(TrayIconContext* ctx);

/**
 * @brief Stops the tray message loop and removes icon from Taskbar.
 */
void tray_icon_stop(TrayIconContext* ctx);
