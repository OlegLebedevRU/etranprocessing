#ifndef LEO4_MQTT_CLIENT_H
#define LEO4_MQTT_CLIENT_H

#include "config.h"

/**
 * @brief Queries active Device SN from local Leo4Proxy REST endpoint (GET /_leo4/sn).
 * @param proxy_port Local proxy HTTP port (default 18443).
 * @param out_sn Destination buffer for serial number string.
 * @param out_sn_size Destination buffer capacity.
 * @return 0 on success, -1 on failure.
 */
int mqtt_client_query_sn(int proxy_port, char* out_sn, size_t out_sn_size);

/**
 * @brief Runs the main MQTT presence / LWT client engine.
 *
 * Connects to Mosquitto bridge, configures LWT (dev/<SN>/svc = svc_offline, retain=1, QoS=1),
 * on CONNACK publishes dev/<SN>/svc = svc_online (retain=1, QoS=1),
 * maintains keepalive ping, and on graceful shutdown publishes svc_offline before DISCONNECT.
 *
 * @param config Application configuration.
 * @param stop_event Windows Event signaled when service or console is stopping.
 * @return 0 on clean exit, non-zero on fatal error.
 */
int mqtt_client_run(const AppConfig* config, HANDLE stop_event);

#endif /* LEO4_MQTT_CLIENT_H */
