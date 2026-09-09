#include "ctl_protocol.h"
#include "json_min.h"
#include "dedup_cache.h"
#include "input_inject.h"
#include "desktop_state.h"
#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int64_t ctl_get_time_ms(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10000ULL);
}

void ctl_get_utc_iso(char* out, size_t max_len) {
    if (!out || max_len == 0) return;
    SYSTEMTIME st;
    GetSystemTime(&st);
    snprintf(out, max_len, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

static bool is_valid_uuid(const char* s) {
    if (!s || strlen(s) != 36) return false;
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else {
            if (!isxdigit((unsigned char)s[i])) return false;
        }
    }
    return true;
}

int ctl_build_presence_payload(char* buf, size_t max_len,
                              const char* status,
                              bool desktop_available,
                              const ScreenMetrics* screen) {
    char iso_time[64];
    ctl_get_utc_iso(iso_time, sizeof(iso_time));

    if (strcmp(status, "online") == 0 && screen) {
        return snprintf(buf, max_len,
            "{\"v\":1,\"type\":\"presence\",\"agent\":\"l4desk\",\"status\":\"online\","
            "\"desktop_available\":%s,\"screen\":{\"virtual_x\":%d,\"virtual_y\":%d,\"virtual_width\":%d,\"virtual_height\":%d},"
            "\"timestamp\":\"%s\"}",
            desktop_available ? "true" : "false",
            screen->virtual_x, screen->virtual_y, screen->virtual_width, screen->virtual_height,
            iso_time);
    } else {
        return snprintf(buf, max_len,
            "{\"v\":1,\"type\":\"presence\",\"agent\":\"l4desk\",\"status\":\"%s\","
            "\"desktop_available\":%s,\"timestamp\":\"%s\"}",
            status, desktop_available ? "true" : "false", iso_time);
    }
}

int ctl_build_ack_payload(char* buf, size_t max_len,
                          const char* command_id,
                          const char* lease_id,
                          const char* sn,
                          int64_t terminal_time_ms) {
    return snprintf(buf, max_len,
        "{\"v\":1,\"type\":\"ack\",\"command_id\":\"%s\",\"lease_id\":\"%s\",\"sn\":\"%s\","
        "\"result\":\"injected\",\"terminal_time_ms\":%lld}",
        command_id, lease_id, sn, (long long)terminal_time_ms);
}

int ctl_build_nack_payload(char* buf, size_t max_len,
                           const char* command_id,
                           const char* lease_id,
                           const char* sn,
                           const char* code,
                           const char* message,
                           int64_t terminal_time_ms) {
    return snprintf(buf, max_len,
        "{\"v\":1,\"type\":\"ack\",\"command_id\":\"%s\",\"lease_id\":\"%s\",\"sn\":\"%s\","
        "\"result\":\"nack\",\"code\":\"%s\",\"message\":\"%s\",\"terminal_time_ms\":%lld}",
        command_id ? command_id : "",
        lease_id ? lease_id : "",
        sn ? sn : "",
        code ? code : "unknown_error",
        message ? message : "",
        (long long)terminal_time_ms);
}

bool ctl_handle_command(const char* payload, size_t payload_len,
                        const char* own_sn,
                        char* out_resp, size_t max_resp,
                        size_t* out_resp_len,
                        bool* p_should_publish,
                        uint8_t* p_qos) {
    *p_should_publish = false;
    *p_qos = 1;
    if (out_resp_len) *out_resp_len = 0;

    int64_t now_ms = ctl_get_time_ms();

    // 1. Size check <= 1024 bytes and valid payload
    char cmd_id[64] = { 0 };
    char lease_id[64] = { 0 };
    char cmd_sn[64] = { 0 };
    char cmd_type[32] = { 0 };

    if (payload_len > 1024 || payload_len < 10) {
        log_warn("Dropping malformed command: length %zu > 1024 or too small", payload_len);
        return false;
    }

    int v = 0;
    if (!json_extract_int(payload, "v", &v) || v != 1) {
        log_warn("Dropping invalid protocol version v=%d", v);
        return false;
    }

    json_extract_str(payload, "command_id", cmd_id, sizeof(cmd_id));
    json_extract_str(payload, "lease_id", lease_id, sizeof(lease_id));
    json_extract_str(payload, "sn", cmd_sn, sizeof(cmd_sn));
    json_extract_str(payload, "type", cmd_type, sizeof(cmd_type));

    // 2. Type check
    bool is_move = (strcmp(cmd_type, "pointer_move") == 0);
    bool is_click = (strcmp(cmd_type, "mouse_click") == 0);
    if (!is_move && !is_click) {
        log_warn("Unsupported command type: '%s'", cmd_type);
        if (cmd_id[0] != '\0') {
            int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, "unsupported", "Unsupported command type", now_ms);
            if (len > 0) {
                *out_resp_len = (size_t)len;
                *p_should_publish = true;
                return true;
            }
        }
        return false;
    }

    // 3. SN check
    if (strcmp(cmd_sn, own_sn) != 0) {
        log_warn("Rejected command for foreign SN: '%s' (own: '%s')", cmd_sn, own_sn);
        if (is_click && cmd_id[0] != '\0') {
            int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, "invalid_sn", "SN mismatch", now_ms);
            if (len > 0) {
                *out_resp_len = (size_t)len;
                *p_should_publish = true;
                return true;
            }
        }
        return false;
    }

    // 4. UUID format check
    if (!is_valid_uuid(cmd_id) || !is_valid_uuid(lease_id)) {
        log_warn("Invalid UUID format: cmd_id='%s' lease_id='%s'", cmd_id, lease_id);
        if (is_click) {
            int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, "invalid_payload", "Invalid UUID format", now_ms);
            if (len > 0) {
                *out_resp_len = (size_t)len;
                *p_should_publish = true;
                return true;
            }
        }
        return false;
    }

    // 5. Expiration check (+2000 ms tolerance)
    int64_t expires_at_ms = 0;
    if (json_extract_int64(payload, "expires_at_ms", &expires_at_ms)) {
        if (expires_at_ms + 2000 < now_ms) {
            log_debug("Expired command: expires_at_ms=%lld now=%lld", (long long)expires_at_ms, (long long)now_ms);
            if (is_click) {
                int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, "expired", "Command expired", now_ms);
                if (len > 0) {
                    *out_resp_len = (size_t)len;
                    *p_should_publish = true;
                    return true;
                }
            }
            return false;
        }
    }

    // 6. Coordinates and button check
    int x = -1, y = -1;
    if (!json_extract_int(payload, "x", &x) || !json_extract_int(payload, "y", &y) ||
        x < 0 || x > 65535 || y < 0 || y > 65535) {
        log_warn("Invalid coordinates: x=%d y=%d", x, y);
        if (is_click) {
            int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, "invalid_payload", "Coordinates out of range 0..65535", now_ms);
            if (len > 0) {
                *out_resp_len = (size_t)len;
                *p_should_publish = true;
                return true;
            }
        }
        return false;
    }

    if (is_click) {
        char button[16] = { 0 };
        json_extract_str(payload, "button", button, sizeof(button));
        if (strcmp(button, "left") != 0) {
            log_warn("Unsupported button: '%s'", button);
            int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, "unsupported", "Only left button is supported", now_ms);
            if (len > 0) {
                *out_resp_len = (size_t)len;
                *p_should_publish = true;
                return true;
            }
            return false;
        }
    }

    // 7. Dedup check: if command already processed, resend cached response
    if (dedup_cache_get(cmd_id, out_resp, max_resp, out_resp_len)) {
        log_debug("Dedup cache hit for cmd=%s, resending cached response", cmd_id);
        *p_should_publish = true;
        return true;
    }

    // 8. Desktop availability check
    if (!desktop_is_interactive_available()) {
        log_warn("Interactive desktop unavailable for command cmd=%s", cmd_id);
        if (is_click) {
            int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, "interactive_desktop_unavailable", "Desktop is locked or in non-interactive session", now_ms);
            if (len > 0) {
                dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
                *out_resp_len = (size_t)len;
                *p_should_publish = true;
                return true;
            }
        }
        return false;
    }

    // 9. Input injection
    DWORD inject_err = 0;
    if (is_move) {
        bool ok = input_inject_move(x, y, &inject_err);
        if (!ok) {
            log_debug("input_inject_move failed (err=%lu)", inject_err);
        }
        // Do NOT send ACK for pointer_move (best-effort)
        *p_should_publish = false;
        return true;
    } else if (is_click) {
        bool ok = input_inject_click(x, y, &inject_err);
        if (!ok) {
            char errMsg[64];
            snprintf(errMsg, sizeof(errMsg), "SendInput failed with error %lu", inject_err);
            log_warn("Click injection failed for cmd=%s: %s", cmd_id, errMsg);
            int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, "inject_failed", errMsg, now_ms);
            if (len > 0) {
                dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
                *out_resp_len = (size_t)len;
                *p_should_publish = true;
                return true;
            }
        } else {
            log_info("Click injected at (%d, %d) for cmd=%s lease=%s", x, y, cmd_id, lease_id);
            int len = ctl_build_ack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, now_ms);
            if (len > 0) {
                dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
                *out_resp_len = (size_t)len;
                *p_should_publish = true;
                return true;
            }
        }
    }

    return true;
}
