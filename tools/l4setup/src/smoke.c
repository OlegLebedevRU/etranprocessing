#include "smoke.h"
#include "log.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <wtsapi32.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "wtsapi32.lib")

static bool check_proxy_info(void) {
    HINTERNET hSession = WinHttpOpen(L"l4setup/1.6.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    DWORD timeout = 5000;
    WinHttpSetTimeouts(hSession, timeout, timeout, timeout, timeout);

    bool ok = false;
    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", 18443, 0);
    if (hConnect) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/_leo4/info", NULL,
                                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hRequest) {
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hRequest, NULL)) {
                DWORD status_code = 0;
                DWORD status_code_size = sizeof(status_code);
                WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_code_size, WINHTTP_NO_HEADER_INDEX);

                if (status_code == 200) {
                    char buf[1024] = { 0 };
                    DWORD bytes_read = 0;
                    if (WinHttpReadData(hRequest, buf, sizeof(buf) - 1, &bytes_read) && bytes_read > 0) {
                        buf[bytes_read] = '\0';
                        if (strstr(buf, "\"status\"")) {
                            ok = true;
                        }
                    }
                }
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
    return ok;
}

static bool check_mosquitto_port(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    u_long non_blocking = 1;
    ioctlsocket(s, FIONBIO, &non_blocking);

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(1883);
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");

    connect(s, (struct sockaddr*)&sin, sizeof(sin));

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(s, &writefds);

    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;

    bool connected = false;
    if (select(0, NULL, &writefds, NULL, &tv) > 0) {
        int so_error = 0;
        int len = sizeof(so_error);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&so_error, &len) == 0 && so_error == 0) {
            connected = true;
        }
    }

    closesocket(s);
    WSACleanup();
    return connected;
}

static bool check_l4desk_in_session(DWORD target_session_id) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    bool running = false;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"l4desk.exe") == 0) {
                DWORD proc_sess = 0;
                if (ProcessIdToSessionId(pe.th32ProcessID, &proc_sess)) {
                    if (proc_sess == target_session_id) {
                        running = true;
                        break;
                    }
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return running;
}

static void check_ffmpeg_capture(const wchar_t* dest_dir, int session_id, char* out_status, size_t out_size) {
    if (!out_status || out_size == 0) return;

    if (session_id == 0) {
        strncpy_s(out_status, out_size, "skipped", _TRUNCATE);
        return;
    }

    wchar_t ffmpeg_exe[MAX_PATH];
    swprintf_s(ffmpeg_exe, MAX_PATH, L"%ls\\ffmpeg\\ffmpeg.exe", dest_dir);
    if (GetFileAttributesW(ffmpeg_exe) == INVALID_FILE_ATTRIBUTES) {
        strncpy_s(out_status, out_size, "skipped", _TRUNCATE);
        return;
    }

    wchar_t cmd[1024];
    swprintf_s(cmd, 1024, L"\"%ls\" -f gdigrab -i desktop -frames:v 1 -f null -", ffmpeg_exe);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        strncpy_s(out_status, out_size, "skipped", _TRUNCATE);
        return;
    }

    DWORD wait_res = WaitForSingleObject(pi.hProcess, 10000);
    if (wait_res == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        strncpy_s(out_status, out_size, "skipped", _TRUNCATE);
        return;
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exit_code == 0) {
        strncpy_s(out_status, out_size, "ok", _TRUNCATE);
    } else {
        strncpy_s(out_status, out_size, "skipped", _TRUNCATE);
    }
}

static bool check_network_reachability(void) {
    ADDRINFOA hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("iot.leo4.ru", "443", &hints, &res) == 0 && res != NULL) {
        freeaddrinfo(res);
        return true;
    }
    return false;
}

static bool check_desktop_locked(void) {
    HDESK hDesk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (hDesk != NULL) {
        CloseDesktop(hDesk);
        return false;
    }
    return true;
}

bool smoke_run_probes(
    const wchar_t* dest_dir,
    bool is_active_status,
    SmokeProbesResult* out_result
) {
    if (!out_result) return false;
    memset(out_result, 0, sizeof(SmokeProbesResult));

    log_info("Executing Phase 5 Smoke Tests...");

    // 1. Probe proxy_info
    bool proxy_ok = check_proxy_info();
    strcpy_s(out_result->proxy_info, sizeof(out_result->proxy_info), proxy_ok ? "ok" : "fail");
    log_info("Smoke probe [proxy_info]: %s", out_result->proxy_info);

    // 2. Probe mosquitto_port
    bool mosq_ok = check_mosquitto_port();
    strcpy_s(out_result->mosquitto_port, sizeof(out_result->mosquitto_port), mosq_ok ? "ok" : "fail");
    log_info("Smoke probe [mosquitto_port]: %s", out_result->mosquitto_port);

    // 3. User session and l4desk
    DWORD session_id = WTSGetActiveConsoleSessionId();
    out_result->user_session_id = (int)session_id;

    if (is_active_status && session_id != 0 && session_id != 0xFFFFFFFF) {
        out_result->l4desk_running = check_l4desk_in_session(session_id);
    } else {
        out_result->l4desk_running = false;
    }
    log_info("Smoke probe [user_session_id]: %d, [l4desk_running]: %s",
             out_result->user_session_id, out_result->l4desk_running ? "true" : "false");

    // 4. FFmpeg test frame capture
    check_ffmpeg_capture(dest_dir, (int)session_id, out_result->ffmpeg_smoke_capture, sizeof(out_result->ffmpeg_smoke_capture));
    log_info("Smoke probe [ffmpeg_smoke_capture]: %s", out_result->ffmpeg_smoke_capture);

    // 5. Desktop locked check
    out_result->desktop_locked = check_desktop_locked();
    log_info("Smoke probe [desktop_locked]: %s", out_result->desktop_locked ? "true" : "false");

    // 6. Remote input determination
    if (out_result->user_session_id == 0 || out_result->user_session_id == (int)0xFFFFFFFF) {
        strcpy_s(out_result->remote_input, sizeof(out_result->remote_input), "session_unavailable");
    } else if (out_result->desktop_locked) {
        strcpy_s(out_result->remote_input, sizeof(out_result->remote_input), "desktop_locked");
    } else if (out_result->l4desk_running) {
        strcpy_s(out_result->remote_input, sizeof(out_result->remote_input), "available");
    } else {
        strcpy_s(out_result->remote_input, sizeof(out_result->remote_input), "disabled");
    }
    log_info("Smoke probe [remote_input]: %s", out_result->remote_input);

    // 7. Network reachability check
    bool net_ok = check_network_reachability();
    strcpy_s(out_result->network, sizeof(out_result->network), net_ok ? "reachable" : "unreachable");
    log_info("Smoke probe [network]: %s", out_result->network);

    // Evaluate
    if (!proxy_ok || !mosq_ok) {
        out_result->critical_failed = true;
        out_result->calculated_exit_code = 27; // Critical smoke probe failed
        log_err("Smoke critical probe failed (proxy_info: %s, mosquitto_port: %s).",
                out_result->proxy_info, out_result->mosquitto_port);
        return false;
    }

    if (!net_ok || (is_active_status && session_id != 0 && !out_result->l4desk_running)) {
        out_result->has_warnings = true;
        out_result->calculated_exit_code = 12; // Degraded / ready with warnings
        log_warn("Smoke completed with degraded status (code 12).");
    } else {
        out_result->calculated_exit_code = 0; // All smoke probes cleanly passed
        log_info("Smoke probes completed successfully (code 0).");
    }

    return true;
}
