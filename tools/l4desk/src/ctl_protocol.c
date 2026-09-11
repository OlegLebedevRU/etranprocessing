#include "ctl_protocol.h"
#include "json_min.h"
#include "dedup_cache.h"
#include "input_inject.h"
#include "desktop_state.h"
#include "ffmpeg_supervisor.h"
#include "log.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

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

static bool is_valid_identifier(const char* s) {
    if (!s) return false;
    size_t len = strlen(s);
    if (len < 1 || len > 64) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c <= 32 || c >= 127) return false;
    }
    return true;
}

static void json_escape_str(const char* in, char* out, size_t max_out) {
    if (!out || max_out == 0) return;
    size_t j = 0;
    for (size_t i = 0; in && in[i] && j + 2 < max_out; i++) {
        if (in[i] == '\\') {
            out[j++] = '\\';
            out[j++] = '\\';
        } else if (in[i] == '"') {
            out[j++] = '\\';
            out[j++] = '"';
        } else {
            out[j++] = in[i];
        }
    }
    out[j] = '\0';
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

int ctl_build_extended_presence_payload(char* buf, size_t max_len,
                                       const char* status,
                                       bool desktop_available,
                                       const ScreenMetrics* screen,
                                       const SystemInventory* inv,
                                       const StreamStateInfo* stream) {
    char iso_time[64];
    ctl_get_utc_iso(iso_time, sizeof(iso_time));

    DWORD session_id = desktop_get_current_session_id();

    int offset = snprintf(buf, max_len,
        "{\"v\":1,\"type\":\"presence\",\"agent\":\"l4desk\",\"status\":\"%s\","
        "\"desktop_available\":%s,\"session_id\":%u,",
        status, desktop_available ? "true" : "false", session_id);

    if (screen && offset > 0 && (size_t)offset < max_len) {
        offset += snprintf(buf + offset, max_len - offset,
            "\"screen\":{\"virtual_x\":%d,\"virtual_y\":%d,\"virtual_width\":%d,\"virtual_height\":%d},",
            screen->virtual_x, screen->virtual_y, screen->virtual_width, screen->virtual_height);
    }

    /* Inventory block */
    if (inv && offset > 0 && (size_t)offset < max_len) {
        offset += snprintf(buf + offset, max_len - offset, "\"inventory\":{\"displays\":[");
        for (int i = 0; i < inv->display_count; i++) {
            const DisplayInfo* d = &inv->displays[i];
            char esc_name[128];
            json_escape_str(d->name, esc_name, sizeof(esc_name));
            offset += snprintf(buf + offset, max_len - offset,
                "%s{\"desktop_id\":\"%s\",\"name\":\"%s\",\"primary\":%s,\"x\":%d,\"y\":%d,"
                "\"width\":%d,\"height\":%d,\"session_id\":%u,\"policy\":\"%s\"}",
                (i > 0 ? "," : ""),
                d->desktop_id, esc_name, d->primary ? "true" : "false",
                d->x, d->y, d->width, d->height, d->session_id, d->policy);
        }

        offset += snprintf(buf + offset, max_len - offset, "],\"cameras\":[");
        for (int i = 0; i < inv->camera_count; i++) {
            const CameraInfo* c = &inv->cameras[i];
            char esc_cam_name[256];
            json_escape_str(c->name, esc_cam_name, sizeof(esc_cam_name));
            offset += snprintf(buf + offset, max_len - offset,
                "%s{\"camera_id\":\"%s\",\"name\":\"%s\",\"available\":%s}",
                (i > 0 ? "," : ""),
                c->camera_id, esc_cam_name, c->available ? "true" : "false");
        }
        offset += snprintf(buf + offset, max_len - offset, "]},");
    }

    /* Stream block */
    if (stream && offset > 0 && (size_t)offset < max_len) {
        const char* state_val = stream->state[0] ? stream->state : "stopped";
        const char* mode_val = (strcmp(state_val, "stopped") == 0 || stream->mode[0] == '\0') ? "stopped" : stream->mode;

        offset += snprintf(buf + offset, max_len - offset,
            "\"stream\":{\"state\":\"%s\",\"mode\":\"%s\",",
            state_val, mode_val);

        if (stream->source_id[0] != '\0') {
            char esc_src[128];
            json_escape_str(stream->source_id, esc_src, sizeof(esc_src));
            offset += snprintf(buf + offset, max_len - offset, "\"source_id\":\"%s\",", esc_src);
        } else {
            offset += snprintf(buf + offset, max_len - offset, "\"source_id\":null,");
        }

        if (stream->stream_instance_id[0] != '\0') {
            char esc_inst[128];
            json_escape_str(stream->stream_instance_id, esc_inst, sizeof(esc_inst));
            offset += snprintf(buf + offset, max_len - offset, "\"stream_instance_id\":\"%s\",", esc_inst);
        } else {
            offset += snprintf(buf + offset, max_len - offset, "\"stream_instance_id\":null,");
        }

        const char* profile_val = stream->profile[0] ? stream->profile : "default";

        char reason_buf[128];
        if (stream->reason[0] != '\0') {
            char esc_reason[96];
            json_escape_str(stream->reason, esc_reason, sizeof(esc_reason));
            snprintf(reason_buf, sizeof(reason_buf), "\"%s\"", esc_reason);
        } else {
            strcpy_s(reason_buf, sizeof(reason_buf), "null");
        }

        char started_at_buf[64];
        if (strcmp(state_val, "stopped") == 0 || stream->started_at == 0) {
            strcpy_s(started_at_buf, sizeof(started_at_buf), "null");
        } else {
            time_t sec = 0;
            int ms = 0;
            if (stream->started_at > 100000000000ULL) {
                sec = (time_t)(stream->started_at / 1000ULL);
                ms = (int)(stream->started_at % 1000ULL);
            } else {
                sec = (time_t)stream->started_at;
                ms = 0;
            }
            struct tm tm_utc;
            memset(&tm_utc, 0, sizeof(tm_utc));
            if (gmtime_s(&tm_utc, &sec) == 0) {
                snprintf(started_at_buf, sizeof(started_at_buf), "\"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ\"",
                         tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                         tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, ms);
            } else {
                strcpy_s(started_at_buf, sizeof(started_at_buf), "null");
            }
        }

        offset += snprintf(buf + offset, max_len - offset,
            "\"profile\":\"%s\",\"reason\":%s,\"ffmpeg_pid\":%u,\"started_at\":%s,\"restart_count\":%d},",
            profile_val,
            reason_buf,
            stream->ffmpeg_pid,
            started_at_buf,
            stream->restart_count);
    }

    if (offset > 0 && (size_t)offset < max_len) {
        offset += snprintf(buf + offset, max_len - offset, "\"timestamp\":\"%s\"}", iso_time);
    }

    return offset;
}

static bool ctl_is_valid_stream_state(const char* s) {
    if (!s) return false;
    return (strcmp(s, "stopped") == 0 ||
            strcmp(s, "starting") == 0 ||
            strcmp(s, "running") == 0 ||
            strcmp(s, "stopping") == 0 ||
            strcmp(s, "restarting") == 0 ||
            strcmp(s, "failed") == 0 ||
            strcmp(s, "source_unavailable") == 0 ||
            strcmp(s, "session_unavailable") == 0);
}

int ctl_build_stream_event_payload(char* buf, size_t max_len,
                                  const char* sn,
                                  const char* stream_instance_id,
                                  const char* state,
                                  const char* reason) {
    char iso_time[64];
    ctl_get_utc_iso(iso_time, sizeof(iso_time));

    const char* valid_state = (state && ctl_is_valid_stream_state(state)) ? state : "failed";

    char esc_sn[128] = { 0 };
    char esc_inst[128] = { 0 };
    char esc_reason[256] = { 0 };

    json_escape_str(sn ? sn : "", esc_sn, sizeof(esc_sn));
    json_escape_str(stream_instance_id ? stream_instance_id : "", esc_inst, sizeof(esc_inst));
    json_escape_str(reason ? reason : "", esc_reason, sizeof(esc_reason));

    return snprintf(buf, max_len,
        "{\"v\":1,\"type\":\"stream_event\",\"sn\":\"%s\",\"stream_instance_id\":\"%s\","
        "\"state\":\"%s\",\"reason\":\"%s\",\"timestamp\":\"%s\"}",
        esc_sn,
        esc_inst,
        valid_state,
        esc_reason,
        iso_time);
}

int ctl_build_ack_payload(char* buf, size_t max_len,
                          const char* command_id,
                          const char* lease_id,
                          const char* sn,
                          int64_t terminal_time_ms) {
    return snprintf(buf, max_len,
        "{\"v\":1,\"type\":\"ack\",\"command_id\":\"%s\",\"lease_id\":\"%s\",\"sn\":\"%s\","
        "\"result\":\"injected\",\"terminal_time_ms\":%lld}",
        command_id ? command_id : "",
        lease_id ? lease_id : "",
        sn ? sn : "",
        (long long)terminal_time_ms);
}

int ctl_build_ack_stream_payload(char* buf, size_t max_len,
                                 const char* command_id,
                                 const char* lease_id,
                                 const char* sn,
                                 const char* result,
                                 const char* stream_instance_id,
                                 const char* state,
                                 int64_t terminal_time_ms) {
    if (stream_instance_id && stream_instance_id[0] != '\0') {
        return snprintf(buf, max_len,
            "{\"v\":1,\"type\":\"ack\",\"command_id\":\"%s\",\"lease_id\":\"%s\",\"sn\":\"%s\","
            "\"result\":\"%s\",\"stream_instance_id\":\"%s\",\"state\":\"%s\",\"terminal_time_ms\":%lld}",
            command_id ? command_id : "",
            lease_id ? lease_id : "",
            sn ? sn : "",
            result ? result : "",
            stream_instance_id,
            state ? state : "",
            (long long)terminal_time_ms);
    } else {
        return snprintf(buf, max_len,
            "{\"v\":1,\"type\":\"ack\",\"command_id\":\"%s\",\"lease_id\":\"%s\",\"sn\":\"%s\","
            "\"result\":\"%s\",\"stream_instance_id\":null,\"state\":\"%s\",\"terminal_time_ms\":%lld}",
            command_id ? command_id : "",
            lease_id ? lease_id : "",
            sn ? sn : "",
            result ? result : "",
            state ? state : "",
            (long long)terminal_time_ms);
    }
}

int ctl_build_ack_inventory_payload(char* buf, size_t max_len,
                                    const char* command_id,
                                    const char* lease_id,
                                    const char* sn,
                                    const SystemInventory* inv,
                                    int64_t terminal_time_ms) {
    int offset = snprintf(buf, max_len,
        "{\"v\":1,\"type\":\"ack\",\"command_id\":\"%s\",\"lease_id\":\"%s\",\"sn\":\"%s\","
        "\"result\":\"inventory\",\"inventory\":{\"displays\":[",
        command_id ? command_id : "",
        lease_id ? lease_id : "",
        sn ? sn : "");

    if (inv && offset > 0 && (size_t)offset < max_len) {
        for (int i = 0; i < inv->display_count; i++) {
            const DisplayInfo* d = &inv->displays[i];
            char esc_name[128];
            json_escape_str(d->name, esc_name, sizeof(esc_name));
            offset += snprintf(buf + offset, max_len - offset,
                "%s{\"desktop_id\":\"%s\",\"name\":\"%s\",\"primary\":%s,\"x\":%d,\"y\":%d,"
                "\"width\":%d,\"height\":%d,\"session_id\":%u,\"policy\":\"%s\"}",
                (i > 0 ? "," : ""),
                d->desktop_id, esc_name, d->primary ? "true" : "false",
                d->x, d->y, d->width, d->height, d->session_id, d->policy);
        }

        offset += snprintf(buf + offset, max_len - offset, "],\"cameras\":[");
        for (int i = 0; i < inv->camera_count; i++) {
            const CameraInfo* c = &inv->cameras[i];
            char esc_cam_name[256];
            json_escape_str(c->name, esc_cam_name, sizeof(esc_cam_name));
            offset += snprintf(buf + offset, max_len - offset,
                "%s{\"camera_id\":\"%s\",\"name\":\"%s\",\"available\":%s}",
                (i > 0 ? "," : ""),
                c->camera_id, esc_cam_name, c->available ? "true" : "false");
        }
        offset += snprintf(buf + offset, max_len - offset, "]},");
    } else {
        offset += snprintf(buf + offset, max_len - offset, "],\"cameras\":[]},");
    }

    if (offset > 0 && (size_t)offset < max_len) {
        offset += snprintf(buf + offset, max_len - offset, "\"terminal_time_ms\":%lld}",
                           (long long)terminal_time_ms);
    }
    return offset;
}

int ctl_build_nack_payload(char* buf, size_t max_len,
                           const char* command_id,
                           const char* lease_id,
                           const char* sn,
                           const char* code,
                           const char* message,
                           int64_t terminal_time_ms) {
    char esc_msg[512] = { 0 };
    json_escape_str(message, esc_msg, sizeof(esc_msg));

    return snprintf(buf, max_len,
        "{\"v\":1,\"type\":\"ack\",\"command_id\":\"%s\",\"lease_id\":\"%s\",\"sn\":\"%s\","
        "\"result\":\"nack\",\"code\":\"%s\",\"message\":\"%s\",\"terminal_time_ms\":%lld}",
        command_id ? command_id : "",
        lease_id ? lease_id : "",
        sn ? sn : "",
        code ? code : "unknown_error",
        esc_msg,
        (long long)terminal_time_ms);
}

bool ctl_handle_command(const char* payload, size_t payload_len,
                        const char* own_sn,
                        const SystemInventory* inv,
                        char* out_resp, size_t max_resp,
                        size_t* out_resp_len,
                        bool* p_should_publish,
                        uint8_t* p_qos) {
    *p_should_publish = false;
    *p_qos = 1;
    if (out_resp_len) *out_resp_len = 0;

    int64_t now_ms = ctl_get_time_ms();

    if (payload_len > 8192 || payload_len < 10) {
        log_warn("Dropping malformed command: length %zu out of range", payload_len);
        return false;
    }

    int v = 0;
    if (!json_extract_int(payload, "v", &v) || v != 1) {
        log_warn("Dropping invalid protocol version v=%d", v);
        return false;
    }

    char cmd_id[64] = { 0 };
    char lease_id[64] = { 0 };
    char cmd_sn[64] = { 0 };
    char cmd_type[32] = { 0 };

    json_extract_str(payload, "command_id", cmd_id, sizeof(cmd_id));
    json_extract_str(payload, "lease_id", lease_id, sizeof(lease_id));
    json_extract_str(payload, "sn", cmd_sn, sizeof(cmd_sn));
    json_extract_str(payload, "type", cmd_type, sizeof(cmd_type));

    log_info("Received command type: '%s', id: '%s'", cmd_type, cmd_id);

    if (cmd_sn[0] != '\0' && own_sn && own_sn[0] != '\0' && strcmp(cmd_sn, own_sn) != 0) {
        log_warn("Rejected command for foreign SN: '%s' (own: '%s')", cmd_sn, own_sn);
        int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, "invalid_sn", "SN mismatch", now_ms);
        if (len > 0) {
            *out_resp_len = (size_t)len;
            *p_should_publish = true;
            return true;
        }
        return false;
    }

    if (!is_valid_identifier(cmd_id)) {
        log_warn("Invalid command_id format: '%s'", cmd_id);
        int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, "invalid_payload", "Invalid command_id", now_ms);
        if (len > 0) {
            *out_resp_len = (size_t)len;
            *p_should_publish = true;
            return true;
        }
        return false;
    }

    /* Expiration check (+2000 ms tolerance) */
    int64_t expires_at_ms = 0;
    if (json_extract_int64(payload, "expires_at_ms", &expires_at_ms)) {
        if (expires_at_ms > 0 && expires_at_ms + 2000 < now_ms) {
            log_debug("Expired command: expires_at_ms=%lld now=%lld", (long long)expires_at_ms, (long long)now_ms);
            int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, "expired", "Command expired", now_ms);
            if (len > 0) {
                *out_resp_len = (size_t)len;
                *p_should_publish = true;
                return true;
            }
            return false;
        }
    }

    /* Dedup check */
    if (dedup_cache_get(cmd_id, out_resp, max_resp, out_resp_len)) {
        log_debug("Dedup cache hit for cmd=%s, resending cached response", cmd_id);
        *p_should_publish = true;
        return true;
    }

    /* 1. inventory_get */
    if (strcmp(cmd_type, "inventory_get") == 0) {
        int len = ctl_build_ack_inventory_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, inv, now_ms);
        if (len > 0) {
            dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
            *out_resp_len = (size_t)len;
            *p_should_publish = true;
            return true;
        }
        return false;
    }

    /* 2. stream_start */
    if (strcmp(cmd_type, "stream_start") == 0) {
        char mode[32] = { 0 };
        char source_id[64] = { 0 };
        char profile[32] = { 0 };
        char stream_instance_id[64] = { 0 };

        json_extract_str(payload, "mode", mode, sizeof(mode));
        json_extract_str(payload, "source_id", source_id, sizeof(source_id));
        json_extract_str(payload, "profile", profile, sizeof(profile));
        json_extract_str(payload, "stream_instance_id", stream_instance_id, sizeof(stream_instance_id));

        if (mode[0] == '\0' || source_id[0] == '\0' || stream_instance_id[0] == '\0') {
            int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                             "invalid_payload", "Missing required stream_start fields", now_ms);
            if (len > 0) {
                dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
                *out_resp_len = (size_t)len;
                *p_should_publish = true;
                return true;
            }
            return false;
        }

        char result[32] = { 0 };
        char err_code[64] = { 0 };
        char err_msg[256] = { 0 };

        bool ok = ffmpeg_supervisor_start(stream_instance_id, lease_id, mode, source_id,
                                          profile, inv, result, sizeof(result),
                                          err_code, sizeof(err_code),
                                          err_msg, sizeof(err_msg));
        int len = 0;
        if (ok) {
            if (expires_at_ms > 0) {
                ffmpeg_supervisor_update_lease(lease_id, (uint64_t)expires_at_ms);
            }
            len = ctl_build_ack_stream_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                               result, stream_instance_id, "running", now_ms);
        } else {
            len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                         err_code, err_msg, now_ms);
        }

        if (len > 0) {
            dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
            *out_resp_len = (size_t)len;
            *p_should_publish = true;
            return true;
        }
        return false;
    }

    /* 3. stream_stop */
    if (strcmp(cmd_type, "stream_stop") == 0) {
        char stream_instance_id[64] = { 0 };
        json_extract_str(payload, "stream_instance_id", stream_instance_id, sizeof(stream_instance_id));

        char result[32] = { 0 };
        char err_code[64] = { 0 };
        char err_msg[256] = { 0 };

        bool ok = ffmpeg_supervisor_stop(stream_instance_id, lease_id,
                                         result, sizeof(result),
                                         err_code, sizeof(err_code),
                                         err_msg, sizeof(err_msg));
        int len = 0;
        if (ok) {
            len = ctl_build_ack_stream_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                               result, stream_instance_id, "stopped", now_ms);
        } else {
            len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                         err_code, err_msg, now_ms);
        }

        if (len > 0) {
            dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
            *out_resp_len = (size_t)len;
            *p_should_publish = true;
            return true;
        }
        return false;
    }

    /* lease_renew / stream_renew */
    if (strcmp(cmd_type, "lease_renew") == 0 || strcmp(cmd_type, "stream_renew") == 0) {
        StreamStateInfo stream;
        ffmpeg_supervisor_get_info(&stream);

        if (strcmp(stream.state, "running") != 0 && strcmp(stream.state, "restarting") != 0) {
            int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                             "stream_not_running", "No active stream to renew", now_ms);
            if (len > 0) {
                *out_resp_len = (size_t)len;
                *p_should_publish = true;
                return true;
            }
            return false;
        }

        if (stream.lease_id[0] != '\0' && strcmp(stream.lease_id, lease_id) != 0) {
            int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                             "lease_mismatch", "Active stream lease does not match", now_ms);
            if (len > 0) {
                *out_resp_len = (size_t)len;
                *p_should_publish = true;
                return true;
            }
            return false;
        }

        if (expires_at_ms > 0) {
            ffmpeg_supervisor_update_lease(lease_id, (uint64_t)expires_at_ms);
            log_info("Lease renewed for stream %s: new expires_at_ms=%llu",
                     stream.stream_instance_id, (unsigned long long)expires_at_ms);
        }

        int len = ctl_build_ack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, now_ms);
        if (len > 0) {
            dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
            *out_resp_len = (size_t)len;
            *p_should_publish = true;
            return true;
        }
        return false;
    }

    /* 4. Remote input commands: pointer_move, mouse_click, key_event */
    bool is_move = (strcmp(cmd_type, "pointer_move") == 0);
    bool is_click = (strcmp(cmd_type, "mouse_click") == 0);
    bool is_key = (strcmp(cmd_type, "key_event") == 0);

    if (is_move || is_click || is_key) {
        char desktop_id[64] = { 0 };
        char stream_instance_id[64] = { 0 };
        json_extract_str(payload, "desktop_id", desktop_id, sizeof(desktop_id));
        json_extract_str(payload, "stream_instance_id", stream_instance_id, sizeof(stream_instance_id));

        StreamStateInfo stream;
        ffmpeg_supervisor_get_info(&stream);

        /* Validate active stream */
        if (strcmp(stream.state, "running") != 0) {
            log_warn("Input rejected: stream is not running (state=%s)", stream.state);
            if (!is_move) {
                int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                                 "stream_mismatch", "Stream is not running", now_ms);
                if (len > 0) {
                    dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
                    *out_resp_len = (size_t)len;
                    *p_should_publish = true;
                    return true;
                }
            }
            return false;
        }

        if (_stricmp(stream.mode, "desktop") != 0) {
            log_warn("Input rejected: stream mode is not desktop (mode=%s)", stream.mode);
            if (!is_move) {
                int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                                 "input_not_allowed_in_camera_mode",
                                                 "Input is not allowed in camera mode", now_ms);
                if (len > 0) {
                    dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
                    *out_resp_len = (size_t)len;
                    *p_should_publish = true;
                    return true;
                }
            }
            return false;
        }

        if (stream.lease_id[0] != '\0' && (lease_id[0] == '\0' || strcmp(stream.lease_id, lease_id) != 0)) {
            log_warn("Input rejected: lease_mismatch (active: %s, cmd: %s)", stream.lease_id, lease_id);
            if (!is_move) {
                int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                                 "lease_mismatch", "Lease ID mismatch", now_ms);
                if (len > 0) {
                    dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
                    *out_resp_len = (size_t)len;
                    *p_should_publish = true;
                    return true;
                }
            }
            return false;
        }

        if (stream.source_id[0] != '\0' && desktop_id[0] != '\0' && strcmp(stream.source_id, desktop_id) != 0) {
            log_warn("Input rejected: desktop_mismatch (active: %s, cmd: %s)", stream.source_id, desktop_id);
            if (!is_move) {
                int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                                 "desktop_mismatch", "Desktop ID mismatch", now_ms);
                if (len > 0) {
                    dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
                    *out_resp_len = (size_t)len;
                    *p_should_publish = true;
                    return true;
                }
            }
            return false;
        }

        if (stream.stream_instance_id[0] != '\0' && stream_instance_id[0] != '\0' &&
            strcmp(stream.stream_instance_id, stream_instance_id) != 0) {
            log_warn("Input rejected: stream_mismatch (active: %s, cmd: %s)",
                     stream.stream_instance_id, stream_instance_id);
            if (!is_move) {
                int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                                 "stream_mismatch", "Stream instance mismatch", now_ms);
                if (len > 0) {
                    dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
                    *out_resp_len = (size_t)len;
                    *p_should_publish = true;
                    return true;
                }
            }
            return false;
        }

        /* Check display policy */
        if (inv) {
            DisplayInfo d;
            if (inventory_find_display(inv, stream.source_id, &d)) {
                if (_stricmp(d.policy, "input") != 0) {
                    log_warn("Input rejected: display policy is %s (not input)", d.policy);
                    if (!is_move) {
                        int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                                         "source_not_allowed", "Display policy denies input", now_ms);
                        if (len > 0) {
                            dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
                            *out_resp_len = (size_t)len;
                            *p_should_publish = true;
                            return true;
                        }
                    }
                    return false;
                }
            }
        }

        /* Refresh local lease watchdog on valid control input */
        if (expires_at_ms > 0) {
            ffmpeg_supervisor_update_lease(stream.lease_id, (uint64_t)expires_at_ms);
        }

        /* Handle pointer_move / mouse_click */
        if (is_move || is_click) {
            double nx = 0.0, ny = 0.0;
            bool has_x = json_extract_double(payload, "x", &nx);
            bool has_y = json_extract_double(payload, "y", &ny);

            if (!has_x || !has_y) {
                int ix = 0, iy = 0;
                if (json_extract_int(payload, "x", &ix) && json_extract_int(payload, "y", &iy)) {
                    nx = (double)ix;
                    ny = (double)iy;
                    has_x = true;
                    has_y = true;
                }
            }

            if (!has_x || !has_y) {
                if (is_click) {
                    int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                                     "invalid_payload", "Missing coordinates", now_ms);
                    if (len > 0) {
                        *out_resp_len = (size_t)len;
                        *p_should_publish = true;
                        return true;
                    }
                }
                return false;
            }

            /* Normalize legacy 0..65535 or coords > 1.0 */
            if (nx > 1.0 || ny > 1.0) {
                nx /= 65535.0;
                ny /= 65535.0;
            }

            int rect_w = stream.desktop_rect.right - stream.desktop_rect.left;
            int rect_h = stream.desktop_rect.bottom - stream.desktop_rect.top;
            if (rect_w <= 0) rect_w = 1920;
            if (rect_h <= 0) rect_h = 1080;

            DWORD err = 0;
            if (is_move) {
                input_inject_move_norm(nx, ny, stream.desktop_rect.left, stream.desktop_rect.top,
                                       rect_w, rect_h, &err);
                *p_should_publish = false;
                return true;
            } else {
                char button[16] = "left";
                json_extract_str(payload, "button", button, sizeof(button));

                bool ok = input_inject_click_norm(nx, ny, button,
                                                  stream.desktop_rect.left, stream.desktop_rect.top,
                                                  rect_w, rect_h, &err);
                int len = 0;
                if (ok) {
                    len = ctl_build_ack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, now_ms);
                } else {
                    len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                                 "inject_failed", "Click injection failed", now_ms);
                }
                if (len > 0) {
                    dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
                    *out_resp_len = (size_t)len;
                    *p_should_publish = true;
                    return true;
                }
                return false;
            }
        }

        /* Handle key_event */
        if (is_key) {
            char kind[16] = "press";
            int vk = 0;
            char text[128] = { 0 };

            json_extract_str(payload, "kind", kind, sizeof(kind));
            json_extract_int(payload, "vk", &vk);
            json_extract_str(payload, "text", text, sizeof(text));

            if (vk > 0 && !input_is_vk_allowed(vk)) {
                log_warn("Key rejected: vk %d is forbidden or not whitelisted", vk);
                int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                                 "source_not_allowed", "Virtual key not allowed", now_ms);
                if (len > 0) {
                    dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
                    *out_resp_len = (size_t)len;
                    *p_should_publish = true;
                    return true;
                }
                return false;
            }

            DWORD err = 0;
            bool ok = input_inject_key(kind, vk, text[0] != '\0' ? text : NULL, &err);
            int len = 0;
            if (ok) {
                len = ctl_build_ack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn, now_ms);
            } else {
                len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                             "inject_failed", "Key injection failed", now_ms);
            }
            if (len > 0) {
                dedup_cache_put(cmd_id, out_resp, (size_t)len, expires_at_ms);
                *out_resp_len = (size_t)len;
                *p_should_publish = true;
                return true;
            }
            return false;
        }
    }

    /* Unsupported command */
    log_warn("Unsupported command type: '%s'", cmd_type);
    int len = ctl_build_nack_payload(out_resp, max_resp, cmd_id, lease_id, own_sn,
                                     "unsupported", "Unsupported command type", now_ms);
    if (len > 0) {
        *out_resp_len = (size_t)len;
        *p_should_publish = true;
        return true;
    }

    return false;
}
