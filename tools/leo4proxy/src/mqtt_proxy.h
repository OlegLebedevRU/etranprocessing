/**
 * @file mqtt_proxy.h
 * @brief Plain TCP <-> SChannel TLS MQTT Proxy for Leo4Proxy.
 */

#ifndef LEO4_MQTT_PROXY_H
#define LEO4_MQTT_PROXY_H

#include "config.h"
#include "cert_store.h"
#include "schannel_tls.h"
#include <stdbool.h>

typedef struct {
    const ProxyConfig* config;
    const CertDetails* certDetails;
    CredHandle hClientCred;
    CredHandle hServerCred;
    SOCKET listenSock;
    volatile bool isRunning;
    HANDLE hThread;
} MqttProxyServer;

/**
 * @brief Initializes and starts the MQTT proxy listening thread.
 */
bool mqtt_proxy_start(MqttProxyServer* server, const ProxyConfig* config, const CertDetails* certDetails, CredHandle hClientCred, CredHandle hServerCred);

/**
 * @brief Stops the MQTT proxy and closes sockets.
 */
void mqtt_proxy_stop(MqttProxyServer* server);

#endif /* LEO4_MQTT_PROXY_H */
