/**
 * @file reverse_proxy.h
 * @brief Reverse HTTPS Proxy and TLS Termination for Leo4Proxy.
 */

#ifndef LEO4_REVERSE_PROXY_H
#define LEO4_REVERSE_PROXY_H

#include "config.h"
#include "cert_store.h"
#include "schannel_tls.h"
#include <stdbool.h>
#include <windows.h>

typedef struct {
    const ProxyConfig* config;
    const CertDetails* certDetails;
    CredHandle hServerCred;
    SOCKET listenSock;
    SOCKET listenSock6;
    HANDLE hThread;
    SOCKET httpListenSock;
    SOCKET httpListenSock6;
    HANDLE hHttpThread;
    volatile bool isRunning;
} ReverseProxyServer;

/**
 * @brief Starts the Reverse HTTPS Proxy server thread (listening on 0.0.0.0:443 or configured host:port).
 */
bool reverse_proxy_start(ReverseProxyServer* server, const ProxyConfig* config, const CertDetails* certDetails, CredHandle hServerCred);

/**
 * @brief Stops the Reverse HTTPS Proxy server.
 */
void reverse_proxy_stop(ReverseProxyServer* server);

#endif /* LEO4_REVERSE_PROXY_H */
