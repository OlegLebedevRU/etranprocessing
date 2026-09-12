#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include "orchestrator.h"
#include "cert_discovery.h"
#include "hardware_fingerprint.h"
#include "service_mgr.h"
#include "mosquitto_conf.h"
#include "proxy_client.h"
#include "session_proc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shlwapi.lib")

static PROCESS_INFORMATION g_l4desk_pi = { 0 };
static HANDLE               g_l4desk_job = NULL;
static DWORD                g_l4desk_session = 0;
static time_t              g_l4desk_last_start_attempt = 0;
static int                 g_l4desk_backoff_sec = 5;

static HANDLE              g_hForceTickEvent = NULL;
static int                 g_proxy_cert_mismatch_ticks = 0;
static time_t              g_last_pending_pin_check = 0;

void orchestrator_trigger_force_tick(void) {
    if (g_hForceTickEvent) {
        SetEvent(g_hForceTickEvent);
    }
}

static bool json_extract_str(const char* json, const char* key, char* out, size_t out_size) {
    if (!json || !key || !out || out_size == 0) return false;
    char search[128];
    sprintf_s(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (*p != '\"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '\"' && i < out_size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

static bool json_extract_int(const char* json, const char* key, int* out_val) {
    if (!json || !key || !out_val) return false;
    char search[128];
    sprintf_s(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    *out_val = atoi(p);
    return true;
}

static void secure_delete_file(const wchar_t* path) {
    if (!path || !PathFileExistsW(path)) return;
    FILE* f = NULL;
    if (_wfopen_s(&f, path, L"rb+") == 0 && f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0) {
            char* zeros = (char*)calloc(1, sz);
            if (zeros) {
                fwrite(zeros, 1, sz, f);
                fflush(f);
                free(zeros);
            }
        }
        fclose(f);
    }
    DeleteFileW(path);
}

static bool parse_iso8601_time(const char* str, time_t* out_time) {
    if (!str || !out_time) return false;
    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
    if (sscanf_s(str, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec) < 3) {
        if (sscanf_s(str, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) < 3) {
            return false;
        }
    }
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    t.tm_isdst = 0;
    *out_time = _mkgmtime(&t);
    return (*out_time != (time_t)-1);
}

static bool tcp_can_connect(const char* host, int port, int timeout_sec) {
    if (!host || host[0] == '\0' || port <= 0) return false;

    WSADATA wsa;
    bool wsa_needed = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char port_str[16];
    sprintf_s(port_str, sizeof(port_str), "%d", port);

    struct addrinfo* res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        if (wsa_needed) WSACleanup();
        return false;
    }

    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) {
        freeaddrinfo(res);
        if (wsa_needed) WSACleanup();
        return false;
    }

    u_long non_blocking = 1;
    ioctlsocket(s, FIONBIO, &non_blocking);

    bool connected = false;
    int rc = connect(s, res->ai_addr, (int)res->ai_addrlen);
    if (rc == 0) {
        connected = true;
    } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set write_fds, except_fds;
        FD_ZERO(&write_fds);
        FD_SET(s, &write_fds);
        FD_ZERO(&except_fds);
        FD_SET(s, &except_fds);

        struct timeval tv;
        tv.tv_sec = timeout_sec > 0 ? timeout_sec : 5;
        tv.tv_usec = 0;

        int sel = select(0, NULL, &write_fds, &except_fds, &tv);
        if (sel > 0 && FD_ISSET(s, &write_fds) && !FD_ISSET(s, &except_fds)) {
            int err = 0;
            int err_len = sizeof(err);
            if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &err_len) == 0 && err == 0) {
                connected = true;
            }
        }
    }

    closesocket(s);
    freeaddrinfo(res);
    if (wsa_needed) WSACleanup();
    return connected;
}

static bool check_ca_reachability(const Leo4ProxyInfo* proxy_info, int timeout_sec, char* out_target, size_t out_target_size) {
    if (out_target && out_target_size > 0) out_target[0] = '\0';

    // 1. Check local listener in proxy_info (http_local)
    if (proxy_info && proxy_info->http_local[0] != '\0') {
        char host[128] = { 0 };
        int port = 18443;
        const char* p = proxy_info->http_local;
        if (_strnicmp(p, "http://", 7) == 0) p += 7;
        else if (_strnicmp(p, "https://", 8) == 0) p += 8;

        const char* colon = strchr(p, ':');
        if (colon) {
            size_t host_len = colon - p;
            if (host_len < sizeof(host)) {
                memcpy(host, p, host_len);
                host[host_len] = '\0';
            }
            port = atoi(colon + 1);
        } else {
            strcpy_s(host, sizeof(host), p);
        }

        if (strcmp(host, "0.0.0.0") == 0 || strcmp(host, "[::]") == 0) {
            strcpy_s(host, sizeof(host), "127.0.0.1");
        }

        if (tcp_can_connect(host, port, timeout_sec)) {
            if (out_target && out_target_size > 0) {
                snprintf(out_target, out_target_size, "%s:%d", host, port);
            }
            return true;
        }
    }

    // 2. Fallback to direct cloud endpoint iot-processing.ru:443
    if (tcp_can_connect("iot-processing.ru", 443, timeout_sec)) {
        if (out_target && out_target_size > 0) {
            strncpy_s(out_target, out_target_size, "iot-processing.ru:443", _TRUNCATE);
        }
        return true;
    }

    return false;
}

static void mask_pin_in_string(char* text, const char* pin) {
    if (!text || !text[0]) return;
    if (pin && pin[0]) {
        size_t pin_len = strlen(pin);
        char* p = text;
        while ((p = strstr(p, pin)) != NULL) {
            for (size_t i = 0; i < pin_len; i++) p[i] = '*';
            p += pin_len;
        }
    }
    // Mask pin=...
    char* p = text;
    while (*p) {
        if (_strnicmp(p, "pin=", 4) == 0) {
            p += 4;
            while (*p && *p != ' ' && *p != '&' && *p != '\r' && *p != '\n' && *p != '\t') {
                *p = '*';
                p++;
            }
        } else {
            p++;
        }
    }
}

static void log_info(const char* fmt, ...);

static bool process_pending_pin(const L4SupervConfig* cfg, const Leo4ProxyInfo* proxy_info, bool* p_force_tick) {
    if (p_force_tick) *p_force_tick = false;

    wchar_t pin_file[MAX_PATH];
    swprintf_s(pin_file, MAX_PATH, L"%ls\\pending_pin.json", cfg->base_path);
    if (!PathFileExistsW(pin_file)) {
        return false;
    }

    FILE* f = NULL;
    if (_wfopen_s(&f, pin_file, L"rb") != 0 || !f) {
        return false;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 64 * 1024) {
        fclose(f);
        secure_delete_file(pin_file);
        return false;
    }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return false;
    }

    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = '\0';
    fclose(f);

    int schema_val = 0;
    if (!json_extract_int(buf, "schema", &schema_val) || schema_val != 1) {
        log_info("[WAIT] pending_pin_invalid_schema: schema %d != 1, deleting file", schema_val);
        free(buf);
        secure_delete_file(pin_file);
        return false;
    }

    char expires_str[64] = { 0 };
    json_extract_str(buf, "expires_at", expires_str, sizeof(expires_str));
    time_t exp_time = 0;
    time_t now = time(NULL);
    if (parse_iso8601_time(expires_str, &exp_time) && exp_time < now) {
        log_info("[WAIT] pending_pin_expired: token expired at %s, deleting file", expires_str);
        free(buf);
        secure_delete_file(pin_file);
        return false;
    }

    char pin_dpapi_b64[2048] = { 0 };
    if (!json_extract_str(buf, "pin_dpapi", pin_dpapi_b64, sizeof(pin_dpapi_b64)) || pin_dpapi_b64[0] == '\0') {
        log_info("[WAIT] pending_pin_missing_dpapi: pin_dpapi missing, deleting file");
        free(buf);
        secure_delete_file(pin_file);
        return false;
    }
    free(buf);

    DWORD bin_len = 0;
    if (!CryptStringToBinaryA(pin_dpapi_b64, 0, CRYPT_STRING_BASE64, NULL, &bin_len, NULL, NULL) || bin_len == 0) {
        log_info("[WAIT] pending_pin_invalid_base64: base64 decode failed, deleting file");
        secure_delete_file(pin_file);
        return false;
    }

    BYTE* bin_data = (BYTE*)malloc(bin_len);
    if (!bin_data) {
        return false;
    }

    if (!CryptStringToBinaryA(pin_dpapi_b64, 0, CRYPT_STRING_BASE64, bin_data, &bin_len, NULL, NULL)) {
        free(bin_data);
        secure_delete_file(pin_file);
        return false;
    }

    DATA_BLOB in_blob = { bin_len, bin_data };
    DATA_BLOB out_blob = { 0, NULL };
    BOOL unprotect_ok = CryptUnprotectData(&in_blob, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out_blob);
    SecureZeroMemory(bin_data, bin_len);
    free(bin_data);

    if (!unprotect_ok || !out_blob.pbData || out_blob.cbData == 0) {
        log_info("[WAIT] pending_pin_dpapi_failed: CryptUnprotectData failed (err=%lu), deleting file", GetLastError());
        secure_delete_file(pin_file);
        return false;
    }

    char pin[128] = { 0 };
    size_t copy_len = out_blob.cbData < sizeof(pin) - 1 ? out_blob.cbData : sizeof(pin) - 1;
    memcpy(pin, out_blob.pbData, copy_len);
    pin[copy_len] = '\0';
    SecureZeroMemory(out_blob.pbData, out_blob.cbData);
    LocalFree(out_blob.pbData);

    // Trim whitespace
    char* pin_p = pin;
    while (*pin_p == ' ' || *pin_p == '\t' || *pin_p == '\r' || *pin_p == '\n') pin_p++;
    if (pin_p != pin) memmove(pin, pin_p, strlen(pin_p) + 1);
    size_t len = strlen(pin);
    while (len > 0 && (pin[len - 1] == ' ' || pin[len - 1] == '\t' || pin[len - 1] == '\r' || pin[len - 1] == '\n')) {
        pin[--len] = '\0';
    }

    char ca_target[128] = { 0 };
    bool ca_ok = check_ca_reachability(proxy_info, 5, ca_target, sizeof(ca_target));
    if (!ca_ok) {
        log_info("[WAIT] pending_pin found, expires_at=%s, ca=unreachable (leaving for next tick)", expires_str);
        SecureZeroMemory(pin, sizeof(pin));
        return false;
    }

    log_info("[WAIT] pending_pin found, expires_at=%s, ca=reachable (%s) -> l4pin", expires_str, ca_target);

    // Find l4pin.exe
    wchar_t l4pin_exe[MAX_PATH] = { 0 };
    swprintf_s(l4pin_exe, MAX_PATH, L"%ls\\l4pin\\l4pin.exe", cfg->base_path);
    if (!PathFileExistsW(l4pin_exe)) {
        swprintf_s(l4pin_exe, MAX_PATH, L"%ls\\l4pin\\x64\\l4pin.exe", cfg->base_path);
        if (!PathFileExistsW(l4pin_exe)) {
            swprintf_s(l4pin_exe, MAX_PATH, L"%ls\\l4pin\\x86\\l4pin.exe", cfg->base_path);
            if (!PathFileExistsW(l4pin_exe)) {
                swprintf_s(l4pin_exe, MAX_PATH, L"%ls\\bin\\l4pin.exe", cfg->base_path);
            }
        }
    }

    if (!PathFileExistsW(l4pin_exe)) {
        log_info("[WARN] l4pin.exe not found under %ls, cannot process pending PIN", cfg->base_path);
        SecureZeroMemory(pin, sizeof(pin));
        return false;
    }

    // Launch l4pin.exe <PIN>
    wchar_t cmdline[1024];
    swprintf_s(cmdline, 1024, L"\"%ls\" %hs", l4pin_exe, pin);

    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    CreatePipe(&hReadPipe, &hWritePipe, &sa, 0);
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    wchar_t workdir[MAX_PATH];
    swprintf_s(workdir, MAX_PATH, L"%ls\\l4pin", cfg->base_path);

    BOOL proc_ok = CreateProcessW(l4pin_exe, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, workdir, &si, &pi);

    // SECURE ZERO IMMEDIATELY
    SecureZeroMemory(cmdline, sizeof(cmdline));
    SecureZeroMemory(pin, sizeof(pin));
    CloseHandle(hWritePipe);

    if (!proc_ok) {
        log_info("[WARN] Failed to launch l4pin.exe (err=%lu)", GetLastError());
        CloseHandle(hReadPipe);
        return false;
    }

    // Wait up to 60s
    DWORD wait_res = WaitForSingleObject(pi.hProcess, 60000);
    if (wait_res == WAIT_TIMEOUT) {
        log_info("[WARN] l4pin.exe execution timed out after 60s, terminating");
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
    }

    char out_buf[4096] = { 0 };
    DWORD bytes_read = 0;
    ReadFile(hReadPipe, out_buf, sizeof(out_buf) - 1, &bytes_read, NULL);
    out_buf[bytes_read] = '\0';
    CloseHandle(hReadPipe);

    mask_pin_in_string(out_buf, NULL);

    char* line = out_buf;
    while (*line) {
        char* next_line = strchr(line, '\n');
        if (next_line) {
            *next_line = '\0';
            if (next_line > line && *(next_line - 1) == '\r') *(next_line - 1) = '\0';
        }
        if (line[0] != '\0') {
            log_info("[WAIT] l4pin: %s", line);
        }
        if (!next_line) break;
        line = next_line + 1;
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exit_code == 0) {
        cert_info ci;
        cert_state cs = cert_discover(NULL, &ci);
        if (cs == CERT_VALID || cs == CERT_EXPIRING) {
            log_info("[WAIT] l4pin succeeded, cert_state=%s. Deleting pending_pin.json and triggering force tick", cert_state_to_str(cs));
            secure_delete_file(pin_file);
            if (p_force_tick) *p_force_tick = true;
            orchestrator_trigger_force_tick();
            return true;
        } else {
            log_info("[WAIT] l4pin returned 0, but cert_state=%s. Keeping pending_pin for verification.", cert_state_to_str(cs));
            return false;
        }
    } else if (exit_code == 25) {
        log_info("[WAIT] pending_pin_rejected: CA rejected PIN (exit_code=25), deleting file");
        secure_delete_file(pin_file);
        return true;
    } else {
        log_info("[WAIT] l4pin returned exit_code=%lu (transient or network error), keeping pending_pin for next tick", exit_code);
        return false;
    }
}

bool orchestrator_get_l4desk_status(DWORD* out_pid, DWORD* out_session) {
    if (out_pid) *out_pid = 0;
    if (out_session) *out_session = 0;

    if (sp_is_alive(g_l4desk_pi.hProcess)) {
        if (out_pid) *out_pid = g_l4desk_pi.dwProcessId;
        DWORD sid = 0;
        ProcessIdToSessionId(g_l4desk_pi.dwProcessId, &sid);
        if (out_session) *out_session = sid ? sid : g_l4desk_session;
        return true;
    } else if (g_l4desk_pi.hProcess || g_l4desk_job) {
        sp_stop(&g_l4desk_pi, &g_l4desk_job, NULL, 0);
        g_l4desk_session = 0;
    }
    return false;
}

bool orchestrator_get_ffmpeg_status(const wchar_t* base_path, FFmpegStatus* out_status) {
    if (!out_status) return false;
    memset(out_status, 0, sizeof(FFmpegStatus));

    wchar_t state_file[MAX_PATH];
    swprintf_s(state_file, MAX_PATH, L"%ls\\l4desk\\state\\ffmpeg_state.json",
               (base_path && base_path[0]) ? base_path : L"C:\\l4tools");

    if (!PathFileExistsW(state_file)) {
        return false;
    }

    FILE* f = NULL;
    if (_wfopen_s(&f, state_file, L"rb") != 0 || !f) {
        return false;
    }

    char buf[4096] = { 0 };
    size_t bytes_read = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (bytes_read == 0) {
        return false;
    }
    buf[bytes_read] = '\0';

    int pid = 0;
    if (json_extract_int(buf, "pid", &pid)) {
        out_status->pid = (DWORD)pid;
    }
    json_extract_str(buf, "state", out_status->state, sizeof(out_status->state));
    json_extract_str(buf, "stream_instance_id", out_status->stream_instance_id, sizeof(out_status->stream_instance_id));

    if (out_status->pid > 0) {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, out_status->pid);
        if (!hProc) {
            hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, out_status->pid);
        }
        if (hProc) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(hProc, &exitCode) && exitCode == STILL_ACTIVE) {
                out_status->is_active = true;
            }
            CloseHandle(hProc);
        }
    }

    return true;
}

static void log_info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    printf("[%04d-%02d-%02d %02d:%02d:%02d] %s\n",
           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, buf);
    fflush(stdout);
}

bool orchestrator_step(const L4SupervConfig* cfg, L4State* state, bool* p_action_taken) {
    if (!cfg || !state) return false;
    if (p_action_taken) *p_action_taken = false;

    // 0. Cert Discovery (LocalMachine\MY)
    cert_info cinfo;
    cert_state cs = cert_discover(NULL, &cinfo);
    strcpy_s(state->last_cert_state, sizeof(state->last_cert_state), cert_state_to_str(cs));

    // 1. Hardware Fingerprint & Clone Detection
    char current_fp[128] = { 0 };
    hw_get_fingerprint(current_fp, sizeof(current_fp));

    if (state->hw_fingerprint[0] != '\0' && current_fp[0] != '\0' &&
        strcmp(state->hw_fingerprint, current_fp) != 0) {
        
        log_info("[WARN] Hardware fingerprint mismatch detected! (Old: %s, Current: %s, Cert: %s)",
                 state->hw_fingerprint, current_fp, cert_state_to_str(cs));

        if (cfg->auto_reset_on_clone) {
            log_info("[WARN] Auto-reset on clone is enabled. Cleaning certificates and resetting to Standby...");
            hw_clean_terminal_certificates();
            
            state_init(state);
            strcpy_s(state->hw_fingerprint, sizeof(state->hw_fingerprint), current_fp);
            state_update_services(cfg->base_path, state);
            state_save(cfg->base_path, state);

            mosquitto_conf_generate_standby(cfg->base_path, cfg->mosquitto_port);
            svc_restart(SVC_NAME_MOSQUITTO);
            svc_restart(SVC_NAME_LEO4PROXY);
            svc_restart(SVC_NAME_L4CON);

            if (sp_is_alive(g_l4desk_pi.hProcess)) {
                sp_stop(&g_l4desk_pi, &g_l4desk_job, NULL, 8000);
                g_l4desk_session = 0;
            }

            if (p_action_taken) *p_action_taken = true;
            return true;
        }
    } else if (state->hw_fingerprint[0] == '\0' && current_fp[0] != '\0') {
        strcpy_s(state->hw_fingerprint, sizeof(state->hw_fingerprint), current_fp);
        state_update_services(cfg->base_path, state);
        state_save(cfg->base_path, state);
    }

    // 2. Query Leo4Proxy
    Leo4ProxyInfo proxy_info;
    bool query_ok = proxy_client_query_info(cfg->proxy_url, 3000, &proxy_info);
    state->last_check = time(NULL);

    // 3. Handle Certificate Status Transitions
    ULONGLONG t_activation_start = 0;
    bool was_standby = (strcmp(state->status, "active") != 0);

    if (query_ok && proxy_info.cert_ready && proxy_info.sn[0] != '\0') {
        // --- Certificate is active & valid ---
        bool sn_changed = (strcmp(state->sn, proxy_info.sn) != 0);
        bool thumbprint_changed = (strcmp(state->thumbprint, proxy_info.thumbprint) != 0);

        if (was_standby) {
            t_activation_start = GetTickCount64();
            g_l4desk_last_start_attempt = 0;
            g_l4desk_backoff_sec = 0;
        }

        if (was_standby || sn_changed) {
            log_info("[STATE] Transitioning to ACTIVE (SN: %s, Thumbprint: %.8s...)",
                     proxy_info.sn, proxy_info.thumbprint);

            mosquitto_conf_generate_active(cfg->base_path, cfg->mosquitto_port, proxy_info.sn, cfg->mosquitto_template_path);
            svc_restart(SVC_NAME_MOSQUITTO);
            
            if (sn_changed) {
                // l4con caches SN on start; restart it to fetch new SN
                svc_restart(SVC_NAME_L4CON);

                // l4desk also caches SN on start; restart it
                if (sp_is_alive(g_l4desk_pi.hProcess)) {
                    wchar_t stop_evt[128];
                    swprintf_s(stop_evt, 128, L"Global\\L4Desk_Stop_%hs", state->sn);
                    sp_stop(&g_l4desk_pi, &g_l4desk_job, stop_evt, 8000);
                    g_l4desk_session = 0;
                }
            }

            strcpy_s(state->status, sizeof(state->status), "active");
            strcpy_s(state->sn, sizeof(state->sn), proxy_info.sn);
            strcpy_s(state->thumbprint, sizeof(state->thumbprint), proxy_info.thumbprint);
            strcpy_s(state->not_after, sizeof(state->not_after), proxy_info.not_after);
            state->updated_at = time(NULL);
            state_update_services(cfg->base_path, state);
            state_save(cfg->base_path, state);

            if (p_action_taken) *p_action_taken = true;

        } else if (thumbprint_changed) {
            // Certificate renewed with the same SN
            log_info("[STATE] Certificate renewed with same SN: %s (Thumbprint: %.8s...)",
                     proxy_info.sn, proxy_info.thumbprint);

            // Restart mosquitto to reconnect TLS bridge to updated proxy
            svc_restart(SVC_NAME_MOSQUITTO);

            strcpy_s(state->thumbprint, sizeof(state->thumbprint), proxy_info.thumbprint);
            strcpy_s(state->not_after, sizeof(state->not_after), proxy_info.not_after);
            state->updated_at = time(NULL);
            state_update_services(cfg->base_path, state);
            state_save(cfg->base_path, state);

            if (p_action_taken) *p_action_taken = true;
        } else {
            // Check if active mosquitto.conf was accidentally corrupted or overwritten
            if (!mosquitto_conf_is_active_with_sn(cfg->base_path, proxy_info.sn)) {
                log_info("[REPAIR] Active mosquitto.conf was missing/corrupted. Regenerating for SN: %s...", proxy_info.sn);
                mosquitto_conf_generate_active(cfg->base_path, cfg->mosquitto_port, proxy_info.sn, cfg->mosquitto_template_path);
                svc_restart(SVC_NAME_MOSQUITTO);
                if (p_action_taken) *p_action_taken = true;
            }
        }

        g_proxy_cert_mismatch_ticks = 0;

    } else {
        // --- Certificate is missing or proxy in standby ---
        bool was_active = (strcmp(state->status, "active") == 0);
        bool conf_in_standby = mosquitto_conf_is_standby(cfg->base_path);

        if (was_active || !conf_in_standby) {
            log_info("[STATE] Standby mode: no certificate loaded in leo4proxy. Configuring local Mosquitto...");
            
            mosquitto_conf_generate_standby(cfg->base_path, cfg->mosquitto_port);
            svc_restart(SVC_NAME_MOSQUITTO);

            strcpy_s(state->status, sizeof(state->status), "standby");
            state->sn[0] = '\0';
            state->thumbprint[0] = '\0';
            state->not_after[0] = '\0';
            state->updated_at = time(NULL);
            state_update_services(cfg->base_path, state);
            state_save(cfg->base_path, state);

            if (p_action_taken) *p_action_taken = true;
        }

        // Check proxy cert mismatch: CERT_VALID in store, but leo4proxy certificate_found is false
        if (cs == CERT_VALID && !proxy_info.certificate_found) {
            g_proxy_cert_mismatch_ticks++;
            if (g_proxy_cert_mismatch_ticks > 2) {
                log_info("[WARN] proxy_cert_mismatch: cert_state is CERT_VALID, but leo4proxy certificate_found is false for %d ticks",
                         g_proxy_cert_mismatch_ticks);
            }
        } else {
            g_proxy_cert_mismatch_ticks = 0;
        }

        // Process pending_pin.json in standby
        wchar_t pin_file_path[MAX_PATH];
        swprintf_s(pin_file_path, MAX_PATH, L"%ls\\pending_pin.json", cfg->base_path);
        bool has_pending_pin = PathFileExistsW(pin_file_path);

        time_t now = time(NULL);
        if (has_pending_pin) {
            if (g_last_pending_pin_check == 0 || (now - g_last_pending_pin_check >= cfg->pending_pin_check_sec)) {
                g_last_pending_pin_check = now;
                bool force_tick = false;
                process_pending_pin(cfg, &proxy_info, &force_tick);
                if (force_tick && p_action_taken) {
                    *p_action_taken = true;
                }
            }
            has_pending_pin = PathFileExistsW(pin_file_path);
        }

        const char* ca_diag = "unknown";
        char ca_target[128] = { 0 };
        if (check_ca_reachability(&proxy_info, 2, ca_target, sizeof(ca_target))) {
            ca_diag = "reachable";
        } else {
            ca_diag = "unreachable";
        }

        log_info("[WAIT] standby: cert_state=%s pending_pin=%s ca=%s next=%ds",
                 cert_state_to_str(cs),
                 has_pending_pin ? "yes" : "no",
                 ca_diag,
                 cfg->standby_poll_sec > 0 ? cfg->standby_poll_sec : 5);
    }

    // 4. Watchdog Process & Path Enforcement
    if (cfg->watchdog_enabled) {
        if (cfg->auto_start_leo4proxy) {
            if (!svc_exists(SVC_NAME_LEO4PROXY) || !svc_is_running(SVC_NAME_LEO4PROXY)) {
                log_info("[WATCHDOG] %ls is stopped or missing. Starting...", SVC_NAME_LEO4PROXY);
                svc_start(SVC_NAME_LEO4PROXY);
                if (p_action_taken) *p_action_taken = true;
            }
        }
        if (cfg->auto_start_mosquitto) {
            if (!svc_exists(SVC_NAME_MOSQUITTO) || !svc_is_running(SVC_NAME_MOSQUITTO)) {
                log_info("[WATCHDOG] %ls is stopped or missing. Starting...", SVC_NAME_MOSQUITTO);
                svc_start(SVC_NAME_MOSQUITTO);
                if (p_action_taken) *p_action_taken = true;
            }
        }
        if (cfg->auto_start_l4con) {
            if (!svc_exists(SVC_NAME_L4CON) || !svc_is_running(SVC_NAME_L4CON)) {
                log_info("[WATCHDOG] %ls is stopped or missing. Starting...", SVC_NAME_L4CON);
                svc_start(SVC_NAME_L4CON);
                if (p_action_taken) *p_action_taken = true;
            }
        }
        if (cfg->auto_start_l4desk) {
            // Only start l4desk when state is ACTIVE and SN is resolved
            if (strcmp(state->status, "active") == 0 && state->sn[0] != '\0') {
                DWORD active_session = sp_get_active_console_session();
                if (active_session == 0) {
                    if (sp_is_alive(g_l4desk_pi.hProcess)) {
                        log_info("[WATCHDOG] No active console session found. Stopping l4desk...");
                        wchar_t stop_evt[128];
                        swprintf_s(stop_evt, 128, L"Global\\L4Desk_Stop_%hs", state->sn);
                        sp_stop(&g_l4desk_pi, &g_l4desk_job, stop_evt, 8000);
                        g_l4desk_session = 0;
                    }
                } else {
                    if (sp_is_alive(g_l4desk_pi.hProcess)) {
                        DWORD proc_session = 0;
                        ProcessIdToSessionId(g_l4desk_pi.dwProcessId, &proc_session);
                        if (proc_session != active_session) {
                            log_info("[WATCHDOG] Active console session changed (%lu -> %lu). Re-launching l4desk...",
                                     proc_session, active_session);
                            wchar_t stop_evt[128];
                            swprintf_s(stop_evt, 128, L"Global\\L4Desk_Stop_%hs", state->sn);
                            sp_stop(&g_l4desk_pi, &g_l4desk_job, stop_evt, 8000);
                            g_l4desk_session = 0;
                        }
                    }

                    if (!sp_is_alive(g_l4desk_pi.hProcess)) {
                        time_t now = time(NULL);
                        if (now - g_l4desk_last_start_attempt >= g_l4desk_backoff_sec) {
                            g_l4desk_last_start_attempt = now;
                            wchar_t l4desk_exe[MAX_PATH];
                            swprintf_s(l4desk_exe, MAX_PATH, L"%ls\\l4desk\\l4desk.exe", cfg->base_path);
                            if (!PathFileExistsW(l4desk_exe)) {
                                swprintf_s(l4desk_exe, MAX_PATH, L"%ls\\l4desk\\x86\\l4desk.exe", cfg->base_path);
                                if (!PathFileExistsW(l4desk_exe)) {
                                    swprintf_s(l4desk_exe, MAX_PATH, L"%ls\\l4desk\\x64\\l4desk.exe", cfg->base_path);
                                }
                            }

                            if (PathFileExistsW(l4desk_exe)) {
                                wchar_t cmdline[1024];
                                swprintf_s(cmdline, 1024, L"\"%ls\" %ls", l4desk_exe,
                                           cfg->l4desk_args[0] ? cfg->l4desk_args : L"--run --presence-interval 30");
                                wchar_t workdir[MAX_PATH];
                                swprintf_s(workdir, MAX_PATH, L"%ls\\l4desk", cfg->base_path);

                                log_info("[WATCHDOG] Launching l4desk in active console session %lu...", active_session);
                                sp_enable_system_privileges();
                                if (sp_start_in_session(active_session, l4desk_exe, cmdline, workdir, &g_l4desk_pi, &g_l4desk_job)) {
                                    log_info("[WATCHDOG] l4desk started in session %lu (PID: %lu, Job: %p)",
                                             active_session, g_l4desk_pi.dwProcessId, g_l4desk_job);
                                    g_l4desk_session = active_session;
                                    g_l4desk_backoff_sec = 5;
                                    if (p_action_taken) *p_action_taken = true;
                                } else {
                                    log_info("[WARN] Failed to start l4desk in session %lu (err=%lu)", active_session, GetLastError());
                                    g_l4desk_backoff_sec = (g_l4desk_backoff_sec < 30) ? (g_l4desk_backoff_sec == 5 ? 10 : 30) : 30;
                                }
                            }
                        }
                    }
                }
            } else {
                if (sp_is_alive(g_l4desk_pi.hProcess)) {
                    log_info("[WATCHDOG] Terminal state not active. Stopping l4desk...");
                    sp_stop(&g_l4desk_pi, &g_l4desk_job, NULL, 8000);
                    g_l4desk_session = 0;
                }
            }
        }
    }

    // If transition to active just completed, log activation time
    if (was_standby && t_activation_start > 0 && strcmp(state->status, "active") == 0) {
        ULONGLONG t_activation_elapsed = GetTickCount64() - t_activation_start;
        log_info("[WAIT] activation completed in %llu ms", t_activation_elapsed);
    }

    // Always keep services state updated in state.json
    state_update_services(cfg->base_path, state);
    state_save(cfg->base_path, state);

    return true;
}

void orchestrator_run_loop(const L4SupervConfig* cfg, volatile bool* p_stop_flag) {
    if (!cfg) return;

    if (!g_hForceTickEvent) {
        g_hForceTickEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    }

    log_info("=======================================================");
    log_info(" l4superv (Leo4 Supervisor & Watchdog) Started");
    log_info(" Base Directory:    %ls", cfg->base_path);
    log_info(" Active Poll:       %d sec", cfg->watchdog_interval_sec);
    log_info(" Standby Poll:      %d sec", cfg->standby_poll_sec);
    log_info(" Pending PIN Check: %d sec", cfg->pending_pin_check_sec);
    log_info(" Watchdog:          %s", cfg->watchdog_enabled ? "Enabled" : "Disabled");
    log_info("=======================================================");

    L4State state;
    state_load(cfg->base_path, &state);

    while (!p_stop_flag || !(*p_stop_flag)) {
        bool action = false;
        orchestrator_step(cfg, &state, &action);

        if (p_stop_flag && *p_stop_flag) break;

        int poll_sec = (strcmp(state.status, "standby") == 0) ?
                       (cfg->standby_poll_sec > 0 ? cfg->standby_poll_sec : 5) :
                       (cfg->watchdog_interval_sec > 0 ? cfg->watchdog_interval_sec : 10);

        DWORD wait_res = WaitForSingleObject(g_hForceTickEvent, poll_sec * 1000);
        if (wait_res == WAIT_OBJECT_0) {
            log_info("[TICK] Force tick triggered via SERVICE_CONTROL 128");
        }
    }

    if (sp_is_alive(g_l4desk_pi.hProcess)) {
        wchar_t stop_evt[128];
        swprintf_s(stop_evt, 128, L"Global\\L4Desk_Stop_%hs", state.sn);
        sp_stop(&g_l4desk_pi, &g_l4desk_job, stop_evt, 8000);
        g_l4desk_session = 0;
    }

    state_cleanup(&state);
    if (g_hForceTickEvent) {
        CloseHandle(g_hForceTickEvent);
        g_hForceTickEvent = NULL;
    }

    log_info("l4superv supervisor loop stopped.");
}
