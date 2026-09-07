/**
 * @file rtp_tunnel.c
 * @brief Primary video media tunnel: local RTP/RTCP UDP -> framed mTLS/TCP (L4RTP/1) for Leo4Proxy.
 */

#include "rtp_tunnel.h"
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define L4RTP_FRAME_RTP       0x01
#define L4RTP_FRAME_RTCP      0x02
#define L4RTP_FRAME_KEEPALIVE 0x03

typedef enum {
    STATE_IDLE,
    STATE_CONNECTING,
    STATE_STREAMING,
    STATE_BACKOFF
} TunnelState;

static SOCKET create_bound_udp_socket(const char* host, int port) {
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo* res = NULL;
    int rc = getaddrinfo(host, portStr, &hints, &res);
    if (rc != 0 || !res) {
        fprintf(stderr, "[RTP-TUNNEL] getaddrinfo failed for %s:%d: %d\n", host, port, rc);
        return INVALID_SOCKET;
    }

    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) {
        fprintf(stderr, "[RTP-TUNNEL] socket() failed for port %d: %d\n", port, WSAGetLastError());
        freeaddrinfo(res);
        return INVALID_SOCKET;
    }

    /* Note: SO_REUSEADDR is intentionally NOT used to prevent stream hijacking by other local processes */

    /* SO_RCVBUF = 512 KB to accommodate bursts of H.264 IDR frames */
    int rcvbuf = 512 * 1024;
    if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf)) != 0) {
        fprintf(stderr, "[RTP-TUNNEL] Warning: failed to set SO_RCVBUF on port %d: %d\n", port, WSAGetLastError());
    }

    if (bind(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
        int err = WSAGetLastError();
        if (err == WSAEADDRINUSE) {
            fprintf(stderr, "[RTP-TUNNEL] Port %d is already in use (WSAEADDRINUSE 10048) on %s:%d\n", port, host, port);
        } else {
            fprintf(stderr, "[RTP-TUNNEL] bind() failed on %s:%d: %d\n", host, port, err);
        }
        closesocket(s);
        freeaddrinfo(res);
        return INVALID_SOCKET;
    }

    freeaddrinfo(res);

    /* Set non-blocking mode */
    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);

    return s;
}

static unsigned __stdcall rtp_tunnel_worker_thread(void* param) {
    RtpTunnelServer* server = (RtpTunnelServer*)param;
    const ProxyConfig* config = server->config;

    /* Allocate buffer for frame header (4 bytes) + max UDP payload (65535 bytes) */
    unsigned char* buf = (unsigned char*)malloc(4 + 65535);
    if (!buf) {
        fprintf(stderr, "[RTP-TUNNEL] Failed to allocate UDP reception buffer\n");
        return 1;
    }

    TunnelState state = STATE_IDLE;
    SChannelSession remote;
    memset(&remote, 0, sizeof(remote));

    int base_reconnect_sec = config->rtp_tunnel_reconnect_sec > 0 ?
                             config->rtp_tunnel_reconnect_sec : DEFAULT_RTP_TUNNEL_RECONNECT_SEC;
    int current_backoff_sec = base_reconnect_sec;

    ULONGLONG lastActivityTime = 0;
    int pendingLen = 0;
    bool pendingIsRtcp = false;

    while (server->isRunning) {
        switch (state) {
            case STATE_IDLE: {
                InterlockedExchange(&g_proxyStats.rtp_tunnel_active, 0);

                fd_set readSet;
                FD_ZERO(&readSet);
                FD_SET(server->rtpSocket, &readSet);
                FD_SET(server->rtcpSocket, &readSet);

                struct timeval tv = { 0, 200000 }; /* 200 ms <= 250 ms */
                int sel = select(0, &readSet, NULL, NULL, &tv);
                if (!server->isRunning) break;
                if (sel <= 0) continue;

                if (FD_ISSET(server->rtpSocket, &readSet)) {
                    int n = recvfrom(server->rtpSocket, (char*)buf + 4, 65535, 0, NULL, NULL);
                    if (n > 0) {
                        pendingLen = n;
                        pendingIsRtcp = false;
                        state = STATE_CONNECTING;
                    }
                } else if (FD_ISSET(server->rtcpSocket, &readSet)) {
                    int n = recvfrom(server->rtcpSocket, (char*)buf + 4, 65535, 0, NULL, NULL);
                    if (n > 0) {
                        pendingLen = n;
                        pendingIsRtcp = true;
                        state = STATE_CONNECTING;
                    }
                }
                break;
            }

            case STATE_CONNECTING: {
                if (config->verbose) {
                    printf("[RTP-TUNNEL] First datagram received (%d bytes, %s). Connecting mTLS to %s:%d...\n",
                           pendingLen, pendingIsRtcp ? "RTCP" : "RTP",
                           config->rtp_tunnel_remote_host, config->rtp_tunnel_remote_port);
                }

                bool ok = schannel_connect(&remote, (CredHandle*)&server->hClientCred,
                                          config->rtp_tunnel_remote_host,
                                          config->rtp_tunnel_remote_port,
                                          DEFAULT_RTP_TUNNEL_CONNECT_TIMEOUT,
                                          config->insecure_server_cert);
                if (!ok) {
                    fprintf(stderr, "[RTP-TUNNEL] Failed to establish mTLS connection to %s:%d\n",
                            config->rtp_tunnel_remote_host, config->rtp_tunnel_remote_port);
                    InterlockedIncrement64(&g_proxyStats.rtp_tunnel_dropped_no_upstream);
                    pendingLen = 0;
                    state = STATE_BACKOFF;
                    break;
                }

                /* Socket options on remote TLS socket */
                BOOL tcpNoDelay = TRUE;
                setsockopt(remote.sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&tcpNoDelay, sizeof(tcpNoDelay));

                BOOL keepAlive = TRUE;
                setsockopt(remote.sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&keepAlive, sizeof(keepAlive));

                struct tcp_keepalive alive_vals;
                alive_vals.onoff = 1;
                alive_vals.keepalivetime = 15000;     /* 15 sec */
                alive_vals.keepaliveinterval = 5000;  /* 5 sec */
                DWORD dwBytesReturned = 0;
                WSAIoctl(remote.sock, SIO_KEEPALIVE_VALS, &alive_vals, sizeof(alive_vals),
                         NULL, 0, &dwBytesReturned, NULL, NULL);

                int sndBuf = 262144; /* 256 KB send buffer */
                setsockopt(remote.sock, SOL_SOCKET, SO_SNDBUF, (const char*)&sndBuf, sizeof(sndBuf));

                /* Send L4RTP/1 Preamble */
                size_t snLen = strlen(server->certDetails->sn);
                BYTE preamble[8 + 128];
                preamble[0] = 'L';
                preamble[1] = '4';
                preamble[2] = 'R';
                preamble[3] = 'T';
                preamble[4] = 0x01;
                preamble[5] = 0x00;
                preamble[6] = (BYTE)((snLen >> 8) & 0xFF);
                preamble[7] = (BYTE)(snLen & 0xFF);
                memcpy(preamble + 8, server->certDetails->sn, snLen);

                int preSent = schannel_send(&remote, preamble, (int)(8 + snLen));
                if (preSent <= 0) {
                    fprintf(stderr, "[RTP-TUNNEL] Failed to send L4RTP/1 preamble to %s:%d\n",
                            config->rtp_tunnel_remote_host, config->rtp_tunnel_remote_port);
                    schannel_close(&remote);
                    InterlockedIncrement64(&g_proxyStats.rtp_tunnel_dropped_no_upstream);
                    pendingLen = 0;
                    state = STATE_BACKOFF;
                    break;
                }

                /* Reset backoff on successful handshake & preamble */
                current_backoff_sec = base_reconnect_sec;
                InterlockedIncrement(&g_proxyStats.rtp_tunnel_total_connections);
                InterlockedExchange(&g_proxyStats.rtp_tunnel_active, 1);
                InterlockedAdd64(&g_proxyStats.rtp_tunnel_bytes_up, preSent);

                printf("[RTP-TUNNEL] Connected to %s:%d (L4RTP/1 mTLS, SN: %s)\n",
                       config->rtp_tunnel_remote_host, config->rtp_tunnel_remote_port, server->certDetails->sn);

                /* Send the datagram that triggered the connection */
                if (pendingLen > 0) {
                    buf[0] = pendingIsRtcp ? L4RTP_FRAME_RTCP : L4RTP_FRAME_RTP;
                    buf[1] = 0x00;
                    buf[2] = (unsigned char)((pendingLen >> 8) & 0xFF);
                    buf[3] = (unsigned char)(pendingLen & 0xFF);

                    int sent = schannel_send(&remote, buf, 4 + pendingLen);
                    pendingLen = 0;
                    if (sent <= 0) {
                        fprintf(stderr, "[RTP-TUNNEL] Failed to send initial datagram over TLS\n");
                        schannel_close(&remote);
                        InterlockedExchange(&g_proxyStats.rtp_tunnel_active, 0);
                        state = STATE_BACKOFF;
                        break;
                    }

                    InterlockedAdd64(&g_proxyStats.rtp_tunnel_bytes_up, sent);
                    if (pendingIsRtcp) {
                        InterlockedIncrement64(&g_proxyStats.rtp_tunnel_packets_rtcp);
                    } else {
                        InterlockedIncrement64(&g_proxyStats.rtp_tunnel_packets_rtp);
                    }
                }

                lastActivityTime = GetTickCount64();
                state = STATE_STREAMING;
                break;
            }

            case STATE_STREAMING: {
                /* Idle timeout check */
                if (config->rtp_tunnel_idle_timeout_sec > 0) {
                    ULONGLONG now = GetTickCount64();
                    if ((now - lastActivityTime) >= ((ULONGLONG)config->rtp_tunnel_idle_timeout_sec * 1000ULL)) {
                        printf("[RTP-TUNNEL] Idle timeout; waiting for local RTP/RTCP\n");
                        schannel_close(&remote);
                        InterlockedExchange(&g_proxyStats.rtp_tunnel_active, 0);
                        state = STATE_IDLE;
                        continue;
                    }
                }

                fd_set readSet;
                FD_ZERO(&readSet);
                FD_SET(server->rtpSocket, &readSet);
                FD_SET(server->rtcpSocket, &readSet);

                struct timeval tv = { 0, 200000 }; /* 200 ms <= 250 ms */
                int sel = select(0, &readSet, NULL, NULL, &tv);
                if (!server->isRunning) break;
                if (sel < 0) {
                    break;
                }
                if (sel == 0) continue;

                /* Process RTP socket */
                if (FD_ISSET(server->rtpSocket, &readSet)) {
                    int drained = 0;
                    while (drained < 32 && server->isRunning) {
                        int n = recvfrom(server->rtpSocket, (char*)buf + 4, 65535, 0, NULL, NULL);
                        if (n <= 0) break;
                        drained++;

                        buf[0] = L4RTP_FRAME_RTP;
                        buf[1] = 0x00;
                        buf[2] = (unsigned char)((n >> 8) & 0xFF);
                        buf[3] = (unsigned char)(n & 0xFF);

                        int sent = schannel_send(&remote, buf, 4 + n);
                        if (sent <= 0) {
                            fprintf(stderr, "[RTP-TUNNEL] Connection closed by upstream or send error: %d\n", WSAGetLastError());
                            schannel_close(&remote);
                            InterlockedExchange(&g_proxyStats.rtp_tunnel_active, 0);
                            state = STATE_BACKOFF;
                            break;
                        }

                        InterlockedAdd64(&g_proxyStats.rtp_tunnel_bytes_up, sent);
                        InterlockedIncrement64(&g_proxyStats.rtp_tunnel_packets_rtp);
                        lastActivityTime = GetTickCount64();
                    }
                    if (state == STATE_BACKOFF) continue;
                }

                /* Process RTCP socket */
                if (FD_ISSET(server->rtcpSocket, &readSet)) {
                    int drained = 0;
                    while (drained < 16 && server->isRunning) {
                        int n = recvfrom(server->rtcpSocket, (char*)buf + 4, 65535, 0, NULL, NULL);
                        if (n <= 0) break;
                        drained++;

                        buf[0] = L4RTP_FRAME_RTCP;
                        buf[1] = 0x00;
                        buf[2] = (unsigned char)((n >> 8) & 0xFF);
                        buf[3] = (unsigned char)(n & 0xFF);

                        int sent = schannel_send(&remote, buf, 4 + n);
                        if (sent <= 0) {
                            fprintf(stderr, "[RTP-TUNNEL] Connection closed by upstream or send error: %d\n", WSAGetLastError());
                            schannel_close(&remote);
                            InterlockedExchange(&g_proxyStats.rtp_tunnel_active, 0);
                            state = STATE_BACKOFF;
                            break;
                        }

                        InterlockedAdd64(&g_proxyStats.rtp_tunnel_bytes_up, sent);
                        InterlockedIncrement64(&g_proxyStats.rtp_tunnel_packets_rtcp);
                        lastActivityTime = GetTickCount64();
                    }
                    if (state == STATE_BACKOFF) continue;
                }
                break;
            }

            case STATE_BACKOFF: {
                if (config->verbose) {
                    printf("[RTP-TUNNEL] Backoff for %d seconds before reconnect...\n", current_backoff_sec);
                }

                ULONGLONG backoffStart = GetTickCount64();
                ULONGLONG backoffMs = (ULONGLONG)current_backoff_sec * 1000ULL;
                bool trafficSeenDuringBackoff = false;

                while (server->isRunning && (GetTickCount64() - backoffStart) < backoffMs) {
                    fd_set dropSet;
                    FD_ZERO(&dropSet);
                    FD_SET(server->rtpSocket, &dropSet);
                    FD_SET(server->rtcpSocket, &dropSet);

                    struct timeval tv = { 0, 50000 }; /* 50 ms interruptible slice */
                    int sel = select(0, &dropSet, NULL, NULL, &tv);
                    if (sel > 0) {
                        if (FD_ISSET(server->rtpSocket, &dropSet)) {
                            while (recvfrom(server->rtpSocket, (char*)buf + 4, 65535, 0, NULL, NULL) > 0) {
                                InterlockedIncrement64(&g_proxyStats.rtp_tunnel_dropped_no_upstream);
                                trafficSeenDuringBackoff = true;
                            }
                        }
                        if (FD_ISSET(server->rtcpSocket, &dropSet)) {
                            while (recvfrom(server->rtcpSocket, (char*)buf + 4, 65535, 0, NULL, NULL) > 0) {
                                InterlockedIncrement64(&g_proxyStats.rtp_tunnel_dropped_no_upstream);
                                trafficSeenDuringBackoff = true;
                            }
                        }
                    }
                }

                if (!server->isRunning) break;

                /* Exponential backoff increase with cap */
                current_backoff_sec *= 2;
                if (current_backoff_sec > DEFAULT_RTP_TUNNEL_RECONNECT_MAX_SEC) {
                    current_backoff_sec = DEFAULT_RTP_TUNNEL_RECONNECT_MAX_SEC;
                }

                if (trafficSeenDuringBackoff) {
                    /* Only attempt immediate reconnect if local datagrams continue to arrive */
                    int n = recvfrom(server->rtpSocket, (char*)buf + 4, 65535, 0, NULL, NULL);
                    if (n > 0) {
                        pendingLen = n;
                        pendingIsRtcp = false;
                        state = STATE_CONNECTING;
                    } else {
                        n = recvfrom(server->rtcpSocket, (char*)buf + 4, 65535, 0, NULL, NULL);
                        if (n > 0) {
                            pendingLen = n;
                            pendingIsRtcp = true;
                            state = STATE_CONNECTING;
                        } else {
                            state = STATE_IDLE;
                        }
                    }
                } else {
                    /* Return to IDLE so we don't spam upstream connects when there is no local video */
                    state = STATE_IDLE;
                }
                break;
            }
        }
    }

    if (remote.isConnected) {
        schannel_close(&remote);
    }
    InterlockedExchange(&g_proxyStats.rtp_tunnel_active, 0);
    free(buf);
    return 0;
}

bool rtp_tunnel_start(RtpTunnelServer* server, const ProxyConfig* config,
                      const CertDetails* certDetails, CredHandle hClientCred) {
    if (!server || !config || !certDetails) return false;
    memset(server, 0, sizeof(RtpTunnelServer));

    /* Strict SN validation: SN must be from certDetails->sn and length 1..127 */
    size_t snLen = strlen(certDetails->sn);
    if (snLen == 0 || snLen > 127) {
        fprintf(stderr, "[RTP-TUNNEL] Error: Invalid certificate SN '%s' (length %zu, must be 1..127). Cannot start RTP tunnel.\n",
                certDetails->sn, snLen);
        return false;
    }

    /* Port validation */
    if (config->rtp_tunnel_rtp_port <= 0 || config->rtp_tunnel_rtp_port > 65535 ||
        config->rtp_tunnel_rtcp_port <= 0 || config->rtp_tunnel_rtcp_port > 65535) {
        fprintf(stderr, "[RTP-TUNNEL] Error: Invalid RTP/RTCP ports (%d, %d)\n",
                config->rtp_tunnel_rtp_port, config->rtp_tunnel_rtcp_port);
        return false;
    }
    if (config->rtp_tunnel_rtp_port == config->rtp_tunnel_rtcp_port) {
        fprintf(stderr, "[RTP-TUNNEL] Error: RTP port (%d) and RTCP port (%d) must differ\n",
                config->rtp_tunnel_rtp_port, config->rtp_tunnel_rtcp_port);
        return false;
    }

    /* Bind local UDP sockets on loopback (SO_RCVBUF = 512 KB, no SO_REUSEADDR) */
    server->rtpSocket = create_bound_udp_socket(config->rtp_tunnel_local_host, config->rtp_tunnel_rtp_port);
    if (server->rtpSocket == INVALID_SOCKET) {
        return false;
    }

    server->rtcpSocket = create_bound_udp_socket(config->rtp_tunnel_local_host, config->rtp_tunnel_rtcp_port);
    if (server->rtcpSocket == INVALID_SOCKET) {
        closesocket(server->rtpSocket);
        server->rtpSocket = INVALID_SOCKET;
        return false;
    }

    server->config = config;
    server->certDetails = certDetails;
    server->hClientCred = hClientCred;
    server->isRunning = true;

    printf("[RTP-TUNNEL] Ready: UDP %s:%d (RTP) and %s:%d (RTCP) -> %s:%d (L4RTP/1 mTLS, SN: %s)\n",
           config->rtp_tunnel_local_host, config->rtp_tunnel_rtp_port,
           config->rtp_tunnel_local_host, config->rtp_tunnel_rtcp_port,
           config->rtp_tunnel_remote_host, config->rtp_tunnel_remote_port,
           certDetails->sn);

    server->hThread = (HANDLE)_beginthreadex(NULL, 0, rtp_tunnel_worker_thread, server, 0, NULL);
    if (!server->hThread) {
        fprintf(stderr, "[RTP-TUNNEL] Failed to start worker thread: %lu\n", GetLastError());
        closesocket(server->rtpSocket);
        closesocket(server->rtcpSocket);
        server->rtpSocket = INVALID_SOCKET;
        server->rtcpSocket = INVALID_SOCKET;
        server->isRunning = false;
        return false;
    }

    return true;
}

void rtp_tunnel_stop(RtpTunnelServer* server) {
    if (!server) return;
    server->isRunning = false;

    if (server->rtpSocket != INVALID_SOCKET) {
        closesocket(server->rtpSocket);
        server->rtpSocket = INVALID_SOCKET;
    }
    if (server->rtcpSocket != INVALID_SOCKET) {
        closesocket(server->rtcpSocket);
        server->rtcpSocket = INVALID_SOCKET;
    }

    if (server->hThread) {
        WaitForSingleObject(server->hThread, 3000);
        CloseHandle(server->hThread);
        server->hThread = NULL;
    }
}
