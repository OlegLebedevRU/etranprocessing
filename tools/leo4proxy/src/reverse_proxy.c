/**
 * @file reverse_proxy.c
 * @brief Reverse HTTPS Proxy and TLS Termination for Leo4Proxy.
 */

#include "reverse_proxy.h"
#include <ws2tcpip.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SOCKET clientSock;
    struct sockaddr_storage clientAddr;
    const ProxyConfig* config;
    const CertDetails* certDetails;
    CredHandle hServerCred;
} ReverseClientWorkerArgs;

typedef struct {
    SOCKET clientSock;
    const CertDetails* certDetails;
} HttpRedirectWorkerArgs;

static int send_all_socket(SOCKET s, const BYTE* data, int len) {
    int total = 0;
    while (total < len) {
        int sent = send(s, (const char*)(data + total), len - total, 0);
        if (sent <= 0) return sent;
        total += sent;
    }
    return total;
}

static bool get_http_header(const char* req, const char* name, char* outVal, size_t outSize) {
    if (!req || !name || !outVal || outSize == 0) return false;
    outVal[0] = '\0';
    size_t nameLen = strlen(name);
    const char* p = req;

    // Skip request line
    const char* lineEnd = strstr(p, "\r\n");
    if (!lineEnd) return false;
    p = lineEnd + 2;

    while (*p && *p != '\r' && *p != '\n') {
        const char* nextLine = strstr(p, "\r\n");
        if (!nextLine) break;

        if (_strnicmp(p, name, nameLen) == 0 && p[nameLen] == ':') {
            const char* valStart = p + nameLen + 1;
            while (*valStart == ' ' || *valStart == '\t') valStart++;
            size_t valLen = (size_t)(nextLine - valStart);
            if (valLen >= outSize) valLen = outSize - 1;
            memcpy(outVal, valStart, valLen);
            outVal[valLen] = '\0';
            return true;
        }
        p = nextLine + 2;
    }
    return false;
}

static void send_http_error(SChannelSession* session, int statusCode, const char* statusText, const char* body, const char* sn, bool keep_alive) {
    char responseBuf[4096];
    int bodyLen = body ? (int)strlen(body) : 0;

    int totalLen = snprintf(responseBuf, sizeof(responseBuf),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, PATCH, OPTIONS\r\n"
        "X-Leo4-Proxy-Sn: %s\r\n"
        "Connection: %s\r\n"
        "%s"
        "\r\n"
        "%s",
        statusCode, statusText, bodyLen, sn ? sn : "",
        keep_alive ? "keep-alive" : "close",
        keep_alive ? "Keep-Alive: timeout=15, max=100\r\n" : "",
        body ? body : "");

    schannel_send(session, responseBuf, totalLen);
}

static void send_redirect(SChannelSession* session, const char* location, bool keep_alive) {
    char responseBuf[1024];
    int totalLen = snprintf(responseBuf, sizeof(responseBuf),
        "HTTP/1.1 302 Found\r\n"
        "Location: %s\r\n"
        "Content-Length: 0\r\n"
        "Connection: %s\r\n"
        "%s"
        "\r\n",
        location ? location : "/",
        keep_alive ? "keep-alive" : "close",
        keep_alive ? "Keep-Alive: timeout=15, max=100\r\n" : "");

    schannel_send(session, responseBuf, totalLen);
}

static void handle_info_request(SChannelSession* session, const ProxyConfig* config, const CertDetails* certDetails, bool wants_html, bool keep_alive, bool is_local) {
    bool cert_ready = (certDetails != NULL && certDetails->sn[0] != '\0' && g_proxyStats.cert_ready);
    bool backend_online = tcp_probe_connect(config->reverse_target_host, config->reverse_target_port, 250);

    char jsonBuf[4096];
    int jsonLen = 0;

    if (is_local) {
        if (cert_ready) {
            jsonLen = snprintf(jsonBuf, sizeof(jsonBuf),
                "{\n"
                "  \"status\": \"ready\",\n"
                "  \"certificate_found\": true,\n"
                "  \"version\": \"%s\",\n"
                "  \"sn\": \"%s\",\n"
                "  \"client_id\": \"%s\",\n"
                "  \"urn\": \"%s\",\n"
                "  \"email\": \"%s\",\n"
                "  \"subject\": \"%s\",\n"
                "  \"issuer\": \"%s\",\n"
                "  \"serial\": \"%s\",\n"
                "  \"thumbprint\": \"%s\",\n"
                "  \"not_before\": \"%s\",\n"
                "  \"not_after\": \"%s\",\n"
                "  \"has_private_key\": %s,\n"
                "  \"local_hostname\": \"%s\",\n"
                "  \"san_dns\": \"%s\",\n"
                "  \"listeners\": {\n"
                "    \"mqtt_local\": \"%s:%d\",\n"
                "    \"http_local\": \"%s:%d\",\n"
                "    \"reverse_listen\": \"%s:%d\"\n"
                "  },\n"
                "  \"upstreams\": {\n"
                "    \"mqtt_remote\": \"%s:%d\",\n"
                "    \"http_remote\": \"https://%s:%d\",\n"
                "    \"reverse_target\": \"http://%s:%d\"\n"
                "  },\n"
                "  \"routes_active\": true,\n"
                "  \"clients\": {\n"
                "    \"mqtt_active_clients\": %ld,\n"
                "    \"mqtt_total_connections\": %ld,\n"
                "    \"http_total_requests\": %ld,\n"
                "    \"reverse_total_requests\": %ld\n"
                "  },\n"
                "  \"backend_service\": {\n"
                "    \"online\": %s,\n"
                "    \"target\": \"%s:%d\"\n"
                "  }\n"
                "}",
                LEO4_PROXY_VERSION,
                certDetails->sn,
                certDetails->sn,
                certDetails->urn,
                certDetails->email,
                certDetails->subject,
                certDetails->issuer,
                certDetails->serial,
                certDetails->thumbprint,
                certDetails->not_before,
                certDetails->not_after,
                certDetails->has_private_key ? "true" : "false",
                certDetails->local_hostname,
                certDetails->san_dns,
                config->mqtt_local_host, config->mqtt_local_port,
                config->http_local_host, config->http_local_port,
                config->reverse_local_host, config->reverse_local_port,
                config->mqtt_remote_host, config->mqtt_remote_port,
                config->http_remote_host, config->http_remote_port,
                config->reverse_target_host, config->reverse_target_port,
                g_proxyStats.mqtt_active_clients,
                g_proxyStats.mqtt_total_connections,
                g_proxyStats.http_total_requests,
                g_proxyStats.reverse_total_requests,
                backend_online ? "true" : "false",
                config->reverse_target_host, config->reverse_target_port
            );
        } else {
            jsonLen = snprintf(jsonBuf, sizeof(jsonBuf),
                "{\n"
                "  \"status\": \"waiting_for_certificate\",\n"
                "  \"certificate_found\": false,\n"
                "  \"version\": \"%s\",\n"
                "  \"sn\": \"\",\n"
                "  \"client_id\": \"\",\n"
                "  \"local_hostname\": \"leo4-device.local\",\n"
                "  \"san_dns\": \"\",\n"
                "  \"listeners\": {\n"
                "    \"mqtt_local\": \"%s:%d (disabled)\",\n"
                "    \"http_local\": \"%s:%d\",\n"
                "    \"reverse_listen\": \"%s:%d (disabled)\"\n"
                "  },\n"
                "  \"upstreams\": {\n"
                "    \"mqtt_remote\": \"%s:%d (disabled)\",\n"
                "    \"http_remote\": \"https://%s:%d (disabled)\",\n"
                "    \"reverse_target\": \"http://%s:%d (disabled)\"\n"
                "  },\n"
                "  \"routes_active\": false,\n"
                "  \"clients\": {\n"
                "    \"mqtt_active_clients\": %ld,\n"
                "    \"mqtt_total_connections\": %ld,\n"
                "    \"http_total_requests\": %ld,\n"
                "    \"reverse_total_requests\": %ld\n"
                "  },\n"
                "  \"backend_service\": {\n"
                "    \"online\": %s,\n"
                "    \"target\": \"%s:%d\"\n"
                "  }\n"
                "}",
                LEO4_PROXY_VERSION,
                config->mqtt_local_host, config->mqtt_local_port,
                config->http_local_host, config->http_local_port,
                config->reverse_local_host, config->reverse_local_port,
                config->mqtt_remote_host, config->mqtt_remote_port,
                config->http_remote_host, config->http_remote_port,
                config->reverse_target_host, config->reverse_target_port,
                g_proxyStats.mqtt_active_clients,
                g_proxyStats.mqtt_total_connections,
                g_proxyStats.http_total_requests,
                g_proxyStats.reverse_total_requests,
                backend_online ? "true" : "false",
                config->reverse_target_host, config->reverse_target_port
            );
        }
    } else {
        if (cert_ready) {
            jsonLen = snprintf(jsonBuf, sizeof(jsonBuf),
                "{\n"
                "  \"status\": \"ready\",\n"
                "  \"sn\": \"%s\",\n"
                "  \"dns\": \"%s\",\n"
                "  \"local_hostname\": \"%s\",\n"
                "  \"backend_online\": %s,\n"
                "  \"internal_clients\": {\n"
                "    \"mqtt_connected\": %ld,\n"
                "    \"http_active\": true,\n"
                "    \"requests_count\": %ld\n"
                "  }\n"
                "}",
                certDetails->sn,
                certDetails->local_hostname,
                certDetails->local_hostname,
                backend_online ? "true" : "false",
                g_proxyStats.mqtt_active_clients,
                g_proxyStats.http_total_requests + g_proxyStats.reverse_total_requests
            );
        } else {
            jsonLen = snprintf(jsonBuf, sizeof(jsonBuf),
                "{\n"
                "  \"status\": \"waiting_for_certificate\",\n"
                "  \"sn\": null,\n"
                "  \"dns\": \"leo4-device.local\",\n"
                "  \"local_hostname\": \"leo4-device.local\",\n"
                "  \"backend_online\": %s,\n"
                "  \"internal_clients\": {\n"
                "    \"mqtt_connected\": 0,\n"
                "    \"http_active\": false,\n"
                "    \"requests_count\": %ld\n"
                "  }\n"
                "}",
                backend_online ? "true" : "false",
                g_proxyStats.http_total_requests + g_proxyStats.reverse_total_requests
            );
        }
    }

    char responseBuf[16384];
    int totalLen = 0;

    if (wants_html) {
        char htmlBody[12288];
        int htmlBodyLen = 0;
        const char* active_sn = cert_ready ? certDetails->sn : "WAITING";
        const char* active_host = cert_ready ? certDetails->local_hostname : "leo4-device.local";

        if (is_local && cert_ready) {
            htmlBodyLen = snprintf(htmlBody, sizeof(htmlBody),
                "<!DOCTYPE html>\n"
                "<html lang=\"ru\">\n"
                "<head>\n"
                "<meta charset=\"utf-8\">\n"
                "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                "<title>Leo4 Terminal - %s</title>\n"
                "<style>\n"
                "  :root { --bg: #0f172a; --card: #1e293b; --border: #334155; --text: #f8fafc; --muted: #94a3b8; --accent: #38bdf8; --success: #4ade80; }\n"
                "  body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 24px; }\n"
                "  .container { max-width: 800px; margin: 0 auto; }\n"
                "  .header { display: flex; align-items: center; justify-content: space-between; border-bottom: 1px solid var(--border); padding-bottom: 16px; margin-bottom: 24px; }\n"
                "  .badge { background: #064e3b; color: var(--success); padding: 4px 12px; border-radius: 9999px; font-weight: 600; font-size: 14px; }\n"
                "  .card { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 20px; margin-bottom: 20px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.1); }\n"
                "  .sn-box { display: flex; align-items: center; justify-content: space-between; background: #0f172a; padding: 12px 16px; border-radius: 8px; border: 1px solid var(--border); font-family: monospace; font-size: 18px; color: var(--accent); margin-top: 8px; }\n"
                "  .btn { background: var(--accent); color: #0f172a; border: none; padding: 8px 16px; border-radius: 6px; font-weight: 600; cursor: pointer; transition: opacity 0.2s; }\n"
                "  .btn:hover { opacity: 0.9; }\n"
                "  .grid { display: grid; grid-template-columns: 140px 1fr; gap: 10px; font-size: 14px; }\n"
                "  .label { color: var(--muted); }\n"
                "  .value { font-family: monospace; word-break: break-all; }\n"
                "  pre { background: #0f172a; padding: 16px; border-radius: 8px; border: 1px solid var(--border); overflow-x: auto; color: #38bdf8; font-size: 13px; }\n"
                "  .links { display: flex; gap: 16px; margin-top: 16px; }\n"
                "  .link { color: var(--accent); text-decoration: none; font-size: 14px; font-weight: 500; }\n"
                "  .link:hover { text-decoration: underline; }\n"
                "</style>\n"
                "</head>\n"
                "<body>\n"
                "<div class=\"container\">\n"
                "  <div class=\"header\">\n"
                "    <div>\n"
                "      <h1 style=\"margin: 0; font-size: 24px;\">Leo4 Terminal Diagnostic Info (Local)</h1>\n"
                "      <div style=\"color: var(--muted); font-size: 14px; margin-top: 4px;\">Host: %s | Reverse HTTPS Proxy</div>\n"
                "    </div>\n"
                "    <div class=\"badge\">Online & TLS OK</div>\n"
                "  </div>\n"
                "  <div class=\"card\">\n"
                "    <div style=\"font-size: 14px; color: var(--muted);\">Serial Number (SN / Client ID)</div>\n"
                "    <div class=\"sn-box\">\n"
                "      <span id=\"snText\">%s</span>\n"
                "      <button class=\"btn\" onclick=\"navigator.clipboard.writeText(document.getElementById('snText').innerText); this.innerText='Скопировано!'; setTimeout(()=>this.innerText='Копировать', 1500)\">Копировать</button>\n"
                "    </div>\n"
                "  </div>\n"
                "  <div class=\"card\">\n"
                "    <h3 style=\"margin-top: 0; margin-bottom: 16px; font-size: 16px; border-bottom: 1px solid var(--border); padding-bottom: 8px;\">Параметры сертификата и маршрутизации</h3>\n"
                "    <div class=\"grid\">\n"
                "      <div class=\"label\">Local Domain:</div><div class=\"value\" style=\"color: var(--accent); font-weight: 600;\">%s</div>\n"
                "      <div class=\"label\">SAN DNS:</div><div class=\"value\">%s</div>\n"
                "      <div class=\"label\">Client Email:</div><div class=\"value\">%s</div>\n"
                "      <div class=\"label\">Issuer CA:</div><div class=\"value\">%s</div>\n"
                "      <div class=\"label\">Serial:</div><div class=\"value\">%s</div>\n"
                "      <div class=\"label\">Thumbprint:</div><div class=\"value\">%s</div>\n"
                "      <div class=\"label\">Valid:</div><div class=\"value\">%s &mdash; %s</div>\n"
                "      <div class=\"label\">Private Key:</div><div class=\"value\" style=\"color: var(--success); font-weight: 600;\">%s</div>\n"
                "      <div class=\"label\">Reverse Target:</div><div class=\"value\">http://%s:%d (%s)</div>\n"
                "      <div class=\"label\">Proxy Version:</div><div class=\"value\">%s</div>\n"
                "    </div>\n"
                "    <div class=\"links\">\n"
                "      <a class=\"link\" href=\"/_leo4/sn\">/_leo4/sn (Plain SN)</a>\n"
                "      <a class=\"link\" href=\"/_leo4/info?format=json\">/_leo4/info?format=json (JSON API)</a>\n"
                "      <a class=\"link\" href=\"/\">/ (Главная страница сервиса)</a>\n"
                "    </div>\n"
                "  </div>\n"
                "  <div class=\"card\">\n"
                "    <h3 style=\"margin-top: 0; margin-bottom: 12px; font-size: 14px; color: var(--muted);\">Raw JSON Response</h3>\n"
                "    <pre>%s</pre>\n"
                "  </div>\n"
                "</div>\n"
                "</body>\n"
                "</html>\n",
                certDetails->sn,
                certDetails->local_hostname,
                certDetails->sn,
                certDetails->local_hostname,
                certDetails->san_dns,
                certDetails->email,
                certDetails->issuer,
                certDetails->serial,
                certDetails->thumbprint,
                certDetails->not_before,
                certDetails->not_after,
                certDetails->has_private_key ? "YES (CNG KSP Hardware / Protected)" : "NO",
                config->reverse_target_host, config->reverse_target_port,
                backend_online ? "online" : "offline",
                LEO4_PROXY_VERSION,
                jsonBuf
            );
        } else {
            htmlBodyLen = snprintf(htmlBody, sizeof(htmlBody),
                "<!DOCTYPE html>\n"
                "<html lang=\"ru\">\n"
                "<head>\n"
                "<meta charset=\"utf-8\">\n"
                "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                "<title>Leo4 Terminal - %s</title>\n"
                "<style>\n"
                "  :root { --bg: #0f172a; --card: #1e293b; --border: #334155; --text: #f8fafc; --muted: #94a3b8; --accent: #38bdf8; --success: #4ade80; }\n"
                "  body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 24px; }\n"
                "  .container { max-width: 800px; margin: 0 auto; }\n"
                "  .header { display: flex; align-items: center; justify-content: space-between; border-bottom: 1px solid var(--border); padding-bottom: 16px; margin-bottom: 24px; }\n"
                "  .badge { background: #064e3b; color: var(--success); padding: 4px 12px; border-radius: 9999px; font-weight: 600; font-size: 14px; }\n"
                "  .card { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 20px; margin-bottom: 20px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.1); }\n"
                "  .sn-box { display: flex; align-items: center; justify-content: space-between; background: #0f172a; padding: 12px 16px; border-radius: 8px; border: 1px solid var(--border); font-family: monospace; font-size: 18px; color: var(--accent); margin-top: 8px; }\n"
                "  .grid { display: grid; grid-template-columns: 140px 1fr; gap: 10px; font-size: 14px; }\n"
                "  .label { color: var(--muted); }\n"
                "  .value { font-family: monospace; word-break: break-all; }\n"
                "  pre { background: #0f172a; padding: 16px; border-radius: 8px; border: 1px solid var(--border); overflow-x: auto; color: #38bdf8; font-size: 13px; }\n"
                "  .links { display: flex; gap: 16px; margin-top: 16px; }\n"
                "  .link { color: var(--accent); text-decoration: none; font-size: 14px; font-weight: 500; }\n"
                "  .link:hover { text-decoration: underline; }\n"
                "</style>\n"
                "</head>\n"
                "<body>\n"
                "<div class=\"container\">\n"
                "  <div class=\"header\">\n"
                "    <div>\n"
                "      <h1 style=\"margin: 0; font-size: 24px;\">Leo4 Terminal Diagnostic Info</h1>\n"
                "      <div style=\"color: var(--muted); font-size: 14px; margin-top: 4px;\">Host: %s</div>\n"
                "    </div>\n"
                "    <div class=\"badge\">%s</div>\n"
                "  </div>\n"
                "  <div class=\"card\">\n"
                "    <div style=\"font-size: 14px; color: var(--muted);\">Serial Number (SN / Client ID)</div>\n"
                "    <div class=\"sn-box\">\n"
                "      <span>%s</span>\n"
                "    </div>\n"
                "  </div>\n"
                "  <div class=\"card\">\n"
                "    <h3 style=\"margin-top: 0; margin-bottom: 16px; font-size: 16px; border-bottom: 1px solid var(--border); padding-bottom: 8px;\">Состояние устройства</h3>\n"
                "    <div class=\"grid\">\n"
                "      <div class=\"label\">Local Domain:</div><div class=\"value\" style=\"color: var(--accent); font-weight: 600;\">%s</div>\n"
                "      <div class=\"label\">Backend Service:</div><div class=\"value\" style=\"color: %s;\">%s</div>\n"
                "      <div class=\"label\">MQTT Clients:</div><div class=\"value\">%ld connected</div>\n"
                "      <div class=\"label\">HTTP Requests:</div><div class=\"value\">%ld total</div>\n"
                "    </div>\n"
                "  </div>\n"
                "  <div class=\"card\">\n"
                "    <h3 style=\"margin-top: 0; margin-bottom: 12px; font-size: 14px; color: var(--muted);\">JSON Output</h3>\n"
                "    <pre>%s</pre>\n"
                "  </div>\n"
                "</div>\n"
                "</body>\n"
                "</html>\n",
                active_sn,
                active_host,
                cert_ready ? "Online & TLS OK" : "Waiting for Certificate",
                active_sn,
                active_host,
                backend_online ? "var(--success)" : "var(--muted)",
                backend_online ? "online" : "offline",
                g_proxyStats.mqtt_active_clients,
                g_proxyStats.http_total_requests + g_proxyStats.reverse_total_requests,
                jsonBuf
            );
        }

        totalLen = snprintf(responseBuf, sizeof(responseBuf),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %d\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Headers: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
            "X-Leo4-Proxy-Sn: %s\r\n"
            "Connection: %s\r\n"
            "%s"
            "\r\n"
            "%s",
            htmlBodyLen, active_sn,
            keep_alive ? "keep-alive" : "close",
            keep_alive ? "Keep-Alive: timeout=15, max=100\r\n" : "",
            htmlBody);
    } else {
        const char* active_sn = cert_ready ? certDetails->sn : "";
        totalLen = snprintf(responseBuf, sizeof(responseBuf),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: %d\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Headers: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
            "X-Leo4-Proxy-Sn: %s\r\n"
            "Connection: %s\r\n"
            "%s"
            "\r\n"
            "%s",
            jsonLen, active_sn,
            keep_alive ? "keep-alive" : "close",
            keep_alive ? "Keep-Alive: timeout=15, max=100\r\n" : "",
            jsonBuf);
    }

    schannel_send(session, responseBuf, totalLen);
}

static void handle_sn_request(SChannelSession* session, const CertDetails* certDetails, bool keep_alive) {
    char responseBuf[1024];
    int snLen = (int)strlen(certDetails->sn);

    int totalLen = snprintf(responseBuf, sizeof(responseBuf),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "X-Leo4-Proxy-Sn: %s\r\n"
        "Connection: %s\r\n"
        "%s"
        "\r\n"
        "%s",
        snLen, certDetails->sn,
        keep_alive ? "keep-alive" : "close",
        keep_alive ? "Keep-Alive: timeout=15, max=100\r\n" : "",
        certDetails->sn);

    schannel_send(session, responseBuf, totalLen);
}

static unsigned __stdcall reverse_client_worker(void* arg) {
    ReverseClientWorkerArgs* args = (ReverseClientWorkerArgs*)arg;
    if (!args) return 0;

    SOCKET clientSock = args->clientSock;
    const ProxyConfig* config = args->config;
    const CertDetails* certDetails = args->certDetails;
    CredHandle hServerCred = args->hServerCred;

    char clientIp[64] = { 0 };
    if (args->clientAddr.ss_family == AF_INET6) {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)&args->clientAddr;
        inet_ntop(AF_INET6, &addr6->sin6_addr, clientIp, sizeof(clientIp));
    } else {
        struct sockaddr_in* addr4 = (struct sockaddr_in*)&args->clientAddr;
        inet_ntop(AF_INET, &addr4->sin_addr, clientIp, sizeof(clientIp));
    }
    printf("[REVERSE-PROXY] Inbound connection from %s\n", clientIp);
    bool is_local = is_loopback_sockaddr((struct sockaddr*)&args->clientAddr);
    free(args);

    InterlockedIncrement(&g_proxyStats.reverse_total_requests);

    // 1. Perform Inbound TLS Handshake via SChannel
    SChannelSession clientTlsSession;
    if (!schannel_accept(&clientTlsSession, &hServerCred, clientSock)) {
        printf("[REVERSE-PROXY] Inbound TLS Handshake FAILED from %s\n", clientIp);
        closesocket(clientSock);
        return 0;
    }
    printf("[REVERSE-PROXY] Inbound TLS Handshake SUCCEEDED from %s\n", clientIp);

    // Set 15-second keep-alive socket timeout
    DWORD timeoutMs = 15000;
    setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));

    while (clientTlsSession.isConnected && clientTlsSession.sock != INVALID_SOCKET) {
        // 2. Read decrypted HTTP Request Headers
        char reqHeaderBuf[16384] = { 0 };
        int totalReqLen = 0;
        char* headerEnd = NULL;

        while (totalReqLen < (int)sizeof(reqHeaderBuf) - 1) {
            int n = schannel_recv(&clientTlsSession, reqHeaderBuf + totalReqLen, (int)sizeof(reqHeaderBuf) - 1 - totalReqLen);
            if (n <= 0) break;
            totalReqLen += n;
            reqHeaderBuf[totalReqLen] = '\0';

            headerEnd = strstr(reqHeaderBuf, "\r\n\r\n");
            if (headerEnd) break;
        }

        if (!headerEnd) {
            break; // Client closed connection or timeout
        }

        // Check Connection: close header
        char connHeader[64] = { 0 };
        bool client_wants_close = false;
        if (get_http_header(reqHeaderBuf, "Connection", connHeader, sizeof(connHeader))) {
            if (_stricmp(connHeader, "close") == 0) {
                client_wants_close = true;
            }
        }
        bool keep_alive = !client_wants_close;

        // 3. Inspect request line
        char method[16] = { 0 };
        char path[512] = { 0 };
        char version[16] = { 0 };
        sscanf_s(reqHeaderBuf, "%15s %511s %15s", method, (unsigned)sizeof(method),
                 path, (unsigned)sizeof(path), version, (unsigned)sizeof(version));

        if (config->verbose) {
            printf("[REVERSE-PROXY] Inbound HTTPS request from %s: %s %s\n", clientIp, method, path);
        }

        // CORS Preflight
        if (_stricmp(method, "OPTIONS") == 0) {
            char corsResp[512];
            int len = snprintf(corsResp, sizeof(corsResp),
                "HTTP/1.1 204 No Content\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, PATCH, OPTIONS, HEAD\r\n"
                "Access-Control-Allow-Headers: *\r\n"
                "Access-Control-Max-Age: 86400\r\n"
                "Content-Length: 0\r\n"
                "Connection: %s\r\n\r\n",
                keep_alive ? "keep-alive" : "close");
            schannel_send(&clientTlsSession, corsResp, len);
            if (client_wants_close) break;
            continue;
        }

        // Favicon handler
        if (_stricmp(path, "/favicon.ico") == 0) {
            char favResp[256];
            int len = snprintf(favResp, sizeof(favResp),
                "HTTP/1.1 204 No Content\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Content-Length: 0\r\n"
                "Connection: %s\r\n\r\n",
                keep_alive ? "keep-alive" : "close");
            schannel_send(&clientTlsSession, favResp, len);
            if (client_wants_close) break;
            continue;
        }

        // Check if client expects HTML (browser navigation)
        bool wants_html = false;
        char acceptHeader[512] = { 0 };
        if (get_http_header(reqHeaderBuf, "Accept", acceptHeader, sizeof(acceptHeader))) {
            if (strstr(acceptHeader, "text/html") != NULL) {
                wants_html = true;
            }
        }
        if (strstr(path, "format=json") != NULL || strstr(path, "format=raw") != NULL) {
            wants_html = false;
        }

        // Built-in device info endpoints
        if (_stricmp(method, "GET") == 0 &&
            (_stricmp(path, "/_leo4/info") == 0 || _strnicmp(path, "/_leo4/info?", 12) == 0 ||
             _stricmp(path, "/_leo4/status") == 0 || _strnicmp(path, "/_leo4/status?", 14) == 0)) {
            handle_info_request(&clientTlsSession, config, certDetails, wants_html, keep_alive, is_local);
            if (client_wants_close) break;
            continue;
        }
        if (_stricmp(method, "GET") == 0 && _stricmp(path, "/_leo4/sn") == 0) {
            handle_sn_request(&clientTlsSession, certDetails, keep_alive);
            if (client_wants_close) break;
            continue;
        }
        if (_stricmp(path, "/_leo4") == 0 || _stricmp(path, "/_leo4/") == 0) {
            send_redirect(&clientTlsSession, "/_leo4/info", keep_alive);
            if (client_wants_close) break;
            continue;
        }

        // 4. Reconstruct HTTP headers with reverse proxy headers
        int origHeaderLen = (int)(headerEnd - reqHeaderBuf) + 4;
        int bodyOffset = origHeaderLen;
        int initialBodyLen = totalReqLen - origHeaderLen;

        // Parse host header
        char hostHeader[256] = { 0 };
        get_http_header(reqHeaderBuf, "Host", hostHeader, sizeof(hostHeader));

        char forwardHeaderBuf[24576] = { 0 };
        int outPos = 0;

        // Copy request line
        char* firstLineEnd = strstr(reqHeaderBuf, "\r\n");
        if (firstLineEnd) {
            int lineLen = (int)(firstLineEnd - reqHeaderBuf) + 2;
            memcpy(forwardHeaderBuf + outPos, reqHeaderBuf, lineLen);
            outPos += lineLen;
        }

        // Append standard headers from client, skipping headers we replace
        char* lineStart = firstLineEnd ? (firstLineEnd + 2) : reqHeaderBuf;
        while (lineStart < headerEnd) {
            char* nextLine = strstr(lineStart, "\r\n");
            if (!nextLine || nextLine > headerEnd) break;

            int lineLen = (int)(nextLine - lineStart) + 2;

            if (_strnicmp(lineStart, "X-Forwarded-", 12) != 0 &&
                _strnicmp(lineStart, "X-Real-IP:", 10) != 0 &&
                _strnicmp(lineStart, "X-Leo4-Proxy-Sn:", 16) != 0) {
                if (outPos + lineLen < (int)sizeof(forwardHeaderBuf) - 512) {
                    memcpy(forwardHeaderBuf + outPos, lineStart, lineLen);
                    outPos += lineLen;
                }
            }

            lineStart = nextLine + 2;
        }

        // Inject Reverse Proxy forwarded headers
        outPos += snprintf(forwardHeaderBuf + outPos, sizeof(forwardHeaderBuf) - outPos,
            "X-Forwarded-For: %s\r\n"
            "X-Forwarded-Proto: https\r\n"
            "X-Forwarded-Host: %s\r\n"
            "X-Real-IP: %s\r\n"
            "X-Leo4-Proxy-Sn: %s\r\n"
            "\r\n",
            clientIp,
            hostHeader[0] ? hostHeader : certDetails->local_hostname,
            clientIp,
            certDetails->sn
        );

        // 5. Connect to target backend (Plain TCP)
        SOCKET targetSock = tcp_connect(config->reverse_target_host, config->reverse_target_port, 5000);
        if (targetSock == INVALID_SOCKET) {
            // If visiting root with browser and backend is offline, show helpful diagnostic landing page
            if ((strcmp(path, "/") == 0 || strcmp(path, "") == 0) && wants_html) {
                handle_info_request(&clientTlsSession, config, certDetails, true, keep_alive, is_local);
                if (client_wants_close) break;
                continue;
            } else {
                char errHtml[1024];
                snprintf(errHtml, sizeof(errHtml),
                    "<!DOCTYPE html><html><head><title>502 Bad Gateway</title></head>"
                    "<body style=\"font-family: sans-serif; background: #0f172a; color: #f8fafc; padding: 24px;\">"
                    "<h2>502 Bad Gateway</h2>"
                    "<p>Leo4Proxy Reverse HTTPS Proxy could not connect to backend <b>http://%s:%d</b>.</p>"
                    "<p><a href=\"/_leo4/info\" style=\"color: #38bdf8;\">&rarr; Open Device Status & Info (/_leo4/info)</a></p>"
                    "<hr style=\"border-color: #334155;\"><small>Leo4Proxy v%s (Device SN: %s)</small></body></html>",
                    config->reverse_target_host, config->reverse_target_port,
                    LEO4_PROXY_VERSION, certDetails->sn);

                send_http_error(&clientTlsSession, 502, "Bad Gateway", errHtml, certDetails->sn, keep_alive);
                if (client_wants_close) break;
                continue;
            }
        }

        // 6. Forward reconstructed headers and initial body to target
        if (send_all_socket(targetSock, (const BYTE*)forwardHeaderBuf, outPos) <= 0) {
            closesocket(targetSock);
            break;
        }

        if (initialBodyLen > 0) {
            if (send_all_socket(targetSock, (const BYTE*)(reqHeaderBuf + bodyOffset), initialBodyLen) <= 0) {
                closesocket(targetSock);
                break;
            }
        }

        // 7. Full Duplex Streaming loop between clientTlsSession and targetSock
        BYTE streamBuf[PROXY_BUFFER_SIZE];
        bool isAlive = true;

        while (isAlive) {
            // If clientTlsSession has buffered decrypted plaintext, drain it immediately
            if (clientTlsSession.plainBufLen > clientTlsSession.plainBufOffset) {
                int n = schannel_recv(&clientTlsSession, streamBuf, sizeof(streamBuf));
                if (n <= 0) break;
                if (send_all_socket(targetSock, streamBuf, n) <= 0) break;
                continue;
            }

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(clientSock, &readfds);
            FD_SET(targetSock, &readfds);

            SOCKET maxSock = (clientSock > targetSock) ? clientSock : targetSock;
            struct timeval tv;
            tv.tv_sec = 60;
            tv.tv_usec = 0;

            int sel = select((int)(maxSock + 1), &readfds, NULL, NULL, &tv);
            if (sel <= 0) break;

            // Target -> Client (decrypted plain HTTP -> encrypted TLS)
            if (FD_ISSET(targetSock, &readfds)) {
                int recvd = recv(targetSock, (char*)streamBuf, sizeof(streamBuf), 0);
                if (recvd <= 0) {
                    break;
                }
                int sent = schannel_send(&clientTlsSession, streamBuf, recvd);
                if (sent <= 0) {
                    break;
                }
            }

            // Client -> Target (encrypted TLS -> plain HTTP)
            if (FD_ISSET(clientSock, &readfds)) {
                int recvd = schannel_recv(&clientTlsSession, streamBuf, sizeof(streamBuf));
                if (recvd <= 0) {
                    break;
                }
                int sent = send_all_socket(targetSock, streamBuf, recvd);
                if (sent <= 0) {
                    break;
                }
            }
        }

        closesocket(targetSock);
        break; // Finish after streaming tunnel closes
    }

    schannel_close(&clientTlsSession);
    return 0;
}

static unsigned __stdcall reverse_listener_thread(void* arg) {
    ReverseProxyServer* server = (ReverseProxyServer*)arg;
    if (!server) return 0;

    while (server->isRunning) {
        fd_set readfds;
        FD_ZERO(&readfds);
        SOCKET maxSock = INVALID_SOCKET;

        if (server->listenSock != INVALID_SOCKET) {
            FD_SET(server->listenSock, &readfds);
            if (maxSock == INVALID_SOCKET || server->listenSock > maxSock) maxSock = server->listenSock;
        }
        if (server->listenSock6 != INVALID_SOCKET) {
            FD_SET(server->listenSock6, &readfds);
            if (maxSock == INVALID_SOCKET || server->listenSock6 > maxSock) maxSock = server->listenSock6;
        }

        if (maxSock == INVALID_SOCKET) break;

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = select((int)(maxSock + 1), &readfds, NULL, NULL, &tv);
        if (sel <= 0) continue;

        SOCKET activeSockets[2] = { server->listenSock, server->listenSock6 };
        for (int i = 0; i < 2; i++) {
            SOCKET lSock = activeSockets[i];
            if (lSock != INVALID_SOCKET && FD_ISSET(lSock, &readfds)) {
                struct sockaddr_storage clientAddr;
                int clientAddrLen = sizeof(clientAddr);
                SOCKET clientSock = accept(lSock, (struct sockaddr*)&clientAddr, &clientAddrLen);

                if (clientSock != INVALID_SOCKET) {
                    ReverseClientWorkerArgs* args = (ReverseClientWorkerArgs*)malloc(sizeof(ReverseClientWorkerArgs));
                    if (args) {
                        args->clientSock = clientSock;
                        args->clientAddr = clientAddr;
                        args->config = server->config;
                        args->certDetails = server->certDetails;
                        args->hServerCred = server->hServerCred;

                        HANDLE hWorker = (HANDLE)_beginthreadex(NULL, 0, reverse_client_worker, args, 0, NULL);
                        if (hWorker) {
                            CloseHandle(hWorker);
                        } else {
                            closesocket(clientSock);
                            free(args);
                        }
                    } else {
                        closesocket(clientSock);
                    }
                }
            }
        }
    }

    return 0;
}

// HTTP Port 80 -> HTTPS Port 443 Redirector
static unsigned __stdcall http_redirect_worker(void* arg) {
    HttpRedirectWorkerArgs* args = (HttpRedirectWorkerArgs*)arg;
    if (!args) return 0;

    SOCKET s = args->clientSock;
    const CertDetails* certDetails = args->certDetails;
    free(args);

    char buf[2048] = { 0 };
    int n = recv(s, buf, (int)sizeof(buf) - 1, 0);
    if (n > 0) {
        char method[16] = { 0 };
        char path[512] = { 0 };
        sscanf_s(buf, "%15s %511s", method, (unsigned)sizeof(method), path, (unsigned)sizeof(path));

        char hostHeader[256] = { 0 };
        get_http_header(buf, "Host", hostHeader, sizeof(hostHeader));

        // Strip port from host header if present
        char* colon = strchr(hostHeader, ':');
        if (colon) *colon = '\0';

        if (hostHeader[0] == '\0') {
            strncpy_s(hostHeader, sizeof(hostHeader), certDetails->local_hostname, _TRUNCATE);
        }

        char redirectBuf[1024];
        int respLen = snprintf(redirectBuf, sizeof(redirectBuf),
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Location: https://%s%s\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n",
            hostHeader, (path[0] != '\0') ? path : "/");

        send(s, redirectBuf, respLen, 0);
    }

    shutdown(s, SD_SEND);
    char drainBuf[512];
    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);
    while (recv(s, drainBuf, sizeof(drainBuf), 0) > 0) {}
    closesocket(s);
    return 0;
}

static unsigned __stdcall http_redirect_listener_thread(void* arg) {
    ReverseProxyServer* server = (ReverseProxyServer*)arg;
    if (!server) return 0;

    while (server->isRunning) {
        fd_set readfds;
        FD_ZERO(&readfds);
        SOCKET maxSock = INVALID_SOCKET;

        if (server->httpListenSock != INVALID_SOCKET) {
            FD_SET(server->httpListenSock, &readfds);
            if (maxSock == INVALID_SOCKET || server->httpListenSock > maxSock) maxSock = server->httpListenSock;
        }
        if (server->httpListenSock6 != INVALID_SOCKET) {
            FD_SET(server->httpListenSock6, &readfds);
            if (maxSock == INVALID_SOCKET || server->httpListenSock6 > maxSock) maxSock = server->httpListenSock6;
        }

        if (maxSock == INVALID_SOCKET) break;

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = select((int)(maxSock + 1), &readfds, NULL, NULL, &tv);
        if (sel <= 0) continue;

        SOCKET activeSockets[2] = { server->httpListenSock, server->httpListenSock6 };
        for (int i = 0; i < 2; i++) {
            SOCKET lSock = activeSockets[i];
            if (lSock != INVALID_SOCKET && FD_ISSET(lSock, &readfds)) {
                struct sockaddr_storage clientAddr;
                int clientAddrLen = sizeof(clientAddr);
                SOCKET clientSock = accept(lSock, (struct sockaddr*)&clientAddr, &clientAddrLen);

                if (clientSock != INVALID_SOCKET) {
                    HttpRedirectWorkerArgs* args = (HttpRedirectWorkerArgs*)malloc(sizeof(HttpRedirectWorkerArgs));
                    if (args) {
                        args->clientSock = clientSock;
                        args->certDetails = server->certDetails;

                        HANDLE hWorker = (HANDLE)_beginthreadex(NULL, 0, http_redirect_worker, args, 0, NULL);
                        if (hWorker) {
                            CloseHandle(hWorker);
                        } else {
                            closesocket(clientSock);
                            free(args);
                        }
                    } else {
                        closesocket(clientSock);
                    }
                }
            }
        }
    }

    return 0;
}

bool reverse_proxy_start(ReverseProxyServer* server, const ProxyConfig* config, const CertDetails* certDetails, CredHandle hServerCred) {
    if (!server || !config || !certDetails || !SecIsValidHandle(&hServerCred)) return false;
    memset(server, 0, sizeof(ReverseProxyServer));

    server->config = config;
    server->certDetails = certDetails;
    server->hServerCred = hServerCred;
    server->listenSock = INVALID_SOCKET;
    server->listenSock6 = INVALID_SOCKET;
    server->httpListenSock = INVALID_SOCKET;
    server->httpListenSock6 = INVALID_SOCKET;
    server->isRunning = true;

    BOOL opt = TRUE;

    // 1. Setup IPv4 HTTPS Listener (port 443)
    struct sockaddr_in addr4 = { 0 };
    addr4.sin_family = AF_INET;
    addr4.sin_port = htons((u_short)config->reverse_local_port);

    if (config->reverse_local_host[0] == '\0' || strcmp(config->reverse_local_host, "0.0.0.0") == 0) {
        addr4.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, config->reverse_local_host, &addr4.sin_addr);
    }

    server->listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->listenSock != INVALID_SOCKET) {
        setsockopt(server->listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        if (bind(server->listenSock, (struct sockaddr*)&addr4, sizeof(addr4)) == 0) {
            listen(server->listenSock, SOMAXCONN);
        } else {
            int err = WSAGetLastError();
            fprintf(stderr, "[REVERSE-PROXY] Failed to bind IPv4 HTTPS to %s:%d (error: %d)\n",
                    config->reverse_local_host, config->reverse_local_port, err);
            closesocket(server->listenSock);
            server->listenSock = INVALID_SOCKET;
        }
    }

    // 2. Setup IPv6 HTTPS Listener (port 443)
    struct sockaddr_in6 addr6 = { 0 };
    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons((u_short)config->reverse_local_port);
    addr6.sin6_addr = in6addr_any;

    server->listenSock6 = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (server->listenSock6 != INVALID_SOCKET) {
        DWORD v6only = 1;
        setsockopt(server->listenSock6, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&v6only, sizeof(v6only));
        setsockopt(server->listenSock6, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        if (bind(server->listenSock6, (struct sockaddr*)&addr6, sizeof(addr6)) == 0) {
            listen(server->listenSock6, SOMAXCONN);
        } else {
            closesocket(server->listenSock6);
            server->listenSock6 = INVALID_SOCKET;
        }
    }

    if (server->listenSock == INVALID_SOCKET && server->listenSock6 == INVALID_SOCKET) {
        fprintf(stderr, "[REVERSE-PROXY] Could not bind any HTTPS socket on port %d!\n", config->reverse_local_port);
        server->isRunning = false;
        return false;
    }

    printf("[REVERSE-PROXY] Listening on https://%s:%d (IPv4%s) -> http://%s:%d (Plain HTTP backend)\n",
           config->reverse_local_host, config->reverse_local_port,
           (server->listenSock6 != INVALID_SOCKET) ? " + IPv6" : "",
           config->reverse_target_host, config->reverse_target_port);

    server->hThread = (HANDLE)_beginthreadex(NULL, 0, reverse_listener_thread, server, 0, NULL);
    if (!server->hThread) {
        fprintf(stderr, "[REVERSE-PROXY] Failed to start listener thread: %lu\n", GetLastError());
        reverse_proxy_stop(server);
        return false;
    }

    // 3. Setup HTTP Redirect Listeners (IPv4 & IPv6 port 80)
    struct sockaddr_in httpAddr4 = { 0 };
    httpAddr4.sin_family = AF_INET;
    httpAddr4.sin_port = htons(80);
    httpAddr4.sin_addr.s_addr = INADDR_ANY;

    server->httpListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->httpListenSock != INVALID_SOCKET) {
        setsockopt(server->httpListenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        if (bind(server->httpListenSock, (struct sockaddr*)&httpAddr4, sizeof(httpAddr4)) == 0 &&
            listen(server->httpListenSock, SOMAXCONN) == 0) {
            // IPv4 port 80 ready
        } else {
            closesocket(server->httpListenSock);
            server->httpListenSock = INVALID_SOCKET;
        }
    }

    struct sockaddr_in6 httpAddr6 = { 0 };
    httpAddr6.sin6_family = AF_INET6;
    httpAddr6.sin6_port = htons(80);
    httpAddr6.sin6_addr = in6addr_any;

    server->httpListenSock6 = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (server->httpListenSock6 != INVALID_SOCKET) {
        DWORD v6only = 1;
        setsockopt(server->httpListenSock6, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&v6only, sizeof(v6only));
        setsockopt(server->httpListenSock6, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        if (bind(server->httpListenSock6, (struct sockaddr*)&httpAddr6, sizeof(httpAddr6)) == 0 &&
            listen(server->httpListenSock6, SOMAXCONN) == 0) {
            // IPv6 port 80 ready
        } else {
            closesocket(server->httpListenSock6);
            server->httpListenSock6 = INVALID_SOCKET;
        }
    }

    if (server->httpListenSock != INVALID_SOCKET || server->httpListenSock6 != INVALID_SOCKET) {
        printf("[REVERSE-PROXY] HTTP Redirector listening on port 80 (Redirects http:// -> https://)\n");
        server->hHttpThread = (HANDLE)_beginthreadex(NULL, 0, http_redirect_listener_thread, server, 0, NULL);
    }

    return true;
}

void reverse_proxy_stop(ReverseProxyServer* server) {
    if (!server) return;
    server->isRunning = false;

    if (server->listenSock != INVALID_SOCKET) {
        closesocket(server->listenSock);
        server->listenSock = INVALID_SOCKET;
    }
    if (server->listenSock6 != INVALID_SOCKET) {
        closesocket(server->listenSock6);
        server->listenSock6 = INVALID_SOCKET;
    }
    if (server->httpListenSock != INVALID_SOCKET) {
        closesocket(server->httpListenSock);
        server->httpListenSock = INVALID_SOCKET;
    }
    if (server->httpListenSock6 != INVALID_SOCKET) {
        closesocket(server->httpListenSock6);
        server->httpListenSock6 = INVALID_SOCKET;
    }

    if (server->hThread) {
        WaitForSingleObject(server->hThread, 2000);
        CloseHandle(server->hThread);
        server->hThread = NULL;
    }
    if (server->hHttpThread) {
        WaitForSingleObject(server->hHttpThread, 2000);
        CloseHandle(server->hHttpThread);
        server->hHttpThread = NULL;
    }
}
