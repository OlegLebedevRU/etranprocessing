/**
 * @file discovery.h
 * @brief Zero-dependency LAN Service Discovery (mDNS .local, DNS-SD, and LLMNR) for Leo4Proxy.
 */

#ifndef LEO4_DISCOVERY_H
#define LEO4_DISCOVERY_H

#include "config.h"
#include "cert_store.h"
#include <stdbool.h>
#include <stdint.h>
#include <windows.h>
#include <winsock2.h>

typedef struct {
    const ProxyConfig* config;
    const CertDetails* certDetails;
    char hostname[256];        /* e.g. "leo4-0000773.local" */
    char label[128];           /* e.g. "leo4-0000773" */
    char lan_ip_str[64];       /* e.g. "192.168.1.50" */
    uint32_t lan_ip_n;         /* Network byte order IPv4 */
    int http_port;             /* Reverse HTTPS port (443) */

    SOCKET mdnsSock;
    SOCKET llmnrSock;
    HANDLE hThread;
    volatile bool isRunning;
} DiscoveryServer;

/**
 * @brief Starts the mDNS and LLMNR discovery and announcement server thread.
 */
bool discovery_start(DiscoveryServer* server, const ProxyConfig* config, const CertDetails* certDetails);

/**
 * @brief Stops the discovery server thread.
 */
void discovery_stop(DiscoveryServer* server);

/**
 * @brief Helper to query the best active LAN IPv4 address string.
 */
bool discovery_get_lan_ip(char* out_ip, size_t out_ip_size, uint32_t* out_ip_n);

#endif /* LEO4_DISCOVERY_H */
