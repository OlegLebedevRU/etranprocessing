/**
 * @file mqtt_proxy.c
 * @brief Plain TCP <-> SChannel TLS MQTT Proxy for Leo4Proxy.
 */

#include "mqtt_proxy.h"
#include <ws2tcpip.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    SOCKET clientSock;
    const ProxyConfig* config;
    CredHandle hCred;
} MqttClientWorkerArgs;

static int send_all_socket(SOCKET s, const BYTE* data, int len) {
    int total = 0;
    while (total < len) {
        int sent = send(s, (const char*)(data + total), len - total, 0);
        if (sent <= 0) return sent;
        total += sent;
    }
    return total;
}

static unsigned __stdcall mqtt_client_worker(void* param) {
    MqttClientWorkerArgs* args = (MqttClientWorkerArgs*)param;
    SOCKET clientSock = args->clientSock;
    const ProxyConfig* config = args->config;
    CredHandle hCred = args->hCred;
    free(args);

    if (config->verbose) {
        printf("[MQTT-PROXY] Accepted local client connection. Connecting to %s:%d via TLS...\n",
               config->mqtt_remote_host, config->mqtt_remote_port);
    }

    SChannelSession tlsSession;
    if (!schannel_connect(&tlsSession, &hCred, config->mqtt_remote_host, config->mqtt_remote_port, 10000, config->insecure_server_cert)) {
        fprintf(stderr, "[MQTT-PROXY] Failed to establish mTLS connection to %s:%d\n",
                config->mqtt_remote_host, config->mqtt_remote_port);
        closesocket(clientSock);
        return 1;
    }

    if (config->verbose) {
        printf("[MQTT-PROXY] mTLS established with %s:%d. Proxying data...\n",
               config->mqtt_remote_host, config->mqtt_remote_port);
    }

    BYTE buf[16384];
    bool running = true;

    while (running) {
        // 1. If we have leftover decrypted plaintext, deliver it to local client immediately
        if (tlsSession.plainBufLen > tlsSession.plainBufOffset) {
            int recvd = schannel_recv(&tlsSession, buf, sizeof(buf));
            if (recvd > 0) {
                int s = send_all_socket(clientSock, buf, recvd);
                if (s <= 0) {
                    running = false;
                    break;
                }
            }
            continue;
        }

        // 2. Select on both sockets with 50ms interval
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(clientSock, &read_fds);
        FD_SET(tlsSession.sock, &read_fds);

        SOCKET maxSock = (clientSock > tlsSession.sock) ? clientSock : tlsSession.sock;
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000; // 50ms

        int sel = select((int)maxSock + 1, &read_fds, NULL, NULL, &tv);
        if (sel < 0) {
            int err = WSAGetLastError();
            if (err != WSAEINTR) break;
            continue;
        }

        // Local client -> Broker (encrypt)
        if (FD_ISSET(clientSock, &read_fds)) {
            int recvd = recv(clientSock, (char*)buf, sizeof(buf), 0);
            if (recvd <= 0) {
                if (config->verbose) {
                    printf("[MQTT-PROXY] Local client disconnected.\n");
                }
                running = false;
                break;
            }

            if (config->verbose) {
                printf("[MQTT-PROXY] Client -> Broker: %d bytes | HEX:", recvd);
                for (int i = 0; i < recvd && i < 16; i++) printf(" %02X", buf[i]);
                printf("\n");
            }

            int sent = schannel_send(&tlsSession, buf, recvd);
            if (sent <= 0) {
                fprintf(stderr, "[MQTT-PROXY] Failed to send encrypted data to broker.\n");
                running = false;
                break;
            }
        }

        // Broker -> Local client (decrypt)
        if (FD_ISSET(tlsSession.sock, &read_fds) || tlsSession.recvBufLen > 0) {
            int recvd = schannel_recv(&tlsSession, buf, sizeof(buf));
            if (recvd < 0) {
                if (config->verbose) {
                    printf("[MQTT-PROXY] SChannel recv error from broker.\n");
                }
                running = false;
                break;
            } else if (recvd == 0) {
                if (!tlsSession.isConnected) {
                    if (config->verbose) {
                        printf("[MQTT-PROXY] Remote broker closed connection.\n");
                    }
                    running = false;
                    break;
                }
            } else {
                if (config->verbose) {
                    printf("[MQTT-PROXY] Broker -> Client: %d bytes | HEX:", recvd);
                    for (int i = 0; i < recvd && i < 16; i++) printf(" %02X", buf[i]);
                    printf("\n");
                }

                int s = send_all_socket(clientSock, buf, recvd);
                if (s <= 0) {
                    running = false;
                    break;
                }
            }
        }
    }

    schannel_close(&tlsSession);
    closesocket(clientSock);

    if (config->verbose) {
        printf("[MQTT-PROXY] Connection closed.\n");
    }

    return 0;
}

static unsigned __stdcall mqtt_listener_thread(void* param) {
    MqttProxyServer* server = (MqttProxyServer*)param;
    const ProxyConfig* config = server->config;

    while (server->isRunning) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server->listenSock, &read_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000; // 500ms

        int sel = select((int)server->listenSock + 1, &read_fds, NULL, NULL, &tv);
        if (sel > 0 && FD_ISSET(server->listenSock, &read_fds)) {
            struct sockaddr_in clientAddr;
            int clientAddrLen = sizeof(clientAddr);
            SOCKET clientSock = accept(server->listenSock, (struct sockaddr*)&clientAddr, &clientAddrLen);

            if (clientSock != INVALID_SOCKET) {
                // Reject non-loopback connections if binding to 127.0.0.1
                char clientIp[64] = { 0 };
                inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));

                if (strcmp(config->mqtt_local_host, "127.0.0.1") == 0 &&
                    strcmp(clientIp, "127.0.0.1") != 0 &&
                    strcmp(clientIp, "::1") != 0) {
                    fprintf(stderr, "[MQTT-PROXY] Rejected connection from non-loopback IP: %s\n", clientIp);
                    closesocket(clientSock);
                    continue;
                }

                MqttClientWorkerArgs* args = (MqttClientWorkerArgs*)malloc(sizeof(MqttClientWorkerArgs));
                if (args) {
                    args->clientSock = clientSock;
                    args->config = config;
                    args->hCred = server->hCred;

                    HANDLE hWorker = (HANDLE)_beginthreadex(NULL, 0, mqtt_client_worker, args, 0, NULL);
                    if (hWorker) {
                        CloseHandle(hWorker);
                    } else {
                        closesocket(clientSock);
                        free(args);
                    }
                } else {
                    closesocket(clientSock);
                }
            }
        }
    }

    return 0;
}

bool mqtt_proxy_start(MqttProxyServer* server, const ProxyConfig* config, const CertDetails* certDetails, CredHandle hCred) {
    if (!server || !config || !certDetails) return false;
    memset(server, 0, sizeof(MqttProxyServer));

    server->config = config;
    server->certDetails = certDetails;
    server->hCred = hCred;
    server->listenSock = INVALID_SOCKET;
    server->isRunning = true;

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)config->mqtt_local_port);
    inet_pton(AF_INET, config->mqtt_local_host, &addr.sin_addr);

    server->listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->listenSock == INVALID_SOCKET) {
        fprintf(stderr, "[MQTT-PROXY] Failed to create listening socket: %d\n", WSAGetLastError());
        server->isRunning = false;
        return false;
    }

    BOOL opt = TRUE;
    setsockopt(server->listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    if (bind(server->listenSock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        int err = WSAGetLastError();
        fprintf(stderr, "[MQTT-PROXY] Failed to bind to %s:%d (error: %d)\n",
                config->mqtt_local_host, config->mqtt_local_port, err);
        if (err == 10048) {
            fprintf(stderr, "[MQTT-PROXY] Port %d is already in use! If Leo4Proxy is already running as a Windows Service, stop it first.\n",
                    config->mqtt_local_port);
        }
        closesocket(server->listenSock);
        server->listenSock = INVALID_SOCKET;
        server->isRunning = false;
        return false;
    }

    if (listen(server->listenSock, SOMAXCONN) != 0) {
        fprintf(stderr, "[MQTT-PROXY] Failed to listen on %s:%d: %d\n",
                config->mqtt_local_host, config->mqtt_local_port, WSAGetLastError());
        closesocket(server->listenSock);
        server->listenSock = INVALID_SOCKET;
        server->isRunning = false;
        return false;
    }

    printf("[MQTT-PROXY] Listening on %s:%d -> %s:%d (mTLS)\n",
           config->mqtt_local_host, config->mqtt_local_port,
           config->mqtt_remote_host, config->mqtt_remote_port);

    server->hThread = (HANDLE)_beginthreadex(NULL, 0, mqtt_listener_thread, server, 0, NULL);
    if (!server->hThread) {
        fprintf(stderr, "[MQTT-PROXY] Failed to start listener thread: %lu\n", GetLastError());
        closesocket(server->listenSock);
        server->listenSock = INVALID_SOCKET;
        server->isRunning = false;
        return false;
    }

    return true;
}

void mqtt_proxy_stop(MqttProxyServer* server) {
    if (!server) return;
    server->isRunning = false;

    if (server->listenSock != INVALID_SOCKET) {
        closesocket(server->listenSock);
        server->listenSock = INVALID_SOCKET;
    }

    if (server->hThread) {
        WaitForSingleObject(server->hThread, 2000);
        CloseHandle(server->hThread);
        server->hThread = NULL;
    }
}
