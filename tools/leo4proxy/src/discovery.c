/**
 * @file discovery.c
 * @brief Zero-dependency LAN Service Discovery (mDNS .local, DNS-SD, and LLMNR) for Leo4Proxy.
 */

#include "discovery.h"
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#define MDNS_MULTICAST_IPV4 "224.0.0.251"
#define MDNS_PORT 5353

#define LLMNR_MULTICAST_IPV4 "224.0.0.252"
#define LLMNR_PORT 5355

#define DNS_TYPE_A     1
#define DNS_TYPE_PTR   12
#define DNS_TYPE_TXT   16
#define DNS_TYPE_SRV   33
#define DNS_TYPE_ANY   255

#define DNS_CLASS_IN   1

/* DNS Packet Header */
#pragma pack(push, 1)
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} DnsHeader;
#pragma pack(pop)

bool discovery_get_lan_ip(char* out_ip, size_t out_ip_size, uint32_t* out_ip_n) {
    if (!out_ip || out_ip_size == 0) return false;
    out_ip[0] = '\0';
    if (out_ip_n) *out_ip_n = 0;

    ULONG bufLen = 16384;
    PIP_ADAPTER_ADDRESSES pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
    if (!pAddresses) return false;

    DWORD flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    DWORD ret = GetAdaptersAddresses(AF_INET, flags, NULL, pAddresses, &bufLen);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        free(pAddresses);
        pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
        if (!pAddresses) return false;
        ret = GetAdaptersAddresses(AF_INET, flags, NULL, pAddresses, &bufLen);
    }

    if (ret != NO_ERROR) {
        free(pAddresses);
        return false;
    }

    uint32_t bestIp = 0;
    int bestScore = -1;

    for (PIP_ADAPTER_ADDRESSES pCurr = pAddresses; pCurr != NULL; pCurr = pCurr->Next) {
        if (pCurr->OperStatus != IfOperStatusUp) continue;
        if (pCurr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

        for (PIP_ADAPTER_UNICAST_ADDRESS pUni = pCurr->FirstUnicastAddress; pUni != NULL; pUni = pUni->Next) {
            if (pUni->Address.lpSockaddr->sa_family == AF_INET) {
                struct sockaddr_in* sa = (struct sockaddr_in*)pUni->Address.lpSockaddr;
                uint32_t ip = sa->sin_addr.s_addr;
                BYTE b1 = (BYTE)(ip & 0xFF);
                BYTE b2 = (BYTE)((ip >> 8) & 0xFF);

                // Skip 127.x.x.x
                if (b1 == 127) continue;

                int score = 10;
                // Prefer non-APIPA (not 169.254.x.x)
                if (b1 == 169 && b2 == 254) {
                    score = 1;
                } else {
                    score = 20;
                }

                // Prefer adapter with configured default gateway
                if (pCurr->FirstGatewayAddress != NULL) {
                    score += 50;
                }

                if (pCurr->IfType == IF_TYPE_ETHERNET_CSMACD) {
                    score += 15;
                } else if (pCurr->IfType == IF_TYPE_IEEE80211) {
                    score += 10;
                }

                if (score > bestScore) {
                    bestScore = score;
                    bestIp = ip;
                }
            }
        }
    }

    free(pAddresses);

    if (bestIp != 0) {
        struct in_addr in;
        in.s_addr = bestIp;
        inet_ntop(AF_INET, &in, out_ip, (socklen_t)out_ip_size);
        if (out_ip_n) *out_ip_n = bestIp;
        return true;
    }

    // Fallback to loopback
    strncpy_s(out_ip, out_ip_size, "127.0.0.1", _TRUNCATE);
    if (out_ip_n) {
        struct in_addr in;
        inet_pton(AF_INET, "127.0.0.1", &in);
        *out_ip_n = in.s_addr;
    }
    return true;
}

static int dns_encode_name(BYTE* buf, int max_len, const char* name) {
    if (!buf || !name || max_len < 2) return 0;
    int pos = 0;
    const char* p = name;

    while (*p) {
        const char* dot = strchr(p, '.');
        int label_len = dot ? (int)(dot - p) : (int)strlen(p);
        if (label_len > 63 || pos + label_len + 1 >= max_len) return 0;

        buf[pos++] = (BYTE)label_len;
        memcpy(buf + pos, p, label_len);
        pos += label_len;

        if (!dot) break;
        p = dot + 1;
    }

    if (pos >= max_len) return 0;
    buf[pos++] = 0x00;
    return pos;
}

static int dns_decode_name(const BYTE* pkt, int pkt_len, int offset, char* out_name, int max_name) {
    if (!pkt || offset < 0 || offset >= pkt_len || !out_name || max_name < 1) return -1;
    out_name[0] = '\0';

    int cur = offset;
    int jumped = 0;
    int next_offset = -1;
    int out_pos = 0;
    int loop_guard = 0;

    while (cur < pkt_len && loop_guard++ < 64) {
        BYTE len = pkt[cur];
        if (len == 0) {
            if (!jumped) next_offset = cur + 1;
            break;
        }

        if ((len & 0xC0) == 0xC0) {
            // Compression pointer
            if (cur + 1 >= pkt_len) return -1;
            if (!jumped) next_offset = cur + 2;
            cur = (((int)(len & 0x3F)) << 8) | (int)pkt[cur + 1];
            jumped = 1;
            continue;
        }

        cur++;
        if (cur + len > pkt_len) return -1;

        if (out_pos > 0 && out_pos + 1 < max_name) {
            out_name[out_pos++] = '.';
        }

        for (int i = 0; i < len && out_pos + 1 < max_name; i++) {
            out_name[out_pos++] = (char)tolower((unsigned char)pkt[cur + i]);
        }
        cur += len;
    }

    out_name[out_pos] = '\0';
    return (next_offset != -1) ? next_offset : cur + 1;
}

static int append_a_record(BYTE* buf, int max_len, const char* name, uint32_t ip_n, uint32_t ttl, bool cache_flush) {
    int pos = dns_encode_name(buf, max_len, name);
    if (pos == 0 || pos + 10 + 4 > max_len) return 0;

    uint16_t type = htons(DNS_TYPE_A);
    uint16_t cls = htons((uint16_t)(DNS_CLASS_IN | (cache_flush ? 0x8000 : 0)));
    uint32_t rttl = htonl(ttl);
    uint16_t rdlen = htons(4);

    memcpy(buf + pos, &type, 2); pos += 2;
    memcpy(buf + pos, &cls, 2); pos += 2;
    memcpy(buf + pos, &rttl, 4); pos += 4;
    memcpy(buf + pos, &rdlen, 2); pos += 2;
    memcpy(buf + pos, &ip_n, 4); pos += 4;

    return pos;
}

static int append_ptr_record(BYTE* buf, int max_len, const char* service, const char* target, uint32_t ttl, bool cache_flush) {
    int pos = dns_encode_name(buf, max_len, service);
    if (pos == 0 || pos + 10 > max_len) return 0;

    uint16_t type = htons(DNS_TYPE_PTR);
    uint16_t cls = htons((uint16_t)(DNS_CLASS_IN | (cache_flush ? 0x8000 : 0)));
    uint32_t rttl = htonl(ttl);

    memcpy(buf + pos, &type, 2); pos += 2;
    memcpy(buf + pos, &cls, 2); pos += 2;
    memcpy(buf + pos, &rttl, 4); pos += 4;

    int rdlen_pos = pos;
    pos += 2; // placeholder for rdlen

    int target_len = dns_encode_name(buf + pos, max_len - pos, target);
    if (target_len == 0) return 0;
    pos += target_len;

    uint16_t rdlen = htons((uint16_t)target_len);
    memcpy(buf + rdlen_pos, &rdlen, 2);

    return pos;
}

static int append_srv_record(BYTE* buf, int max_len, const char* service, const char* target_host, uint16_t port, uint32_t ttl, bool cache_flush) {
    int pos = dns_encode_name(buf, max_len, service);
    if (pos == 0 || pos + 10 + 6 > max_len) return 0;

    uint16_t type = htons(DNS_TYPE_SRV);
    uint16_t cls = htons((uint16_t)(DNS_CLASS_IN | (cache_flush ? 0x8000 : 0)));
    uint32_t rttl = htonl(ttl);

    memcpy(buf + pos, &type, 2); pos += 2;
    memcpy(buf + pos, &cls, 2); pos += 2;
    memcpy(buf + pos, &rttl, 4); pos += 4;

    int rdlen_pos = pos;
    pos += 2; // placeholder for rdlen

    uint16_t priority = htons(0);
    uint16_t weight = htons(0);
    uint16_t rport = htons(port);

    memcpy(buf + pos, &priority, 2); pos += 2;
    memcpy(buf + pos, &weight, 2); pos += 2;
    memcpy(buf + pos, &rport, 2); pos += 2;

    int target_len = dns_encode_name(buf + pos, max_len - pos, target_host);
    if (target_len == 0) return 0;
    pos += target_len;

    uint16_t rdlen = htons((uint16_t)(6 + target_len));
    memcpy(buf + rdlen_pos, &rdlen, 2);

    return pos;
}

static int append_txt_record(BYTE* buf, int max_len, const char* service, const char** txts, int num_txts, uint32_t ttl, bool cache_flush) {
    int pos = dns_encode_name(buf, max_len, service);
    if (pos == 0 || pos + 10 > max_len) return 0;

    uint16_t type = htons(DNS_TYPE_TXT);
    uint16_t cls = htons((uint16_t)(DNS_CLASS_IN | (cache_flush ? 0x8000 : 0)));
    uint32_t rttl = htonl(ttl);

    memcpy(buf + pos, &type, 2); pos += 2;
    memcpy(buf + pos, &cls, 2); pos += 2;
    memcpy(buf + pos, &rttl, 4); pos += 4;

    int rdlen_pos = pos;
    pos += 2; // placeholder for rdlen

    int txt_total = 0;
    for (int i = 0; i < num_txts; i++) {
        int tlen = (int)strlen(txts[i]);
        if (tlen > 255 || pos + 1 + tlen > max_len) return 0;
        buf[pos++] = (BYTE)tlen;
        memcpy(buf + pos, txts[i], tlen);
        pos += tlen;
        txt_total += (1 + tlen);
    }

    uint16_t rdlen = htons((uint16_t)txt_total);
    memcpy(buf + rdlen_pos, &rdlen, 2);

    return pos;
}

static void send_mdns_announcement(DiscoveryServer* server) {
    if (!server || server->mdnsSock == INVALID_SOCKET) return;

    BYTE packet[2048];
    memset(packet, 0, sizeof(packet));

    DnsHeader* hdr = (DnsHeader*)packet;
    hdr->id = 0;
    hdr->flags = htons(0x8400); // Response + Authoritative
    hdr->qdcount = 0;
    hdr->ancount = htons(5);
    hdr->nscount = 0;
    hdr->arcount = 0;

    int pos = sizeof(DnsHeader);

    char svcInstance[256];
    snprintf(svcInstance, sizeof(svcInstance), "%s._https._tcp.local", server->label);

    const char* txtRecords[4];
    char txt1[64], txt2[64], txt3[64];
    snprintf(txt1, sizeof(txt1), "model=leo4");
    snprintf(txt2, sizeof(txt2), "sn=%s", server->certDetails->sn);
    snprintf(txt3, sizeof(txt3), "path=/");
    txtRecords[0] = txt1;
    txtRecords[1] = txt2;
    txtRecords[2] = txt3;

    // 1. PTR _services._dns-sd._udp.local -> _https._tcp.local
    pos += append_ptr_record(packet + pos, sizeof(packet) - pos, "_services._dns-sd._udp.local", "_https._tcp.local", 120, false);
    // 2. PTR _https._tcp.local -> <label>._https._tcp.local
    pos += append_ptr_record(packet + pos, sizeof(packet) - pos, "_https._tcp.local", svcInstance, 120, false);
    // 3. SRV <label>._https._tcp.local -> <hostname>:http_port
    pos += append_srv_record(packet + pos, sizeof(packet) - pos, svcInstance, server->hostname, (uint16_t)server->http_port, 120, true);
    // 4. TXT <label>._https._tcp.local
    pos += append_txt_record(packet + pos, sizeof(packet) - pos, svcInstance, txtRecords, 3, 120, true);
    // 5. A <hostname> -> LAN IP
    pos += append_a_record(packet + pos, sizeof(packet) - pos, server->hostname, server->lan_ip_n, 120, true);

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(MDNS_PORT);
    inet_pton(AF_INET, MDNS_MULTICAST_IPV4, &dest.sin_addr);

    sendto(server->mdnsSock, (const char*)packet, pos, 0, (struct sockaddr*)&dest, sizeof(dest));

    // Also send broadcast announcement for Wi-Fi routers with aggressive IGMP snooping
    struct sockaddr_in bcast;
    memset(&bcast, 0, sizeof(bcast));
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(MDNS_PORT);
    bcast.sin_addr.s_addr = INADDR_BROADCAST;
    sendto(server->mdnsSock, (const char*)packet, pos, 0, (struct sockaddr*)&bcast, sizeof(bcast));
}

static void handle_mdns_query(DiscoveryServer* server, const BYTE* buf, int len, const struct sockaddr_in* from) {
    if (len < sizeof(DnsHeader)) return;
    const DnsHeader* qhdr = (const DnsHeader*)buf;
    uint16_t qdcount = ntohs(qhdr->qdcount);
    if (qdcount == 0) return;

    int offset = sizeof(DnsHeader);
    bool shouldAnswerA = false;
    bool shouldAnswerPtrHttps = false;
    bool shouldAnswerPtrServices = false;
    bool isUnicastReq = false;

    for (int q = 0; q < qdcount; q++) {
        char qname[256];
        int next_offset = dns_decode_name(buf, len, offset, qname, sizeof(qname));
        if (next_offset < 0 || next_offset + 4 > len) return;

        uint16_t qtype = ntohs(*(uint16_t*)(buf + next_offset));
        uint16_t raw_qclass = ntohs(*(uint16_t*)(buf + next_offset + 2));

        if (raw_qclass & 0x8000) {
            isUnicastReq = true;
        }
        uint16_t qclass = raw_qclass & 0x7FFF; // strip unicast-response bit

        if (qclass == DNS_CLASS_IN || qclass == DNS_TYPE_ANY) {
            bool matches = (_stricmp(qname, server->hostname) == 0 || _stricmp(qname, server->label) == 0 ||
                strstr(qname, "leo4-") != NULL);
            if (!matches && server->certDetails) {
                for (int i = 0; i < server->certDetails->san_dns_count; i++) {
                    if (server->certDetails->san_dns_list[i][0] != '\0' &&
                        _stricmp(qname, server->certDetails->san_dns_list[i]) == 0) {
                        matches = true;
                        break;
                    }
                }
            }
            if (matches) {
                if (qtype == DNS_TYPE_A || qtype == 0x001C /* AAAA */ || qtype == DNS_TYPE_ANY) {
                    shouldAnswerA = true;
                }
            } else if (_stricmp(qname, "_https._tcp.local") == 0) {
                if (qtype == DNS_TYPE_PTR || qtype == DNS_TYPE_ANY) {
                    shouldAnswerPtrHttps = true;
                }
            } else if (_stricmp(qname, "_services._dns-sd._udp.local") == 0) {
                if (qtype == DNS_TYPE_PTR || qtype == DNS_TYPE_ANY) {
                    shouldAnswerPtrServices = true;
                }
            }
        }

        offset = next_offset + 4;
    }

    if (!shouldAnswerA && !shouldAnswerPtrHttps && !shouldAnswerPtrServices) {
        return;
    }

    BYTE resp[2048];
    memset(resp, 0, sizeof(resp));

    DnsHeader* rhdr = (DnsHeader*)resp;
    rhdr->id = qhdr->id;
    rhdr->flags = htons(0x8400); // Response + Authoritative
    rhdr->qdcount = 0;
    rhdr->ancount = 0;
    rhdr->nscount = 0;
    rhdr->arcount = 0;

    int pos = sizeof(DnsHeader);
    int ancount = 0;

    char svcInstance[256];
    snprintf(svcInstance, sizeof(svcInstance), "%s._https._tcp.local", server->label);

    if (shouldAnswerA) {
        pos += append_a_record(resp + pos, sizeof(resp) - pos, server->hostname, server->lan_ip_n, 120, true);
        ancount++;
    }

    if (shouldAnswerPtrHttps) {
        const char* txtRecords[3];
        char txt1[64], txt2[64], txt3[64];
        snprintf(txt1, sizeof(txt1), "model=leo4");
        snprintf(txt2, sizeof(txt2), "sn=%s", server->certDetails->sn);
        snprintf(txt3, sizeof(txt3), "path=/");
        txtRecords[0] = txt1; txtRecords[1] = txt2; txtRecords[2] = txt3;

        pos += append_ptr_record(resp + pos, sizeof(resp) - pos, "_https._tcp.local", svcInstance, 120, false);
        pos += append_srv_record(resp + pos, sizeof(resp) - pos, svcInstance, server->hostname, (uint16_t)server->http_port, 120, true);
        pos += append_txt_record(resp + pos, sizeof(resp) - pos, svcInstance, txtRecords, 3, 120, true);
        pos += append_a_record(resp + pos, sizeof(resp) - pos, server->hostname, server->lan_ip_n, 120, true);
        ancount += 4;
    }

    if (shouldAnswerPtrServices) {
        pos += append_ptr_record(resp + pos, sizeof(resp) - pos, "_services._dns-sd._udp.local", "_https._tcp.local", 120, false);
        ancount++;
    }

    rhdr->ancount = htons((uint16_t)ancount);

    // If query came from an ephemeral port or requested unicast response (RFC 6762 Sec 5.4), send directly to sender
    if (from && (from->sin_port != htons(MDNS_PORT) || isUnicastReq)) {
        sendto(server->mdnsSock, (const char*)resp, pos, 0, (const struct sockaddr*)from, sizeof(*from));
    }

    // Also broadcast multicast response
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(MDNS_PORT);
    inet_pton(AF_INET, MDNS_MULTICAST_IPV4, &dest.sin_addr);

    sendto(server->mdnsSock, (const char*)resp, pos, 0, (struct sockaddr*)&dest, sizeof(dest));
}

static void handle_llmnr_query(DiscoveryServer* server, const BYTE* buf, int len, const struct sockaddr_in* from) {
    if (len < sizeof(DnsHeader)) return;
    const DnsHeader* qhdr = (const DnsHeader*)buf;
    uint16_t qdcount = ntohs(qhdr->qdcount);
    if (qdcount == 0) return;

    int offset = sizeof(DnsHeader);
    bool shouldAnswer = false;
    char matchedName[256] = { 0 };

    for (int q = 0; q < qdcount; q++) {
        char qname[256];
        int next_offset = dns_decode_name(buf, len, offset, qname, sizeof(qname));
        if (next_offset < 0 || next_offset + 4 > len) return;

        uint16_t qtype = ntohs(*(uint16_t*)(buf + next_offset));
        uint16_t qclass = ntohs(*(uint16_t*)(buf + next_offset + 2));

        if ((qclass == DNS_CLASS_IN || qclass == DNS_TYPE_ANY) &&
            (qtype == DNS_TYPE_A || qtype == DNS_TYPE_ANY)) {
            if (_stricmp(qname, server->label) == 0 ||
                _stricmp(qname, server->hostname) == 0) {
                shouldAnswer = true;
                strncpy_s(matchedName, sizeof(matchedName), qname, _TRUNCATE);
                break;
            }
        }
        offset = next_offset + 4;
    }

    if (!shouldAnswer) return;

    BYTE resp[1024];
    memset(resp, 0, sizeof(resp));

    DnsHeader* rhdr = (DnsHeader*)resp;
    rhdr->id = qhdr->id;
    rhdr->flags = htons(0x8400); // Response, Authoritative
    rhdr->qdcount = htons(1);
    rhdr->ancount = htons(1);
    rhdr->nscount = 0;
    rhdr->arcount = 0;

    int pos = sizeof(DnsHeader);

    // Echo question
    pos += dns_encode_name(resp + pos, sizeof(resp) - pos, matchedName);
    uint16_t type = htons(DNS_TYPE_A);
    uint16_t cls = htons(DNS_CLASS_IN);
    memcpy(resp + pos, &type, 2); pos += 2;
    memcpy(resp + pos, &cls, 2); pos += 2;

    // Answer RR
    pos += append_a_record(resp + pos, sizeof(resp) - pos, matchedName, server->lan_ip_n, 30, false);

    sendto(server->llmnrSock, (const char*)resp, pos, 0, (const struct sockaddr*)from, sizeof(*from));
}

static unsigned __stdcall discovery_worker_thread(void* arg) {
    DiscoveryServer* server = (DiscoveryServer*)arg;
    if (!server) return 0;

    // Send initial gratuitous announcements to prime LAN
    send_mdns_announcement(server);
    Sleep(500);
    send_mdns_announcement(server);

    DWORD lastAnnounceTime = GetTickCount();

    while (server->isRunning) {
        fd_set readfds;
        FD_ZERO(&readfds);

        SOCKET maxSock = 0;
        if (server->mdnsSock != INVALID_SOCKET) {
            FD_SET(server->mdnsSock, &readfds);
            if (server->mdnsSock > maxSock) maxSock = server->mdnsSock;
        }
        if (server->llmnrSock != INVALID_SOCKET) {
            FD_SET(server->llmnrSock, &readfds);
            if (server->llmnrSock > maxSock) maxSock = server->llmnrSock;
        }

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = select((int)(maxSock + 1), &readfds, NULL, NULL, &tv);
        if (sel > 0) {
            BYTE buf[2048];
            struct sockaddr_in from;
            int fromlen = sizeof(from);

            if (server->mdnsSock != INVALID_SOCKET && FD_ISSET(server->mdnsSock, &readfds)) {
                int recvd = recvfrom(server->mdnsSock, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromlen);
                if (recvd > 0) {
                    handle_mdns_query(server, buf, recvd, &from);
                }
            }

            if (server->llmnrSock != INVALID_SOCKET && FD_ISSET(server->llmnrSock, &readfds)) {
                fromlen = sizeof(from);
                int recvd = recvfrom(server->llmnrSock, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromlen);
                if (recvd > 0) {
                    handle_llmnr_query(server, buf, recvd, &from);
                }
            }
        }

        // Periodic announcement every 10s
        DWORD now = GetTickCount();
        if (now - lastAnnounceTime > 10000) {
            lastAnnounceTime = now;
            send_mdns_announcement(server);
        }
    }

    return 0;
}

bool discovery_start(DiscoveryServer* server, const ProxyConfig* config, const CertDetails* certDetails) {
    if (!server || !config || !certDetails) return false;
    memset(server, 0, sizeof(DiscoveryServer));

    server->config = config;
    server->certDetails = certDetails;
    server->http_port = config->reverse_local_port;
    server->mdnsSock = INVALID_SOCKET;
    server->llmnrSock = INVALID_SOCKET;
    server->isRunning = true;

    // Determine canonical local hostname and label
    if (config->custom_local_domain[0] != '\0') {
        strncpy_s(server->hostname, sizeof(server->hostname), config->custom_local_domain, _TRUNCATE);
    } else if (certDetails->local_hostname[0] != '\0') {
        strncpy_s(server->hostname, sizeof(server->hostname), certDetails->local_hostname, _TRUNCATE);
    } else {
        strncpy_s(server->hostname, sizeof(server->hostname), "leo4-device.local", _TRUNCATE);
    }

    // Extract label (hostname before .local or first dot)
    strncpy_s(server->label, sizeof(server->label), server->hostname, _TRUNCATE);
    char* dot = strchr(server->label, '.');
    if (dot) *dot = '\0';

    discovery_get_lan_ip(server->lan_ip_str, sizeof(server->lan_ip_str), &server->lan_ip_n);

    // Setup mDNS socket (UDP 5353)
    server->mdnsSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server->mdnsSock != INVALID_SOCKET) {
        BOOL opt = TRUE;
        setsockopt(server->mdnsSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        setsockopt(server->mdnsSock, SOL_SOCKET, SO_BROADCAST, (const char*)&opt, sizeof(opt));

        BOOL loop = TRUE;
        setsockopt(server->mdnsSock, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof(loop));

        BYTE ttl = 255;
        setsockopt(server->mdnsSock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));

        struct sockaddr_in mdnsAddr;
        memset(&mdnsAddr, 0, sizeof(mdnsAddr));
        mdnsAddr.sin_family = AF_INET;
        mdnsAddr.sin_port = htons(MDNS_PORT);
        mdnsAddr.sin_addr.s_addr = INADDR_ANY;

        if (bind(server->mdnsSock, (struct sockaddr*)&mdnsAddr, sizeof(mdnsAddr)) == 0) {
            struct ip_mreq mreq;
            inet_pton(AF_INET, MDNS_MULTICAST_IPV4, &mreq.imr_multiaddr);
            mreq.imr_interface.s_addr = INADDR_ANY;
            setsockopt(server->mdnsSock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));

            if (server->lan_ip_n && server->lan_ip_n != INADDR_ANY) {
                mreq.imr_interface.s_addr = server->lan_ip_n;
                setsockopt(server->mdnsSock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
                setsockopt(server->mdnsSock, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&server->lan_ip_n, sizeof(server->lan_ip_n));
            }
        } else {
            closesocket(server->mdnsSock);
            server->mdnsSock = INVALID_SOCKET;
        }
    }

    // Setup LLMNR socket (UDP 5355)
    server->llmnrSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server->llmnrSock != INVALID_SOCKET) {
        BOOL opt = TRUE;
        setsockopt(server->llmnrSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        BOOL loop = TRUE;
        setsockopt(server->llmnrSock, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof(loop));

        BYTE ttl = 255;
        setsockopt(server->llmnrSock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));

        struct sockaddr_in llmnrAddr;
        memset(&llmnrAddr, 0, sizeof(llmnrAddr));
        llmnrAddr.sin_family = AF_INET;
        llmnrAddr.sin_port = htons(LLMNR_PORT);
        llmnrAddr.sin_addr.s_addr = INADDR_ANY;

        if (bind(server->llmnrSock, (struct sockaddr*)&llmnrAddr, sizeof(llmnrAddr)) == 0) {
            struct ip_mreq mreq;
            inet_pton(AF_INET, LLMNR_MULTICAST_IPV4, &mreq.imr_multiaddr);
            mreq.imr_interface.s_addr = INADDR_ANY;
            setsockopt(server->llmnrSock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));

            if (server->lan_ip_n && server->lan_ip_n != INADDR_ANY) {
                mreq.imr_interface.s_addr = server->lan_ip_n;
                setsockopt(server->llmnrSock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
                setsockopt(server->llmnrSock, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&server->lan_ip_n, sizeof(server->lan_ip_n));
            }
        } else {
            closesocket(server->llmnrSock);
            server->llmnrSock = INVALID_SOCKET;
        }
    }

    printf("[DISCOVERY] LAN Announcement active: https://%s:%d (IP: %s) [mDNS :5353, LLMNR :5355]\n",
           server->hostname, server->http_port, server->lan_ip_str);

    server->hThread = (HANDLE)_beginthreadex(NULL, 0, discovery_worker_thread, server, 0, NULL);
    if (!server->hThread) {
        discovery_stop(server);
        return false;
    }

    return true;
}

void discovery_stop(DiscoveryServer* server) {
    if (!server) return;
    server->isRunning = false;

    if (server->mdnsSock != INVALID_SOCKET) {
        closesocket(server->mdnsSock);
        server->mdnsSock = INVALID_SOCKET;
    }

    if (server->llmnrSock != INVALID_SOCKET) {
        closesocket(server->llmnrSock);
        server->llmnrSock = INVALID_SOCKET;
    }

    if (server->hThread) {
        WaitForSingleObject(server->hThread, 2000);
        CloseHandle(server->hThread);
        server->hThread = NULL;
    }
}
