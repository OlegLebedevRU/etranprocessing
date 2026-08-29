/**
 * @file http_proxy.c
 * @brief HTTP -> HTTPS SChannel mTLS Proxy and Device Info Endpoint for Leo4Proxy.
 */

#include "http_proxy.h"
#include <ws2tcpip.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ProxyStats g_proxyStats = { 0 };

bool is_loopback_sockaddr(const struct sockaddr* sa) {
    if (!sa) return false;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in* sin = (const struct sockaddr_in*)sa;
        return (ntohl(sin->sin_addr.s_addr) >> 24) == 127;
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6* sin6 = (const struct sockaddr_in6*)sa;
        return memcmp(&sin6->sin6_addr, &in6addr_loopback, sizeof(struct in6_addr)) == 0;
    }
    return false;
}

bool tcp_probe_connect(const char* host, int port, int timeout_ms) {
    if (!host || host[0] == '\0' || port <= 0) return false;

    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, portStr, &hints, &res) != 0 || !res) {
        return false;
    }

    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) {
        freeaddrinfo(res);
        return false;
    }

    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);

    bool connected = false;
    int rc = connect(s, res->ai_addr, (int)res->ai_addrlen);
    if (rc == 0) {
        connected = true;
    } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set writefds, exceptfds;
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);
        FD_SET(s, &writefds);
        FD_SET(s, &exceptfds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int sel = select((int)(s + 1), NULL, &writefds, &exceptfds, &tv);
        if (sel > 0 && FD_ISSET(s, &writefds) && !FD_ISSET(s, &exceptfds)) {
            int err = 0;
            int errLen = sizeof(err);
            if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &errLen) == 0 && err == 0) {
                connected = true;
            }
        }
    }

    closesocket(s);
    freeaddrinfo(res);
    return connected;
}

typedef struct {
    SOCKET clientSock;
    HttpProxyServer* server;
} HttpClientWorkerArgs;

static int send_all_socket(SOCKET s, const BYTE* data, int len) {
    int total = 0;
    while (total < len) {
        int sent = send(s, (const char*)(data + total), len - total, 0);
        if (sent <= 0) return sent;
        total += sent;
    }
    return total;
}

static int send_http_response_ext(SOCKET s, SChannelSession* tlsSession, int statusCode, const char* statusText, const char* contentType, const char* body, const char* sn) {
    char headerBuf[1024];
    int bodyLen = body ? (int)strlen(body) : 0;

    int headerLen = snprintf(headerBuf, sizeof(headerBuf),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "X-Leo4-Proxy-Sn: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        statusCode, statusText, contentType, bodyLen, sn ? sn : "");

    if (tlsSession) {
        schannel_send(tlsSession, headerBuf, headerLen);
        if (bodyLen > 0) {
            schannel_send(tlsSession, body, bodyLen);
        }
    } else {
        send(s, headerBuf, headerLen, 0);
        if (bodyLen > 0) {
            send(s, body, bodyLen, 0);
        }
    }
    return 0;
}

static void handle_info_request_ext(SOCKET s, SChannelSession* tlsSession, const ProxyConfig* config, const CertDetails* certDetails, bool is_local) {
    bool cert_ready = (certDetails != NULL && certDetails->sn[0] != '\0' && g_proxyStats.cert_ready);
    bool backend_online = tcp_probe_connect(config->reverse_target_host, config->reverse_target_port, 250);

    if (is_local) {
        char jsonBuf[4096];
        if (cert_ready) {
            snprintf(jsonBuf, sizeof(jsonBuf),
                "{\n"
                "  \"status\": \"ready\",\n"
                "  \"certificate_found\": true,\n"
                "  \"version\": \"%s\",\n"
                "  \"sn\": \"%s\",\n"
                "  \"client_id\": \"%s\",\n"
                "  \"urn\": \"%s\",\n"
                "  \"email\": \"%s\",\n"
                "  \"subject\": \"%s\",\n"
                "  \"issuer\": \"%s\",\n"
                "  \"serial\": \"%s\",\n"
                "  \"thumbprint\": \"%s\",\n"
                "  \"not_before\": \"%s\",\n"
                "  \"not_after\": \"%s\",\n"
                "  \"has_private_key\": %s,\n"
                "  \"local_hostname\": \"%s\",\n"
                "  \"san_dns\": \"%s\",\n"
                "  \"listeners\": {\n"
                "    \"mqtt_local\": \"%s:%d\",\n"
                "    \"http_local\": \"%s:%d\",\n"
                "    \"reverse_listen\": \"%s:%d\"\n"
                "  },\n"
                "  \"upstreams\": {\n"
                "    \"mqtt_remote\": \"%s:%d\",\n"
                "    \"http_remote\": \"https://%s:%d\",\n"
                "    \"reverse_target\": \"http://%s:%d\"\n"
                "  },\n"
                "  \"routes_active\": true,\n"
                "  \"clients\": {\n"
                "    \"mqtt_active_clients\": %ld,\n"
                "    \"mqtt_total_connections\": %ld,\n"
                "    \"http_total_requests\": %ld,\n"
                "    \"reverse_total_requests\": %ld\n"
                "  },\n"
                "  \"backend_service\": {\n"
                "    \"online\": %s,\n"
                "    \"target\": \"%s:%d\"\n"
                "  }\n"
                "}\n",
                LEO4_PROXY_VERSION,
                certDetails->sn,
                certDetails->sn,
                certDetails->urn,
                certDetails->email,
                certDetails->subject,
                certDetails->issuer,
                certDetails->serial,
                certDetails->thumbprint,
                certDetails->not_before,
                certDetails->not_after,
                certDetails->has_private_key ? "true" : "false",
                certDetails->local_hostname,
                certDetails->san_dns,
                config->mqtt_local_host, config->mqtt_local_port,
                config->http_local_host, config->http_local_port,
                config->reverse_local_host, config->reverse_local_port,
                config->mqtt_remote_host, config->mqtt_remote_port,
                config->http_remote_host, config->http_remote_port,
                config->reverse_target_host, config->reverse_target_port,
                g_proxyStats.mqtt_active_clients,
                g_proxyStats.mqtt_total_connections,
                g_proxyStats.http_total_requests,
                g_proxyStats.reverse_total_requests,
                backend_online ? "true" : "false",
                config->reverse_target_host, config->reverse_target_port
            );
        } else {
            snprintf(jsonBuf, sizeof(jsonBuf),
                "{\n"
                "  \"status\": \"waiting_for_certificate\",\n"
                "  \"certificate_found\": false,\n"
                "  \"version\": \"%s\",\n"
                "  \"sn\": \"\",\n"
                "  \"client_id\": \"\",\n"
                "  \"local_hostname\": \"leo4-device.local\",\n"
                "  \"san_dns\": \"\",\n"
                "  \"listeners\": {\n"
                "    \"mqtt_local\": \"%s:%d (disabled)\",\n"
                "    \"http_local\": \"%s:%d\",\n"
                "    \"reverse_listen\": \"%s:%d (disabled)\"\n"
                "  },\n"
                "  \"upstreams\": {\n"
                "    \"mqtt_remote\": \"%s:%d (disabled)\",\n"
                "    \"http_remote\": \"https://%s:%d (disabled)\",\n"
                "    \"reverse_target\": \"http://%s:%d (disabled)\"\n"
                "  },\n"
                "  \"routes_active\": false,\n"
                "  \"clients\": {\n"
                "    \"mqtt_active_clients\": %ld,\n"
                "    \"mqtt_total_connections\": %ld,\n"
                "    \"http_total_requests\": %ld,\n"
                "    \"reverse_total_requests\": %ld\n"
                "  },\n"
                "  \"backend_service\": {\n"
                "    \"online\": %s,\n"
                "    \"target\": \"%s:%d\"\n"
                "  }\n"
                "}\n",
                LEO4_PROXY_VERSION,
                config->mqtt_local_host, config->mqtt_local_port,
                config->http_local_host, config->http_local_port,
                config->reverse_local_host, config->reverse_local_port,
                config->mqtt_remote_host, config->mqtt_remote_port,
                config->http_remote_host, config->http_remote_port,
                config->reverse_target_host, config->reverse_target_port,
                g_proxyStats.mqtt_active_clients,
                g_proxyStats.mqtt_total_connections,
                g_proxyStats.http_total_requests,
                g_proxyStats.reverse_total_requests,
                backend_online ? "true" : "false",
                config->reverse_target_host, config->reverse_target_port
            );
        }
        send_http_response_ext(s, tlsSession, 200, "OK", "application/json; charset=utf-8", jsonBuf, cert_ready ? certDetails->sn : NULL);
    } else {
        char jsonBuf[2048];
        if (cert_ready) {
            snprintf(jsonBuf, sizeof(jsonBuf),
                "{\n"
                "  \"status\": \"ready\",\n"
                "  \"sn\": \"%s\",\n"
                "  \"dns\": \"%s\",\n"
                "  \"local_hostname\": \"%s\",\n"
                "  \"backend_online\": %s,\n"
                "  \"internal_clients\": {\n"
                "    \"mqtt_connected\": %ld,\n"
                "    \"http_active\": true,\n"
                "    \"requests_count\": %ld\n"
                "  }\n"
                "}\n",
                certDetails->sn,
                certDetails->local_hostname,
                certDetails->local_hostname,
                backend_online ? "true" : "false",
                g_proxyStats.mqtt_active_clients,
                g_proxyStats.http_total_requests + g_proxyStats.reverse_total_requests
            );
        } else {
            snprintf(jsonBuf, sizeof(jsonBuf),
                "{\n"
                "  \"status\": \"waiting_for_certificate\",\n"
                "  \"sn\": null,\n"
                "  \"dns\": \"leo4-device.local\",\n"
                "  \"local_hostname\": \"leo4-device.local\",\n"
                "  \"backend_online\": %s,\n"
                "  \"internal_clients\": {\n"
                "    \"mqtt_connected\": 0,\n"
                "    \"http_active\": false,\n"
                "    \"requests_count\": %ld\n"
                "  }\n"
                "}\n",
                backend_online ? "true" : "false",
                g_proxyStats.http_total_requests + g_proxyStats.reverse_total_requests
            );
        }
        send_http_response_ext(s, tlsSession, 200, "OK", "application/json; charset=utf-8", jsonBuf, cert_ready ? certDetails->sn : NULL);
    }
}

static void handle_sn_request_ext(SOCKET s, SChannelSession* tlsSession, const CertDetails* certDetails) {
    if (certDetails && certDetails->sn[0] != '\0' && g_proxyStats.cert_ready) {
        send_http_response_ext(s, tlsSession, 200, "OK", "text/plain; charset=utf-8", certDetails->sn, certDetails->sn);
    } else {
        send_http_response_ext(s, tlsSession, 503, "Service Unavailable", "text/plain; charset=utf-8", "Waiting for certificate\n", NULL);
    }
}

static unsigned __stdcall http_client_worker(void* param) {
    HttpClientWorkerArgs* args = (HttpClientWorkerArgs*)param;
    SOCKET clientSock = args->clientSock;
    HttpProxyServer* server = args->server;
    const ProxyConfig* config = server->config;
    free(args);

    InterlockedIncrement(&g_proxyStats.http_total_requests);

    struct sockaddr_storage peerAddr;
    int peerLen = sizeof(peerAddr);
    memset(&peerAddr, 0, sizeof(peerAddr));
    getpeername(clientSock, (struct sockaddr*)&peerAddr, &peerLen);
    bool is_local = is_loopback_sockaddr((struct sockaddr*)&peerAddr);

    const CertDetails* certDetails = server->certDetails;
    CredHandle hClientCred = server->hClientCred;
    CredHandle hServerCred = server->hServerCred;
    bool cert_ready = (certDetails != NULL && certDetails->sn[0] != '\0' && g_proxyStats.cert_ready);
    const char* active_sn = cert_ready ? certDetails->sn : "";

    BOOL keepAlive = TRUE;
    setsockopt(clientSock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&keepAlive, sizeof(keepAlive));

    // Auto-detect or enforce TLS on incoming client socket
    bool isClientTls = false;
    BYTE peekByte = 0;
    int p = recv(clientSock, (char*)&peekByte, 1, MSG_PEEK);
    if (config->http_local_ssl || (config->auto_local_ssl && p == 1 && peekByte == 0x16)) {
        isClientTls = true;
    }

    SChannelSession clientTlsSession;
    memset(&clientTlsSession, 0, sizeof(SChannelSession));

    if (isClientTls && SecIsValidHandle(&hServerCred)) {
        if (config->verbose) {
            printf("[HTTP-PROXY] Inbound TLS handshake from local client...\n");
        }
        if (!schannel_accept(&clientTlsSession, &hServerCred, clientSock)) {
            if (config->verbose) {
                fprintf(stderr, "[HTTP-PROXY] Inbound TLS handshake failed.\n");
            }
            closesocket(clientSock);
            return 1;
        }
    } else if (isClientTls) {
        isClientTls = false;
    }

    char* reqBuf = (char*)malloc(65536);
    char* modifiedReq = (char*)malloc(131072);
    if (!reqBuf || !modifiedReq) {
        if (reqBuf) free(reqBuf);
        if (modifiedReq) free(modifiedReq);
        if (isClientTls) schannel_close(&clientTlsSession);
        else closesocket(clientSock);
        return 1;
    }

    // 1. Read HTTP request from local client
    int reqLen = 0;
    char* headerEnd = NULL;

    while (reqLen < 65536 - 1) {
        int r = isClientTls ? schannel_recv(&clientTlsSession, reqBuf + reqLen, 65536 - 1 - reqLen)
                            : recv(clientSock, reqBuf + reqLen, 65536 - 1 - reqLen, 0);
        if (r <= 0) break;
        reqLen += r;
        reqBuf[reqLen] = '\0';

        headerEnd = strstr(reqBuf, "\r\n\r\n");
        if (headerEnd) {
            // Check Content-Length for body
            int contentLen = 0;
            const char* clHeader = strstr(reqBuf, "Content-Length:");
            if (!clHeader) clHeader = strstr(reqBuf, "content-length:");
            if (clHeader && clHeader < headerEnd) {
                contentLen = atoi(clHeader + 15);
            }

            int headerBytes = (int)(headerEnd + 4 - reqBuf);
            int bodyBytes = reqLen - headerBytes;

            while (bodyBytes < contentLen && reqLen < 65536 - 1) {
                int r2 = isClientTls ? schannel_recv(&clientTlsSession, reqBuf + reqLen, 65536 - 1 - reqLen)
                                     : recv(clientSock, reqBuf + reqLen, 65536 - 1 - reqLen, 0);
                if (r2 <= 0) break;
                reqLen += r2;
                reqBuf[reqLen] = '\0';
                bodyBytes = reqLen - headerBytes;
            }
            break;
        }
    }

    if (reqLen <= 0 || !headerEnd) {
        free(reqBuf);
        free(modifiedReq);
        if (isClientTls) schannel_close(&clientTlsSession);
        else closesocket(clientSock);
        return 1;
    }

    // 2. Parse request line (e.g. GET /_leo4/info HTTP/1.1)
    char method[32] = { 0 };
    char path[1024] = { 0 };
    char version[32] = { 0 };
    sscanf_s(reqBuf, "%31s %1023s %31s", method, (unsigned)sizeof(method), path, (unsigned)sizeof(path), version, (unsigned)sizeof(version));

    // Handle CORS preflight OPTIONS
    if (_stricmp(method, "OPTIONS") == 0) {
        send_http_response_ext(clientSock, isClientTls ? &clientTlsSession : NULL, 204, "No Content", "text/plain", "", active_sn);
        free(reqBuf);
        free(modifiedReq);
        if (isClientTls) schannel_close(&clientTlsSession);
        else closesocket(clientSock);
        return 0;
    }

    // 3. Built-in Local Endpoints
    if (_stricmp(path, "/_leo4/info") == 0 ||
        _stricmp(path, "/_leo4/status") == 0 ||
        _stricmp(path, "/status") == 0 ||
        _stricmp(path, "/info") == 0) {
        handle_info_request_ext(clientSock, isClientTls ? &clientTlsSession : NULL, config, certDetails, is_local);
        free(reqBuf);
        free(modifiedReq);
        if (isClientTls) schannel_close(&clientTlsSession);
        else closesocket(clientSock);
        return 0;
    }

    if (_stricmp(path, "/_leo4/sn") == 0 || _stricmp(path, "/sn") == 0) {
        handle_sn_request_ext(clientSock, isClientTls ? &clientTlsSession : NULL, certDetails);
        free(reqBuf);
        free(modifiedReq);
        if (isClientTls) schannel_close(&clientTlsSession);
        else closesocket(clientSock);
        return 0;
    }

    // 4. Check if upstream proxy routes are active
    if (!cert_ready || !SecIsValidHandle(&hClientCred)) {
        const char* errJson = "{\"error\": \"Proxy routes disabled: waiting for certificate\"}\r\n";
        send_http_response_ext(clientSock, isClientTls ? &clientTlsSession : NULL, 503, "Service Unavailable", "application/json; charset=utf-8", errJson, NULL);
        free(reqBuf);
        free(modifiedReq);
        if (isClientTls) schannel_close(&clientTlsSession);
        else closesocket(clientSock);
        return 1;
    }

    // 5. Proxy to Remote HTTPS Backend
    if (config->verbose) {
        printf("[HTTP-PROXY] %s %s -> https://%s:%d (mTLS SN=%s)%s\n",
               method, path, config->http_remote_host, config->http_remote_port, certDetails->sn,
               isClientTls ? " [client TLS]" : "");
    }

    // Build modified request to forward
    // Extract body if any
    const char* bodyStart = headerEnd + 4;
    int modLen = 0;

    // Line 1: method path version
    modLen += snprintf(modifiedReq + modLen, 131072 - modLen, "%s %s %s\r\n", method, path, version);

    // Inject Host and X-Leo4-Proxy-Sn headers
    if (config->http_remote_port == 443) {
        modLen += snprintf(modifiedReq + modLen, 131072 - modLen, "Host: %s\r\n", config->http_remote_host);
    } else {
        modLen += snprintf(modifiedReq + modLen, 131072 - modLen, "Host: %s:%d\r\n", config->http_remote_host, config->http_remote_port);
    }
    modLen += snprintf(modifiedReq + modLen, 131072 - modLen, "X-Leo4-Proxy-Sn: %s\r\n", certDetails->sn);
    modLen += snprintf(modifiedReq + modLen, 131072 - modLen, "Connection: close\r\n");

    // Copy original headers skipping Host and Connection
    char* lineStart = strstr(reqBuf, "\r\n");
    if (lineStart && lineStart < headerEnd) {
        lineStart += 2;
        while (lineStart < headerEnd) {
            char* lineEnd = strstr(lineStart, "\r\n");
            if (!lineEnd || lineEnd > headerEnd) lineEnd = headerEnd;

            int lineLen = (int)(lineEnd - lineStart);
            if (lineLen > 0) {
                if (_strnicmp(lineStart, "Host:", 5) != 0 &&
                    _strnicmp(lineStart, "Connection:", 11) != 0 &&
                    _strnicmp(lineStart, "X-Leo4-Proxy-Sn:", 16) != 0) {
                    if (modLen + lineLen + 2 < 131072) {
                        memcpy(modifiedReq + modLen, lineStart, lineLen);
                        modLen += lineLen;
                        memcpy(modifiedReq + modLen, "\r\n", 2);
                        modLen += 2;
                    }
                }
            }
            lineStart = lineEnd + 2;
        }
    }

    // End of headers
    if (modLen + 2 < 131072) {
        memcpy(modifiedReq + modLen, "\r\n", 2);
        modLen += 2;
    }

    // Append body if present
    int bodyBytes = reqLen - (int)(bodyStart - reqBuf);
    if (bodyBytes > 0 && modLen + bodyBytes < 131072) {
        memcpy(modifiedReq + modLen, bodyStart, bodyBytes);
        modLen += bodyBytes;
    }

    free(reqBuf); // reqBuf no longer needed

    // Connect to backend via SChannel
    SChannelSession remoteTlsSession;
    if (!schannel_connect(&remoteTlsSession, &hClientCred, config->http_remote_host, config->http_remote_port, 10000, config->insecure_server_cert)) {
        fprintf(stderr, "[HTTP-PROXY] Failed to establish mTLS connection to %s:%d\n",
                config->http_remote_host, config->http_remote_port);
        const char* errJson = "{\"error\": \"Failed to connect to upstream backend\"}";
        send_http_response_ext(clientSock, isClientTls ? &clientTlsSession : NULL, 502, "Bad Gateway", "application/json", errJson, certDetails->sn);
        free(modifiedReq);
        if (isClientTls) schannel_close(&clientTlsSession);
        else closesocket(clientSock);
        return 1;
    }

    // Send HTTP request over TLS
    int sent = schannel_send(&remoteTlsSession, modifiedReq, modLen);
    free(modifiedReq); // modifiedReq no longer needed

    if (sent <= 0) {
        fprintf(stderr, "[HTTP-PROXY] Failed to send request over TLS\n");
        const char* errJson = "{\"error\": \"Failed to send request to upstream\"}";
        send_http_response_ext(clientSock, isClientTls ? &clientTlsSession : NULL, 502, "Bad Gateway", "application/json", errJson, certDetails->sn);
        schannel_close(&remoteTlsSession);
        if (isClientTls) schannel_close(&clientTlsSession);
        else closesocket(clientSock);
        return 1;
    }

    // Stream response from remote TLS to client socket (plain or client TLS)
    BYTE respBuf[16384];
    while (true) {
        int recvd = schannel_recv(&remoteTlsSession, respBuf, sizeof(respBuf));
        if (recvd <= 0) {
            break;
        }

        int s = isClientTls ? schannel_send(&clientTlsSession, respBuf, recvd)
                            : send_all_socket(clientSock, respBuf, recvd);
        if (s <= 0) {
            break;
        }
    }

    schannel_close(&remoteTlsSession);
    if (isClientTls) schannel_close(&clientTlsSession);
    else closesocket(clientSock);
    return 0;
}

static unsigned __stdcall http_listener_thread(void* param) {
    HttpProxyServer* server = (HttpProxyServer*)param;
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

                if (strcmp(config->http_local_host, "127.0.0.1") == 0 &&
                    strcmp(clientIp, "127.0.0.1") != 0 &&
                    strcmp(clientIp, "::1") != 0) {
                    fprintf(stderr, "[HTTP-PROXY] Rejected connection from non-loopback IP: %s\n", clientIp);
                    closesocket(clientSock);
                    continue;
                }

                HttpClientWorkerArgs* args = (HttpClientWorkerArgs*)malloc(sizeof(HttpClientWorkerArgs));
                if (args) {
                    args->clientSock = clientSock;
                    args->server = server;

                    HANDLE hWorker = (HANDLE)_beginthreadex(NULL, 0, http_client_worker, args, 0, NULL);
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

bool http_proxy_start(HttpProxyServer* server, const ProxyConfig* config, const CertDetails* certDetails, CredHandle hClientCred, CredHandle hServerCred) {
    if (!server || !config) return false;
    memset(server, 0, sizeof(HttpProxyServer));

    server->config = config;
    server->certDetails = certDetails;
    server->hClientCred = hClientCred;
    server->hServerCred = hServerCred;
    server->listenSock = INVALID_SOCKET;
    server->isRunning = true;

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)config->http_local_port);
    inet_pton(AF_INET, config->http_local_host, &addr.sin_addr);

    server->listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->listenSock == INVALID_SOCKET) {
        fprintf(stderr, "[HTTP-PROXY] Failed to create listening socket: %d\n", WSAGetLastError());
        server->isRunning = false;
        return false;
    }

    BOOL opt = TRUE;
    setsockopt(server->listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    if (bind(server->listenSock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        int err = WSAGetLastError();
        fprintf(stderr, "[HTTP-PROXY] Failed to bind to %s:%d (error: %d)\n",
                config->http_local_host, config->http_local_port, err);
        if (err == 10048) {
            fprintf(stderr, "[HTTP-PROXY] Port %d is already in use! If Leo4Proxy is already running as a Windows Service, stop it first.\n",
                    config->http_local_port);
        }
        closesocket(server->listenSock);
        server->listenSock = INVALID_SOCKET;
        server->isRunning = false;
        return false;
    }

    if (listen(server->listenSock, SOMAXCONN) != 0) {
        fprintf(stderr, "[HTTP-PROXY] Failed to listen on %s:%d: %d\n",
                config->http_local_host, config->http_local_port, WSAGetLastError());
        closesocket(server->listenSock);
        server->listenSock = INVALID_SOCKET;
        server->isRunning = false;
        return false;
    }

    printf("[HTTP-PROXY] Listening on %s:%d -> https://%s:%d (mTLS)%s\n",
           config->http_local_host, config->http_local_port,
           config->http_remote_host, config->http_remote_port,
           config->http_local_ssl ? " [local SSL enabled]" : " [local HTTP/HTTPS auto-detect]");

    server->hThread = (HANDLE)_beginthreadex(NULL, 0, http_listener_thread, server, 0, NULL);
    if (!server->hThread) {
        fprintf(stderr, "[HTTP-PROXY] Failed to start listener thread: %lu\n", GetLastError());
        closesocket(server->listenSock);
        server->listenSock = INVALID_SOCKET;
        server->isRunning = false;
        return false;
    }

    return true;
}

void http_proxy_update_creds(HttpProxyServer* server, const CertDetails* certDetails, CredHandle hClientCred, CredHandle hServerCred) {
    if (!server) return;
    server->certDetails = certDetails;
    server->hClientCred = hClientCred;
    server->hServerCred = hServerCred;
    MemoryBarrier();
}

void http_proxy_stop(HttpProxyServer* server) {
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
