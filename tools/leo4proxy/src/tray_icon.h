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
    bool isReverseProxyRunning;
    bool isForwardProxyRunning;
    char localHostname[256];
    char lanIp[64];
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
 * @brief Updates the overall tray icon state (Running / Stopped / Error) and tooltip.
 */
void tray_icon_set_state(TrayIconContext* ctx, TrayState state);

/**
 * @brief Updates reverse proxy running state in tray icon context.
 */
void tray_icon_set_reverse_state(TrayIconContext* ctx, bool isRunning);

/**
 * @brief Updates forward proxy running state in tray icon context.
 */
void tray_icon_set_forward_state(TrayIconContext* ctx, bool isRunning);

/**
 * @brief Displays the Information & Details dialog box.
 */
void tray_icon_show_info_dialog(TrayIconContext* ctx);

/**
 * @brief Stops the tray message loop and removes icon from Taskbar.
 */
void tray_icon_stop(TrayIconContext* ctx);
