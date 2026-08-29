/**
 * @file service_mgr.c
 * @brief Windows Service management and service dispatcher for Leo4Proxy.
 */

#include "service_mgr.h"
#include "cert_store.h"
#include "schannel_tls.h"
#include "mqtt_proxy.h"
#include "http_proxy.h"
#include "reverse_proxy.h"
#include "discovery.h"
#include "firewall.h"
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "advapi32.lib")

static SERVICE_STATUS g_serviceStatus = { 0 };
static SERVICE_STATUS_HANDLE g_statusHandle = NULL;
static HANDLE g_stopEvent = NULL;
static ProxyConfig g_serviceConfig;

static void report_service_status(DWORD currentState, DWORD win32ExitCode, DWORD waitHint) {
    static DWORD checkPoint = 1;

    g_serviceStatus.dwCurrentState = currentState;
    g_serviceStatus.dwWin32ExitCode = win32ExitCode;
    g_serviceStatus.dwWaitHint = waitHint;

    if (currentState == SERVICE_START_PENDING) {
        g_serviceStatus.dwControlsAccepted = 0;
    } else {
        g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }

    if (currentState == SERVICE_RUNNING || currentState == SERVICE_STOPPED) {
        g_serviceStatus.dwCheckPoint = 0;
    } else {
        g_serviceStatus.dwCheckPoint = checkPoint++;
    }

    SetServiceStatus(g_statusHandle, &g_serviceStatus);
}

static DWORD WINAPI service_ctrl_handler_ex(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext) {
    (void)dwEventType;
    (void)lpEventData;
    (void)lpContext;

    switch (dwControl) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            report_service_status(SERVICE_STOP_PENDING, NO_ERROR, 3000);
            if (g_stopEvent) {
                SetEvent(g_stopEvent);
            }
            return NO_ERROR;

        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;

        default:
            break;
    }

    return ERROR_CALL_NOT_IMPLEMENTED;
}

static void WINAPI service_main(DWORD argc, LPWSTR* argv) {
    (void)argc;
    (void)argv;

    g_statusHandle = RegisterServiceCtrlHandlerExW(LEO4_SERVICE_NAME, service_ctrl_handler_ex, NULL);
    if (!g_statusHandle) {
        return;
    }

    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwServiceSpecificExitCode = 0;
    report_service_status(SERVICE_START_PENDING, NO_ERROR, 5000);

    g_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_stopEvent) {
        report_service_status(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    // 1. Initialize Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        report_service_status(SERVICE_STOPPED, WSAGetLastError(), 0);
        return;
    }

    // Configure firewall rules
    firewall_ensure_rules(&g_serviceConfig, NULL);

    MqttProxyServer mqttServer;
    HttpProxyServer httpServer;
    ReverseProxyServer reverseServer;
    DiscoveryServer discoveryServer;
    memset(&mqttServer, 0, sizeof(mqttServer));
    memset(&httpServer, 0, sizeof(httpServer));
    memset(&reverseServer, 0, sizeof(reverseServer));
    memset(&discoveryServer, 0, sizeof(discoveryServer));

    CredHandle hClientCred;
    CredHandle hServerCred;
    SecInvalidateHandle(&hClientCred);
    SecInvalidateHandle(&hServerCred);

    CertDetails certDetails;
    memset(&certDetails, 0, sizeof(certDetails));

    bool isCertLoaded = false;
    bool isMqttStarted = false;
    bool isReverseStarted = false;
    bool isDiscoveryStarted = false;

    // Report service as RUNNING immediately
    report_service_status(SERVICE_RUNNING, NO_ERROR, 0);

    // Start HTTP diagnostic listener in standby mode (route proxying disabled until cert is loaded)
    bool isHttpStarted = http_proxy_start(&httpServer, &g_serviceConfig, NULL, hClientCred, hServerCred);
    if (!isHttpStarted) {
        fprintf(stderr, "[SERVICE] Warning: Failed to start base HTTP listener on %s:%d\n",
                g_serviceConfig.http_local_host, g_serviceConfig.http_local_port);
    }

    // Main Service Loop: periodic non-aggressive check for certificate rotation and expiration
    DWORD pollIntervalMs = (g_serviceConfig.cert_poll_interval > 0 ? (DWORD)g_serviceConfig.cert_poll_interval : 30) * 1000;
    DWORD standbyIntervalMs = 5000;

    while (WaitForSingleObject(g_stopEvent, 0) == WAIT_TIMEOUT) {
        if (!isCertLoaded) {
            CertDetails newDetails;
            memset(&newDetails, 0, sizeof(newDetails));
            if (cert_store_find_best_cert(&g_serviceConfig, &newDetails)) {
                CredHandle newClientCred, newServerCred;
                SecInvalidateHandle(&newClientCred);
                SecInvalidateHandle(&newServerCred);

                bool clientOk = schannel_init_client_creds(newDetails.pCertContext, g_serviceConfig.insecure_server_cert, &newClientCred);
                if (!schannel_init_server_creds(newDetails.pCertContext, &newServerCred)) {
                    SecInvalidateHandle(&newServerCred);
                }

                if (clientOk) {
                    certDetails = newDetails;
                    hClientCred = newClientCred;
                    hServerCred = newServerCred;
                    isCertLoaded = true;
                    g_proxyStats.cert_ready = 1;

                    printf("[SERVICE] Terminal certificate loaded successfully! SN: %s, Hostname: %s\n",
                           certDetails.sn, certDetails.local_hostname);

                    // Update HTTP proxy with valid credentials
                    http_proxy_update_creds(&httpServer, &certDetails, hClientCred, hServerCred);

                    // Start MQTT proxy
                    if (!isMqttStarted) {
                        isMqttStarted = mqtt_proxy_start(&mqttServer, &g_serviceConfig, &certDetails, hClientCred, hServerCred);
                    }

                    // Start Reverse HTTPS proxy
                    if (!isReverseStarted && g_serviceConfig.reverse_proxy_enabled && SecIsValidHandle(&hServerCred)) {
                        isReverseStarted = reverse_proxy_start(&reverseServer, &g_serviceConfig, &certDetails, hServerCred);
                    }

                    // Start Discovery
                    if (!isDiscoveryStarted && g_serviceConfig.discovery_enabled) {
                        isDiscoveryStarted = discovery_start(&discoveryServer, &g_serviceConfig, &certDetails);
                    }
                } else {
                    if (SecIsValidHandle(&newClientCred)) schannel_free_creds(&newClientCred);
                    if (SecIsValidHandle(&newServerCred)) schannel_free_creds(&newServerCred);
                    cert_store_free_details(&newDetails);
                }
            }
        } else {
            // Certificate is currently loaded.
            // 1. Check if current certificate has expired and drop_on_expire is enabled
            bool isExpired = cert_store_is_cert_expired(&certDetails);

            // 2. Poll store for best available certificate (finds rotated / newer cert, or skips expired if drop_on_expire is set)
            CertDetails newDetails;
            memset(&newDetails, 0, sizeof(newDetails));
            bool foundBest = cert_store_find_best_cert(&g_serviceConfig, &newDetails);

            if (foundBest) {
                // Check if the certificate in store is different from our active one (by thumbprint)
                if (_stricmp(newDetails.thumbprint, certDetails.thumbprint) != 0) {
                    printf("[SERVICE] New/updated certificate detected in store! Thumbprint: %s (old: %s). Hot-swapping...\n",
                           newDetails.thumbprint, certDetails.thumbprint);

                    CredHandle newClientCred, newServerCred;
                    SecInvalidateHandle(&newClientCred);
                    SecInvalidateHandle(&newServerCred);

                    bool clientOk = schannel_init_client_creds(newDetails.pCertContext, g_serviceConfig.insecure_server_cert, &newClientCred);
                    if (!schannel_init_server_creds(newDetails.pCertContext, &newServerCred)) {
                        SecInvalidateHandle(&newServerCred);
                    }

                    if (clientOk) {
                        bool snChanged = (_stricmp(newDetails.sn, certDetails.sn) != 0 ||
                                          _stricmp(newDetails.local_hostname, certDetails.local_hostname) != 0);

                        // Save old handles to free after switch
                        CredHandle oldClientCred = hClientCred;
                        CredHandle oldServerCred = hServerCred;
                        CertDetails oldDetails = certDetails;

                        // Swap active state
                        certDetails = newDetails;
                        hClientCred = newClientCred;
                        hServerCred = newServerCred;

                        // 1. Update HTTP Proxy credentials
                        http_proxy_update_creds(&httpServer, &certDetails, hClientCred, hServerCred);

                        // 2. Restart MQTT Proxy (closes active client connections so mosquitto bridge reconnects immediately with new cert)
                        if (isMqttStarted) {
                            mqtt_proxy_stop(&mqttServer);
                            isMqttStarted = false;
                        }
                        isMqttStarted = mqtt_proxy_start(&mqttServer, &g_serviceConfig, &certDetails, hClientCred, hServerCred);

                        // 3. Restart Reverse HTTPS Proxy (drops inbound TLS sessions and binds with new server cert)
                        if (isReverseStarted) {
                            reverse_proxy_stop(&reverseServer);
                            isReverseStarted = false;
                        }
                        if (g_serviceConfig.reverse_proxy_enabled && SecIsValidHandle(&hServerCred)) {
                            isReverseStarted = reverse_proxy_start(&reverseServer, &g_serviceConfig, &certDetails, hServerCred);
                        }

                        // 4. Restart Discovery if SN/hostname changed
                        if (snChanged) {
                            if (isDiscoveryStarted) {
                                discovery_stop(&discoveryServer);
                                isDiscoveryStarted = false;
                            }
                            if (g_serviceConfig.discovery_enabled) {
                                isDiscoveryStarted = discovery_start(&discoveryServer, &g_serviceConfig, &certDetails);
                            }
                        }

                        // Free old credentials and details
                        if (SecIsValidHandle(&oldClientCred)) schannel_free_creds(&oldClientCred);
                        if (SecIsValidHandle(&oldServerCred)) schannel_free_creds(&oldServerCred);
                        cert_store_free_details(&oldDetails);

                        printf("[SERVICE] Certificate hot-swap COMPLETE! Active SN: %s, Hostname: %s\n",
                               certDetails.sn, certDetails.local_hostname);
                    } else {
                        fprintf(stderr, "[SERVICE] Failed to acquire SChannel credentials for new certificate. Keeping current cert.\n");
                        if (SecIsValidHandle(&newClientCred)) schannel_free_creds(&newClientCred);
                        if (SecIsValidHandle(&newServerCred)) schannel_free_creds(&newServerCred);
                        cert_store_free_details(&newDetails);
                    }
                } else {
                    // Same certificate, free temporary details
                    cert_store_free_details(&newDetails);
                }
            } else if (isExpired && g_serviceConfig.drop_on_expire) {
                // No valid non-expired certificate found and drop_on_expire is enabled -> transition to standby mode
                printf("[SERVICE] Active certificate (SN: %s) expired and --drop-on-expire is enabled! Transitioning to standby mode...\n",
                       certDetails.sn);

                if (isDiscoveryStarted) {
                    discovery_stop(&discoveryServer);
                    isDiscoveryStarted = false;
                }
                if (isReverseStarted) {
                    reverse_proxy_stop(&reverseServer);
                    isReverseStarted = false;
                }
                if (isMqttStarted) {
                    mqtt_proxy_stop(&mqttServer);
                    isMqttStarted = false;
                }

                // Update HTTP proxy back to standby (no valid cert)
                CredHandle invalidCred;
                SecInvalidateHandle(&invalidCred);
                http_proxy_update_creds(&httpServer, NULL, invalidCred, invalidCred);

                if (SecIsValidHandle(&hClientCred)) schannel_free_creds(&hClientCred);
                if (SecIsValidHandle(&hServerCred)) schannel_free_creds(&hServerCred);
                SecInvalidateHandle(&hClientCred);
                SecInvalidateHandle(&hServerCred);

                cert_store_free_details(&certDetails);
                isCertLoaded = false;
                g_proxyStats.cert_ready = 0;
            }
        }

        // Wait poll interval or until stop signal
        DWORD waitMs = isCertLoaded ? pollIntervalMs : standbyIntervalMs;
        if (WaitForSingleObject(g_stopEvent, waitMs) != WAIT_TIMEOUT) {
            break;
        }
    }

    // Service Stop & Cleanup
    report_service_status(SERVICE_STOP_PENDING, NO_ERROR, 5000);

    if (isDiscoveryStarted) discovery_stop(&discoveryServer);
    if (isReverseStarted) reverse_proxy_stop(&reverseServer);
    if (isMqttStarted) mqtt_proxy_stop(&mqttServer);
    if (isHttpStarted) http_proxy_stop(&httpServer);

    if (SecIsValidHandle(&hClientCred)) schannel_free_creds(&hClientCred);
    if (SecIsValidHandle(&hServerCred)) schannel_free_creds(&hServerCred);
    if (isCertLoaded) cert_store_free_details(&certDetails);

    WSACleanup();
    CloseHandle(g_stopEvent);
    g_stopEvent = NULL;

    report_service_status(SERVICE_STOPPED, NO_ERROR, 0);
}

bool service_install_or_update(const ProxyConfig* config, int argc, char* argv[]) {
    (void)config;
    WCHAR szExePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, szExePath, MAX_PATH) == 0) {
        fprintf(stderr, "[SERVICE] GetModuleFileName failed: %lu\n", GetLastError());
        return false;
    }

    // Build command line string: "C:\path\leo4proxy.exe" --service <custom_args...>
    WCHAR szCmdLine[2048];
    int pos = swprintf_s(szCmdLine, sizeof(szCmdLine) / sizeof(WCHAR), L"\"%s\" --service", szExePath);

    // Reconstruct persistent arguments
    for (int i = 1; i < argc; i++) {
        if (_stricmp(argv[i], "--install") == 0 ||
            _stricmp(argv[i], "--uninstall") == 0 ||
            _stricmp(argv[i], "--start") == 0 ||
            _stricmp(argv[i], "--stop") == 0 ||
            _stricmp(argv[i], "--restart") == 0 ||
            _stricmp(argv[i], "--console") == 0 ||
            _stricmp(argv[i], "--foreground") == 0 ||
            _stricmp(argv[i], "-f") == 0 ||
            _stricmp(argv[i], "--service") == 0 ||
            _stricmp(argv[i], "--get-sn") == 0 ||
            _stricmp(argv[i], "--status") == 0 ||
            _stricmp(argv[i], "--test-cert") == 0) {
            continue;
        }

        WCHAR wArg[256];
        MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, wArg, sizeof(wArg) / sizeof(WCHAR));
        pos += swprintf_s(szCmdLine + pos, (sizeof(szCmdLine) / sizeof(WCHAR)) - pos, L" %s", wArg);
    }

    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            printf("[SERVICE] Note: Administrator privileges required to configure Windows Service.\n");
        } else {
            fprintf(stderr, "[SERVICE] OpenSCManager failed: %lu\n", err);
        }
        return false;
    }

    SC_HANDLE hService = OpenServiceW(hSCM, LEO4_SERVICE_NAME, SERVICE_ALL_ACCESS);
    bool isNew = false;

    if (!hService) {
        if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
            isNew = true;
            hService = CreateServiceW(
                hSCM,
                LEO4_SERVICE_NAME,
                LEO4_SERVICE_DISPLAY_NAME,
                SERVICE_ALL_ACCESS,
                SERVICE_WIN32_OWN_PROCESS,
                SERVICE_AUTO_START,
                SERVICE_ERROR_NORMAL,
                szCmdLine,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL
            );
            if (!hService) {
                fprintf(stderr, "[SERVICE] CreateService failed: %lu\n", GetLastError());
                CloseServiceHandle(hSCM);
                return false;
            }
            printf("[SERVICE] Service '%ls' created successfully with AUTO_START.\n", LEO4_SERVICE_NAME);
        } else {
            fprintf(stderr, "[SERVICE] OpenService failed: %lu\n", GetLastError());
            CloseServiceHandle(hSCM);
            return false;
        }
    } else {
        // Update existing service binary path and auto-start setting
        if (!ChangeServiceConfigW(
                hService,
                SERVICE_NO_CHANGE,
                SERVICE_AUTO_START,
                SERVICE_NO_CHANGE,
                szCmdLine,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL,
                NULL
            )) {
            fprintf(stderr, "[SERVICE] ChangeServiceConfig failed: %lu\n", GetLastError());
        } else {
            printf("[SERVICE] Service '%ls' configuration updated with current arguments:\n  %ls\n",
                   LEO4_SERVICE_NAME, szCmdLine);
        }
    }

    // Configure Failure Actions (Auto-Restart on Crash/Failure)
    SC_ACTION actions[3];
    actions[0].Type = SC_ACTION_RESTART;
    actions[0].Delay = 5000;   // 5 seconds
    actions[1].Type = SC_ACTION_RESTART;
    actions[1].Delay = 10000;  // 10 seconds
    actions[2].Type = SC_ACTION_RESTART;
    actions[2].Delay = 30000;  // 30 seconds

    SERVICE_FAILURE_ACTIONSW sfa = { 0 };
    sfa.dwResetPeriod = 86400; // Reset fail count after 1 day
    sfa.lpRebootMsg = NULL;
    sfa.lpCommand = NULL;
    sfa.cActions = 3;
    sfa.lpsaActions = actions;

    if (!ChangeServiceConfig2W(hService, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa)) {
        fprintf(stderr, "[SERVICE] ChangeServiceConfig2(FAILURE_ACTIONS) failed: %lu\n", GetLastError());
    } else {
        printf("[SERVICE] Configured auto-recovery actions (auto-restart on crash: 5s, 10s, 30s).\n");
    }

    // Set Description
    SERVICE_DESCRIPTIONW sd;
    sd.lpDescription = (LPWSTR)LEO4_SERVICE_DESC;
    ChangeServiceConfig2W(hService, SERVICE_CONFIG_DESCRIPTION, &sd);

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    return true;
}

bool service_uninstall(void) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        fprintf(stderr, "[SERVICE] OpenSCManager failed: %lu (Administrator rights required)\n", GetLastError());
        return false;
    }

    SC_HANDLE hService = OpenServiceW(hSCM, LEO4_SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (!hService) {
        fprintf(stderr, "[SERVICE] Service '%ls' not found: %lu\n", LEO4_SERVICE_NAME, GetLastError());
        CloseServiceHandle(hSCM);
        return false;
    }

    // Stop if running
    SERVICE_STATUS status;
    ControlService(hService, SERVICE_CONTROL_STOP, &status);

    if (DeleteService(hService)) {
        printf("[SERVICE] Service '%ls' uninstalled successfully.\n", LEO4_SERVICE_NAME);
        firewall_remove_rules();
    } else {
        fprintf(stderr, "[SERVICE] DeleteService failed: %lu\n", GetLastError());
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    return true;
}

bool service_start(void) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        fprintf(stderr, "[SERVICE] OpenSCManager failed: %lu (Administrator rights required)\n", GetLastError());
        return false;
    }

    SC_HANDLE hService = OpenServiceW(hSCM, LEO4_SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (!hService) {
        fprintf(stderr, "[SERVICE] Service '%ls' not found: %lu\n", LEO4_SERVICE_NAME, GetLastError());
        CloseServiceHandle(hSCM);
        return false;
    }

    if (StartServiceW(hService, 0, NULL)) {
        printf("[SERVICE] Start command sent to service '%ls'.\n", LEO4_SERVICE_NAME);
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_ALREADY_RUNNING) {
            printf("[SERVICE] Service '%ls' is already running.\n", LEO4_SERVICE_NAME);
        } else {
            fprintf(stderr, "[SERVICE] StartService failed: %lu\n", err);
        }
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    return true;
}

bool service_stop(void) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        fprintf(stderr, "[SERVICE] OpenSCManager failed: %lu (Administrator rights required)\n", GetLastError());
        return false;
    }

    SC_HANDLE hService = OpenServiceW(hSCM, LEO4_SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (!hService) {
        fprintf(stderr, "[SERVICE] Service '%ls' not found: %lu\n", LEO4_SERVICE_NAME, GetLastError());
        CloseServiceHandle(hSCM);
        return false;
    }

    SERVICE_STATUS status;
    if (ControlService(hService, SERVICE_CONTROL_STOP, &status)) {
        printf("[SERVICE] Stop command sent to service '%ls'.\n", LEO4_SERVICE_NAME);
    } else {
        fprintf(stderr, "[SERVICE] ControlService(STOP) failed: %lu\n", GetLastError());
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
    return true;
}

bool service_restart(void) {
    service_stop();
    Sleep(2000);
    return service_start();
}

void service_query_status(void) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) {
        printf("[SERVICE] Could not connect to SCM: error %lu\n", GetLastError());
        return;
    }

    SC_HANDLE hService = OpenServiceW(hSCM, LEO4_SERVICE_NAME, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (!hService) {
        printf("[SERVICE] Service '%ls' is NOT installed.\n", LEO4_SERVICE_NAME);
        CloseServiceHandle(hSCM);
        return;
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded;
    if (QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        const char* stateStr = "UNKNOWN";
        switch (ssp.dwCurrentState) {
            case SERVICE_STOPPED:          stateStr = "STOPPED"; break;
            case SERVICE_START_PENDING:    stateStr = "START_PENDING"; break;
            case SERVICE_STOP_PENDING:     stateStr = "STOP_PENDING"; break;
            case SERVICE_RUNNING:          stateStr = "RUNNING"; break;
            case SERVICE_CONTINUE_PENDING: stateStr = "CONTINUE_PENDING"; break;
            case SERVICE_PAUSE_PENDING:    stateStr = "PAUSE_PENDING"; break;
            case SERVICE_PAUSED:           stateStr = "PAUSED"; break;
        }
        printf("[SERVICE] Service '%ls' Status: %s (PID: %lu)\n", LEO4_SERVICE_NAME, stateStr, ssp.dwProcessId);
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
}

bool service_run_dispatcher(const ProxyConfig* config) {
    if (config) {
        memcpy(&g_serviceConfig, config, sizeof(ProxyConfig));
    }

    SERVICE_TABLE_ENTRYW serviceTable[] = {
        { (LPWSTR)LEO4_SERVICE_NAME, service_main },
        { NULL, NULL }
    };

    return StartServiceCtrlDispatcherW(serviceTable) != 0;
}
