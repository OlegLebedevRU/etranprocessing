#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <shlwapi.h>

#include "config.h"
#include "state_mgr.h"
#include "proxy_client.h"
#include "mosquitto_conf.h"
#include "orchestrator.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")

static volatile bool g_stub_cert_ready = false;
static volatile bool g_stub_stop = false;
static SOCKET g_server_sock = INVALID_SOCKET;

static DWORD WINAPI http_stub_thread(LPVOID lpParam) {
    (void)lpParam;
    while (!g_stub_stop) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(g_server_sock, &read_fds);
        struct timeval tv = { 0, 100000 }; // 100ms
        int sel = select(0, &read_fds, NULL, NULL, &tv);
        if (sel <= 0 || !FD_ISSET(g_server_sock, &read_fds)) {
            continue;
        }

        SOCKET client = accept(g_server_sock, NULL, NULL);
        if (client == INVALID_SOCKET) continue;

        char req_buf[1024] = { 0 };
        recv(client, req_buf, sizeof(req_buf) - 1, 0);

        char body[1024];
        if (g_stub_cert_ready) {
            snprintf(body, sizeof(body),
                     "{\n"
                     "  \"status\": \"ready\",\n"
                     "  \"certificate_found\": true,\n"
                     "  \"sn\": \"a4b0000773c82116d210826\",\n"
                     "  \"thumbprint\": \"CC88419A4C3763150A4C0905261EC073C58CFC09\",\n"
                     "  \"not_after\": \"2027-08-29 17:09:40 UTC\",\n"
                     "  \"listeners\": { \"http_local\": \"127.0.0.1:18499\" }\n"
                     "}\n");
        } else {
            snprintf(body, sizeof(body),
                     "{\n"
                     "  \"status\": \"waiting_for_certificate\",\n"
                     "  \"certificate_found\": false,\n"
                     "  \"sn\": \"\",\n"
                     "  \"thumbprint\": \"\",\n"
                     "  \"not_after\": \"\",\n"
                     "  \"listeners\": { \"http_local\": \"127.0.0.1:18499\" }\n"
                     "}\n");
        }

        char resp[2048];
        snprintf(resp, sizeof(resp),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: application/json\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n\r\n%s",
                 strlen(body), body);

        send(client, resp, (int)strlen(resp), 0);
        closesocket(client);
    }
    return 0;
}

int main(void) {
    printf("=======================================================\n");
    printf(" Running l4superv Component Test (Leo4Proxy Stub)\n");
    printf("=======================================================\n");

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    g_server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(g_server_sock != INVALID_SOCKET);

    int opt = 1;
    setsockopt(g_server_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(18499);
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");

    int b_res = bind(g_server_sock, (struct sockaddr*)&sin, sizeof(sin));
    assert(b_res == 0);

    listen(g_server_sock, 5);

    HANDLE hThread = CreateThread(NULL, 0, http_stub_thread, NULL, 0, NULL);
    assert(hThread != NULL);

    // Setup temporary base directory
    wchar_t temp_dir[MAX_PATH];
    GetTempPathW(MAX_PATH, temp_dir);
    wcscat_s(temp_dir, MAX_PATH, L"l4superv_comp_test");
    CreateDirectoryW(temp_dir, NULL);

    wchar_t mosq_dir[MAX_PATH];
    swprintf_s(mosq_dir, MAX_PATH, L"%ls\\mosquitto", temp_dir);
    CreateDirectoryW(mosq_dir, NULL);

    L4SupervConfig cfg;
    config_init_defaults(&cfg, NULL);
    wcscpy_s(cfg.base_path, MAX_PATH, temp_dir);
    wcscpy_s(cfg.proxy_url, 256, L"http://127.0.0.1:18499/_leo4/info");
    cfg.standby_poll_sec = 1;
    cfg.watchdog_interval_sec = 2;
    cfg.watchdog_enabled = false;
    cfg.auto_reset_on_clone = false;
    cfg.auto_start_l4desk = false;

    L4State state;
    state_init(&state);

    // Phase 1: Standby test (stub returns certificate_found: false)
    g_stub_cert_ready = false;
    printf("[TEST] Phase 1: Querying stub in Standby (no cert)...\n");

    bool action = false;
    bool step_ok = orchestrator_step(&cfg, &state, &action);
    assert(step_ok);
    assert(strcmp(state.status, "standby") == 0);
    assert(mosquitto_conf_is_standby(temp_dir));
    printf("[PASS] Phase 1 succeeded: state is 'standby', mosquitto.conf is standby.\n");

    // Phase 2: Activation transition test (stub switches to certificate_found: true)
    printf("[TEST] Phase 2: Switching stub to 'ready' (cert found). Measuring activation time...\n");
    g_stub_cert_ready = true;

    ULONGLONG t_start = GetTickCount64();
    step_ok = orchestrator_step(&cfg, &state, &action);
    ULONGLONG t_elapsed = GetTickCount64() - t_start;

    assert(step_ok);
    assert(strcmp(state.status, "active") == 0);
    assert(strcmp(state.sn, "a4b0000773c82116d210826") == 0);
    assert(strcmp(state.thumbprint, "CC88419A4C3763150A4C0905261EC073C58CFC09") == 0);
    assert(mosquitto_conf_is_active_with_sn(temp_dir, "a4b0000773c82116d210826"));

    printf("[RESULT] Activation completed in: %llu ms\n", t_elapsed);
    assert(t_elapsed <= 15000); // Contract requirement: <= 15 seconds

    printf("[PASS] Phase 2 succeeded: state is 'active' with SN and bridge config in %llu ms (<= 15s)!\n", t_elapsed);

    // Stop stub thread
    g_stub_stop = true;
    WaitForSingleObject(hThread, 2000);
    CloseHandle(hThread);
    closesocket(g_server_sock);
    WSACleanup();

    // Clean up test dir
    wchar_t f_path[MAX_PATH];
    swprintf_s(f_path, MAX_PATH, L"%ls\\mosquitto\\mosquitto.conf", temp_dir);
    DeleteFileW(f_path);
    RemoveDirectoryW(mosq_dir);
    swprintf_s(f_path, MAX_PATH, L"%ls\\state.json", temp_dir);
    DeleteFileW(f_path);
    RemoveDirectoryW(temp_dir);

    state_cleanup(&state);

    printf("\n=======================================================\n");
    printf(" ALL COMPONENT TESTS PASSED SUCCESSFULLY!\n");
    printf("=======================================================\n");
    return 0;
}
