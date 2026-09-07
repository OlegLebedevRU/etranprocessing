/**
 * @file stream_proxy.c
 * @brief Plain TCP <-> SChannel TLS Stream Forwarder (RTP/RTSP/MPEG-TS over mTLS) for Leo4Proxy.
 */

#include "stream_proxy.h"
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    SOCKET clientSock;
    struct sockaddr_in clientAddr;
    const ProxyConfig* config;
    StreamProxyServer* server;
    CredHandle hClientCred;
} StreamClientWorkerArgs;

static int send_all_socket(SOCKET s, const BYTE* data, int len) {
    int total = 0;
    while (total < len) {
        int sent = send(s, (const char*)(data + total), len - total, 0);
        if (sent <= 0) return sent;
        total += sent;
    }
    return total;
}

static unsigned __stdcall stream_client_worker(void* param) {
    StreamClientWorkerArgs* args = (StreamClientWorkerArgs*)param;
    SOCKET clientSock = args->clientSock;
    struct sockaddr_in clientAddr = args->clientAddr;
    const ProxyConfig* config = args->config;
    StreamProxyServer* server = args->server;
    CredHandle hClientCred = args->hClientCred;
    free(args);

    InterlockedIncrement(&g_proxyStats.stream_active_clients);
    InterlockedIncrement(&g_proxyStats.stream_total_connections);

    char clientIp[64] = { 0 };
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));

    /* Socket options on local client socket */
    BOOL keepAlive = TRUE;
    setsockopt(clientSock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&keepAlive, sizeof(keepAlive));
    BOOL tcpNoDelay = TRUE;
    setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, (const char*)&tcpNoDelay, sizeof(tcpNoDelay));

    if (config->verbose) {
        printf("[STREAM-PROXY] Accepted local client connection from %s:%d. Connecting to %s:%d via TLS...\n",
               clientIp, ntohs(clientAddr.sin_port), config->stream_remote_host, config->stream_remote_port);
    }

    /* Connect to cloud media ingress over mTLS */
    SChannelSession remoteTlsSession;
    if (!schannel_connect(&remoteTlsSession, &hClientCred, config->stream_remote_host, config->stream_remote_port, 10000, config->insecure_server_cert)) {
        fprintf(stderr, "[STREAM-PROXY] Failed to establish mTLS connection to %s:%d\n",
                config->stream_remote_host, config->stream_remote_port);
        closesocket(clientSock);
        InterlockedDecrement(&g_proxyStats.stream_active_clients);
        return 1;
    }

    /* Socket options on remote TLS socket */
    setsockopt(remoteTlsSession.sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&tcpNoDelay, sizeof(tcpNoDelay));
    int sndBuf = 262144; /* 256 KB send buffer for LTE jitter */
    setsockopt(remoteTlsSession.sock, SOL_SOCKET, SO_SNDBUF, (const char*)&sndBuf, sizeof(sndBuf));

    /* Aggressive TCP keepalive for cellular/LTE NAT timeouts */
    struct tcp_keepalive alive_vals;
    alive_vals.onoff = 1;
    alive_vals.keepalivetime = 15000;     /* 15 seconds */
    alive_vals.keepaliveinterval = 5000;  /* 5 seconds */
    DWORD dwBytesReturned = 0;
    if (WSAIoctl(remoteTlsSession.sock, SIO_KEEPALIVE_VALS, &alive_vals, sizeof(alive_vals),
                 NULL, 0, &dwBytesReturned, NULL, NULL) != 0) {
        if (config->verbose) {
            fprintf(stderr, "[STREAM-PROXY] Warning: WSAIoctl(SIO_KEEPALIVE_VALS) failed: %d\n", WSAGetLastError());
        }
    }

    if (config->verbose) {
        printf("[STREAM-PROXY] mTLS established with %s:%d. Forwarding stream...\n",
               config->stream_remote_host, config->stream_remote_port);
    }

    /* Allocate 64 KB buffer on heap */
    BYTE* buf = (BYTE*)malloc(PROXY_BUFFER_SIZE);
    if (!buf) {
        fprintf(stderr, "[STREAM-PROXY] Failed to allocate stream buffer (%d bytes)\n", PROXY_BUFFER_SIZE);
        schannel_close(&remoteTlsSession);
        closesocket(clientSock);
        InterlockedDecrement(&g_proxyStats.stream_active_clients);
        return 1;
    }

    LONGLONG connBytesUp = 0;
    LONGLONG connBytesDown = 0;
    ULONGLONG lastActivityTime = GetTickCount64();
    bool running = true;

    while (running && (!server || server->isRunning)) {
        /* Idle timeout check */
        if (config->stream_idle_timeout_sec > 0) {
            ULONGLONG now = GetTickCount64();
            if ((now - lastActivityTime) >= ((ULONGLONG)config->stream_idle_timeout_sec * 1000ULL)) {
                printf("[STREAM-PROXY] Idle timeout (%d s). Closing connection.\n", config->stream_idle_timeout_sec);
                running = false;
                break;
            }
        }

        /* 1. If remote SChannel session has leftover decrypted plaintext, deliver to local client */
        if (remoteTlsSession.plainBufLen > remoteTlsSession.plainBufOffset) {
            int recvd = schannel_recv(&remoteTlsSession, buf, PROXY_BUFFER_SIZE);
            if (recvd > 0) {
                int s = send_all_socket(clientSock, buf, recvd);
                if (s <= 0) {
                    running = false;
                    break;
                }
                InterlockedExchangeAdd64((LONG64*)&g_proxyStats.stream_bytes_down, recvd);
                connBytesDown += recvd;
                lastActivityTime = GetTickCount64();
            } else if (recvd < 0) {
                running = false;
                break;
            }
            continue;
        }

        /* 2. Select on both sockets with 50ms interval */
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(clientSock, &read_fds);
        FD_SET(remoteTlsSession.sock, &read_fds);

        SOCKET maxSock = (clientSock > remoteTlsSession.sock) ? clientSock : remoteTlsSession.sock;
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000; /* 50ms */

        int sel = select((int)maxSock + 1, &read_fds, NULL, NULL, &tv);
        if (sel < 0) {
            int err = WSAGetLastError();
            if (err != WSAEINTR) break;
            continue;
        }

        /* Local client -> Remote (stream upload) */
        if (FD_ISSET(clientSock, &read_fds)) {
            int recvd = recv(clientSock, (char*)buf, PROXY_BUFFER_SIZE, 0);
            if (recvd <= 0) {
                if (config->verbose) {
                    printf("[STREAM-PROXY] Local client disconnected.\n");
                }
                running = false;
                break;
            } else {
                int sent = schannel_send(&remoteTlsSession, buf, recvd);
                if (sent <= 0) {
                    if (config->verbose) {
                        fprintf(stderr, "[STREAM-PROXY] Failed to send encrypted data to remote.\n");
                    }
                    running = false;
                    break;
                }
                InterlockedExchangeAdd64((LONG64*)&g_proxyStats.stream_bytes_up, sent);
                connBytesUp += sent;
                lastActivityTime = GetTickCount64();
            }
        }

        /* Remote -> Local client (stream download / feedback / RTSP responses / RTCP) */
        if (FD_ISSET(remoteTlsSession.sock, &read_fds) || remoteTlsSession.recvBufLen > 0) {
            int recvd = schannel_recv(&remoteTlsSession, buf, PROXY_BUFFER_SIZE);
            if (recvd < 0) {
                if (config->verbose) {
                    printf("[STREAM-PROXY] SChannel recv error from remote.\n");
                }
                running = false;
                break;
            } else if (recvd == 0) {
                if (!remoteTlsSession.isConnected) {
                    if (config->verbose) {
                        printf("[STREAM-PROXY] Remote closed connection.\n");
                    }
                    running = false;
                    break;
                }
            } else {
                int s = send_all_socket(clientSock, buf, recvd);
                if (s <= 0) {
                    running = false;
                    break;
                }
                InterlockedExchangeAdd64((LONG64*)&g_proxyStats.stream_bytes_down, recvd);
                connBytesDown += recvd;
                lastActivityTime = GetTickCount64();
            }
        }
    }

    schannel_close(&remoteTlsSession);
    closesocket(clientSock);
    free(buf);

    InterlockedDecrement(&g_proxyStats.stream_active_clients);

    if (config->verbose) {
        printf("[STREAM-PROXY] Stream connection closed. Bytes up: %lld, Bytes down: %lld\n",
               connBytesUp, connBytesDown);
    }

    return 0;
}

static unsigned __stdcall stream_listener_thread(void* param) {
    StreamProxyServer* server = (StreamProxyServer*)param;
    const ProxyConfig* config = server->config;

    while (server->isRunning) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server->listenSock, &read_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000; /* 500ms */

        int sel = select((int)server->listenSock + 1, &read_fds, NULL, NULL, &tv);
        if (sel > 0 && FD_ISSET(server->listenSock, &read_fds)) {
            struct sockaddr_in clientAddr;
            int clientAddrLen = sizeof(clientAddr);
            SOCKET clientSock = accept(server->listenSock, (struct sockaddr*)&clientAddr, &clientAddrLen);

            if (clientSock != INVALID_SOCKET) {
                /* Reject non-loopback connections if binding to 127.0.0.1 */
                char clientIp[64] = { 0 };
                inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));

                if (strcmp(config->stream_local_host, "127.0.0.1") == 0 &&
                    strcmp(clientIp, "127.0.0.1") != 0 &&
                    strcmp(clientIp, "::1") != 0) {
                    fprintf(stderr, "[STREAM-PROXY] Rejected connection from non-loopback IP: %s\n", clientIp);
                    closesocket(clientSock);
                    continue;
                }

                /* Check max clients limit */
                if (config->stream_max_clients > 0 &&
                    g_proxyStats.stream_active_clients >= config->stream_max_clients) {
                    fprintf(stderr, "[STREAM-PROXY] Rejected: max clients (%d) reached\n", config->stream_max_clients);
                    closesocket(clientSock);
                    continue;
                }

                StreamClientWorkerArgs* args = (StreamClientWorkerArgs*)malloc(sizeof(StreamClientWorkerArgs));
                if (args) {
                    args->clientSock = clientSock;
                    args->clientAddr = clientAddr;
                    args->config = config;
                    args->server = server;
                    args->hClientCred = server->hClientCred;

                    HANDLE hWorker = (HANDLE)_beginthreadex(NULL, 0, stream_client_worker, args, 0, NULL);
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

bool stream_proxy_start(StreamProxyServer* server, const ProxyConfig* config,
                        const CertDetails* certDetails, CredHandle hClientCred) {
    if (!server || !config || !certDetails) return false;
    memset(server, 0, sizeof(StreamProxyServer));

    server->config = config;
    server->certDetails = certDetails;
    server->hClientCred = hClientCred;
    server->listenSock = INVALID_SOCKET;
    server->isRunning = true;

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)config->stream_local_port);
    inet_pton(AF_INET, config->stream_local_host, &addr.sin_addr);

    server->listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->listenSock == INVALID_SOCKET) {
        fprintf(stderr, "[STREAM-PROXY] Failed to create listening socket: %d\n", WSAGetLastError());
        server->isRunning = false;
        return false;
    }

    BOOL opt = TRUE;
    setsockopt(server->listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    if (bind(server->listenSock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        int err = WSAGetLastError();
        fprintf(stderr, "[STREAM-PROXY] Failed to bind to %s:%d (error: %d)\n",
                config->stream_local_host, config->stream_local_port, err);
        if (err == 10048) {
            fprintf(stderr, "[STREAM-PROXY] Port %d is already in use! If Leo4Proxy is already running as a Windows Service, stop it first.\n",
                    config->stream_local_port);
        }
        closesocket(server->listenSock);
        server->listenSock = INVALID_SOCKET;
        server->isRunning = false;
        return false;
    }

    if (listen(server->listenSock, SOMAXCONN) != 0) {
        fprintf(stderr, "[STREAM-PROXY] Failed to listen on %s:%d: %d\n",
                config->stream_local_host, config->stream_local_port, WSAGetLastError());
        closesocket(server->listenSock);
        server->listenSock = INVALID_SOCKET;
        server->isRunning = false;
        return false;
    }

    printf("[STREAM-PROXY] Listening on %s:%d -> %s:%d (mTLS)\n",
           config->stream_local_host, config->stream_local_port,
           config->stream_remote_host, config->stream_remote_port);

    server->hThread = (HANDLE)_beginthreadex(NULL, 0, stream_listener_thread, server, 0, NULL);
    if (!server->hThread) {
        fprintf(stderr, "[STREAM-PROXY] Failed to start listener thread: %lu\n", GetLastError());
        closesocket(server->listenSock);
        server->listenSock = INVALID_SOCKET;
        server->isRunning = false;
        return false;
    }

    return true;
}

void stream_proxy_stop(StreamProxyServer* server) {
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
