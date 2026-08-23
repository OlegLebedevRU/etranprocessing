/**
 * @file http_proxy.h
 * @brief HTTP -> HTTPS SChannel mTLS Proxy and Device Info Endpoint for Leo4Proxy.
 */

#ifndef LEO4_HTTP_PROXY_H
#define LEO4_HTTP_PROXY_H

#include "config.h"
#include "cert_store.h"
#include "schannel_tls.h"
#include <stdbool.h>

typedef struct {
    const ProxyConfig* config;
    const CertDetails* certDetails;
    CredHandle hCred;
    SOCKET listenSock;
    volatile bool isRunning;
    HANDLE hThread;
} HttpProxyServer;

/**
 * @brief Initializes and starts the HTTP->HTTPS proxy listening thread.
 */
bool http_proxy_start(HttpProxyServer* server, const ProxyConfig* config, const CertDetails* certDetails, CredHandle hCred);

/**
 * @brief Stops the HTTP proxy and closes sockets.
 */
void http_proxy_stop(HttpProxyServer* server);

#endif /* LEO4_HTTP_PROXY_H */
