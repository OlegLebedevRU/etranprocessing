#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "http_client.h"
#include "url_finder.h"

static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)

typedef struct { SOCKET listener; const char* response; DWORD delay; } Server;
static DWORD WINAPI serve(void* argument) {
    Server* server = argument;
    SOCKET client = accept(server->listener, NULL, NULL);
    if (client != INVALID_SOCKET) {
        char input[4096];
        recv(client, input, sizeof(input), 0);
        if (server->delay) Sleep(server->delay);
        if (server->response) send(client, server->response, (int)strlen(server->response), 0);
        shutdown(client, SD_BOTH);
        closesocket(client);
    }
    return 0;
}

static void request_case(const char* response, DWORD delay, bool expected, bool enrollment) {
    Server server = { socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), response, delay };
    CHECK(server.listener != INVALID_SOCKET);
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(server.listener, (const struct sockaddr*)&addr, sizeof(addr)) == 0);
    CHECK(listen(server.listener, 1) == 0);
    int size = sizeof(addr);
    CHECK(getsockname(server.listener, (struct sockaddr*)&addr, &size) == 0);
    char url[128];
    sprintf_s(url, sizeof(url), "http://127.0.0.1:%u/api/certificates", ntohs(addr.sin_port));
    HANDLE worker = CreateThread(NULL, 0, serve, &server, 0, NULL);
    CHECK(worker != NULL);
    char* body = NULL;
    size_t length = 0;
    ULONGLONG started = GetTickCount64();
    bool ok = enrollment ? http_check(url, "TEST00", "test", "26", &body, &length)
                         : http_get_simple(url, 200, &body, &length);
    CHECK(ok == expected);
    if (expected) { CHECK(length == 2); CHECK(body && !strcmp(body, "ok")); }
    else CHECK(body == NULL);
    if (delay) CHECK(GetTickCount64() - started < 1500);
    free(body);
    CHECK(WaitForSingleObject(worker, 3000) == WAIT_OBJECT_0);
    CloseHandle(worker);
    closesocket(server.listener);
}

static void info_case(const char* info, bool expected) {
    char out[256];
    CHECK(certificates_url_from_info(info, strlen(info), out, sizeof(out)) == expected);
    if (expected) CHECK(!strcmp(out, "http://127.0.0.1:18443/api/certificates"));
}

int main(void) {
    WSADATA data;
    CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    info_case("{\"status\":\"ready\",\"listeners\":{\"http_local\":\"127.0.0.1:18443\"}}", true);
    info_case("{\"status\":\"ready\",\"listeners\":{\"http_local\":\"[::]:18443\"}}", true);
    info_case("{\"status\":\"waiting_for_certificate\",\"listeners\":{\"http_local\":\"127.0.0.1:18443\"}}", false);
    info_case("{\"status\":\"ready\",\"http_local\":\"127.0.0.1:18443\"}", false);
    info_case("{\"status\":\"ready\",\"listeners\":{\"http_local\":\"example.org:18443\"}}", false);
    info_case("{\"status\":\"ready\",\"listeners\":{\"http_local\":\"127.0.0.1:65536\"}}", false);
    info_case("{\"status\":\"ready\",\"status\":\"ready\",\"listeners\":{\"http_local\":\"127.0.0.1:18443\"}}", false);
    info_case("{\"status\":\"ready\",\"listeners\":{\"http_local\":\"127.0.0.1:18443\"}", false);
    request_case("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok", 0, true, false);
    request_case("HTTP/1.1 200 OK\r\nContent-Length: 50\r\nConnection: close\r\n\r\nshort", 0, false, false);
    request_case("HTTP/1.1 500 Error\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok", 0, false, true);
    request_case(NULL, 700, false, false);
    WSACleanup();
    printf("HTTP/discovery failures: %d\n", failures);
    return failures ? 1 : 0;
}
