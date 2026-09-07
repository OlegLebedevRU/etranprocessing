/**
 * @file stream_proxy.h
 * @brief Plain TCP <-> SChannel TLS Stream Forwarder (RTP/RTSP/MPEG-TS over mTLS) for Leo4Proxy.
 */

#ifndef LEO4_STREAM_PROXY_H
#define LEO4_STREAM_PROXY_H

#include "config.h"
#include "cert_store.h"
#include "schannel_tls.h"
#include <stdbool.h>

typedef struct {
    const ProxyConfig* config;
    const CertDetails* certDetails;
    CredHandle hClientCred;
    SOCKET listenSock;
    volatile bool isRunning;
    HANDLE hThread;
} StreamProxyServer;

/**
 * @brief Initializes and starts the Stream proxy listening thread.
 */
bool stream_proxy_start(StreamProxyServer* server, const ProxyConfig* config,
                        const CertDetails* certDetails, CredHandle hClientCred);

/**
 * @brief Stops the Stream proxy and closes sockets.
 */
void stream_proxy_stop(StreamProxyServer* server);

#endif /* LEO4_STREAM_PROXY_H */
