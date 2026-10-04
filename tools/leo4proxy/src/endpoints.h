#ifndef LEO4_ENDPOINTS_H
#define LEO4_ENDPOINTS_H
#include "config.h"
#define ENDPOINT_MAX 16
enum { ENDPOINT_MQTT, ENDPOINT_HTTPS, ENDPOINT_STREAM, ENDPOINT_RTP, ENDPOINT_CHANNELS };
typedef struct {
    char host[MAX_HOST_LEN];
    int port;
    char source[24];
    unsigned short priority, weight;
} Leo4Endpoint;
void endpoints_init(const ProxyConfig* config);
void endpoints_identity(const char* sn);
void endpoints_accept(const char* text, size_t length, const char* sn);
int endpoints_candidates(const ProxyConfig* config, int channel, bool recovery, Leo4Endpoint* out);
bool endpoint_ipv4(const char* text);
bool endpoint_host_valid(const char* text);
bool endpoint_resolve_ipv4(const char* host, char out[16], DWORD timeout);
const char* endpoint_logical_name(const ProxyConfig* config, int channel);
void endpoints_connected(int channel, const Leo4Endpoint* endpoint);
void endpoints_diagnostics(char* out, size_t size);
void endpoints_shutdown(void);
#endif
