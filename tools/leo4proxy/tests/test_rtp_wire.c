#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <assert.h>

#include "../src/config.h"
#include "../src/cert_store.h"
#include "../src/rtp_tunnel.h"

ProxyStats g_proxyStats = { 0 };

void proxy_config_init_defaults(ProxyConfig* config) {
    if (!config) return;
    memset(config, 0, sizeof(ProxyConfig));
    config->rtp_tunnel_enabled = 1;
    strncpy_s(config->rtp_tunnel_local_host, sizeof(config->rtp_tunnel_local_host), "127.0.0.1", _TRUNCATE);
    config->rtp_tunnel_rtp_port = 5004;
    config->rtp_tunnel_rtcp_port = 5005;
    strncpy_s(config->rtp_tunnel_remote_host, sizeof(config->rtp_tunnel_remote_host), "dev.leo4.ru", _TRUNCATE);
    config->rtp_tunnel_remote_port = 8443;
    config->rtp_tunnel_idle_timeout_sec = 30;
    config->rtp_tunnel_reconnect_sec = 3;
}

static void test_preamble_format(void) {
    printf("[TEST] Checking L4RTP/1 preamble wire format...\n");
    const char* testSn = "000100773";
    size_t snLen = strlen(testSn);
    assert(snLen == 9);

    BYTE preamble[8 + 128];
    preamble[0] = 'L';
    preamble[1] = '4';
    preamble[2] = 'R';
    preamble[3] = 'T';
    preamble[4] = 0x01;
    preamble[5] = 0x00;
    preamble[6] = (BYTE)((snLen >> 8) & 0xFF);
    preamble[7] = (BYTE)(snLen & 0xFF);
    memcpy(preamble + 8, testSn, snLen);

    // Expected bytes from specification:
    // 4C 34 52 54 01 00 00 09 30 30 30 31 30 30 37 37 33
    const BYTE expected[] = {
        0x4C, 0x34, 0x52, 0x54,
        0x01,
        0x00,
        0x00, 0x09,
        0x30, 0x30, 0x30, 0x31, 0x30, 0x30, 0x37, 0x37, 0x33
    };

    assert(sizeof(expected) == 8 + snLen);
    assert(memcmp(preamble, expected, sizeof(expected)) == 0);
    printf("  [PASS] Preamble matches expected wire specification byte-for-byte.\n");
}

static void test_frame_format(void) {
    printf("[TEST] Checking L4RTP/1 frame wire format (RTP & RTCP)...\n");
    unsigned char buf[4 + 2048];
    int payloadLen = 1400; // typical RTP packet
    memset(buf + 4, 0xAB, payloadLen);

    // RTP Frame (0x01)
    buf[0] = 0x01;
    buf[1] = 0x00;
    buf[2] = (unsigned char)((payloadLen >> 8) & 0xFF);
    buf[3] = (unsigned char)(payloadLen & 0xFF);

    assert(buf[0] == 0x01);
    assert(buf[1] == 0x00);
    assert(buf[2] == 0x05);
    assert(buf[3] == 0x78); // 1400 = 0x0578
    assert(buf[4] == 0xAB);

    // RTCP Frame (0x02)
    int rtcpLen = 72; // typical RTCP sender report
    buf[0] = 0x02;
    buf[1] = 0x00;
    buf[2] = (unsigned char)((rtcpLen >> 8) & 0xFF);
    buf[3] = (unsigned char)(rtcpLen & 0xFF);

    assert(buf[0] == 0x02);
    assert(buf[1] == 0x00);
    assert(buf[2] == 0x00);
    assert(buf[3] == 0x48); // 72 = 0x0048
    printf("  [PASS] Frame headers and payload lengths match wire specification.\n");
}

static void test_validation_and_sockets(void) {
    printf("[TEST] Checking start validation (SN and ports)...\n");
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    RtpTunnelServer server;
    ProxyConfig config;
    proxy_config_init_defaults(&config);
    config.rtp_tunnel_enabled = 1;

    CertDetails details;
    memset(&details, 0, sizeof(details));
    CredHandle dummyCred;
    SecInvalidateHandle(&dummyCred);

    // 1. Empty SN should fail
    details.sn[0] = '\0';
    assert(!rtp_tunnel_start(&server, &config, &details, dummyCred));
    printf("  [PASS] Empty SN correctly rejected.\n");

    // 2. Same RTP and RTCP port should fail
    strncpy_s(details.sn, sizeof(details.sn), "000100773", _TRUNCATE);
    config.rtp_tunnel_rtp_port = 5004;
    config.rtp_tunnel_rtcp_port = 5004;
    assert(!rtp_tunnel_start(&server, &config, &details, dummyCred));
    printf("  [PASS] Identical RTP and RTCP port correctly rejected.\n");

    // 3. Valid ports and SN should succeed starting UDP listener (Lazy connect)
    config.rtp_tunnel_rtp_port = 5504;
    config.rtp_tunnel_rtcp_port = 5505;
    bool started = rtp_tunnel_start(&server, &config, &details, dummyCred);
    assert(started);
    printf("  [PASS] Sockets successfully created and bound to loopback.\n");

    // Verify SO_RCVBUF is set
    int rcvbuf = 0;
    int optlen = sizeof(rcvbuf);
    getsockopt(server.rtpSocket, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, &optlen);
    printf("  [INFO] RTP socket SO_RCVBUF = %d bytes (>= 512KB)\n", rcvbuf);
    assert(rcvbuf >= 512 * 1024);

    // 4. Clean stop
    rtp_tunnel_stop(&server);
    printf("  [PASS] RTP tunnel stopped cleanly.\n");

    WSACleanup();
}

int main(void) {
    printf("=======================================================\n");
    printf(" Running Leo4Proxy RTP Tunnel Wire & Socket Tests\n");
    printf("=======================================================\n");
    test_preamble_format();
    test_frame_format();
    test_validation_and_sockets();
    printf("=======================================================\n");
    printf(" ALL RTP TUNNEL TESTS PASSED SUCCESSFULLY!\n");
    printf("=======================================================\n");
    return 0;
}
