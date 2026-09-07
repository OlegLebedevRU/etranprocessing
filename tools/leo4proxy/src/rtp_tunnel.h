/**
 * @file rtp_tunnel.h
 * @brief Primary video media tunnel: local RTP/RTCP UDP -> framed mTLS/TCP (L4RTP/1) for Leo4Proxy.
 */

#ifndef LEO4_RTP_TUNNEL_H
#define LEO4_RTP_TUNNEL_H

#include "config.h"
#include "cert_store.h"
#include "schannel_tls.h"
#include <stdbool.h>

typedef struct {
    const ProxyConfig* config;
    const CertDetails* certDetails;
    CredHandle hClientCred;
    SOCKET rtpSocket;
    SOCKET rtcpSocket;
    volatile bool isRunning;
    HANDLE hThread;
} RtpTunnelServer;

/**
 * @brief Starts the RTP tunnel module (creates UDP sockets on loopback and starts worker thread).
 * Note: Lazy connect — outbound mTLS connection is opened only when first local UDP packet arrives.
 */
bool rtp_tunnel_start(RtpTunnelServer* server, const ProxyConfig* config,
                      const CertDetails* certDetails, CredHandle hClientCred);

/**
 * @brief Stops the RTP tunnel module, closes mTLS and UDP sockets, and terminates worker thread.
 */
void rtp_tunnel_stop(RtpTunnelServer* server);

#endif /* LEO4_RTP_TUNNEL_H */
