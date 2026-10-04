#ifndef LEO4_POLICY_H
#define LEO4_POLICY_H
#include "cert_store.h"
#define POLICY_GRACE_SECONDS (72ULL * 60 * 60)
#define POLICY_POLL_SECONDS 600
typedef struct PolicySocket {
    SOCKET socket;
    struct PolicySocket* next;
    bool registered;
} PolicySocket;
typedef struct {
    char sn[MAX_SN_LEN];
    unsigned long long generation, last_success_at, offline_allowed_until;
    bool known, allowed;
    char facts[1024];
} PolicyRecord;
bool policy_record_parse(const char* text, size_t len, const char* sn, PolicyRecord* record);
bool policy_response_parse(const char* text, size_t len, const char* sn,
                           bool* media, bool* https, char* facts, size_t facts_size);
bool policy_record_allowed(const PolicyRecord* record, unsigned long long now);
bool policy_record_prefer(const PolicyRecord* candidate, const PolicyRecord* current);
void policy_init(const ProxyConfig* config);
void policy_identity(const CertDetails* details);
void policy_stop(void);
bool policy_media_allowed(void);
bool policy_probe_media_allowed(const char* sn);
bool policy_probe_bootstrap(const ProxyConfig* config, const CertDetails* cert);
bool policy_https_path_allowed(const char* path);
void policy_diagnostics(char* out, size_t size);
/* Called with a nonblocking socket. Registration and connect share the policy lock. */
int policy_media_connect(PolicySocket* node, SOCKET socket, const struct sockaddr* address, int length);
void policy_socket_unregister(PolicySocket* node);
#endif
