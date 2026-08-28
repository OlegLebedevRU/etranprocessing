#include "mqtt_protocol.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

int mqtt_encode_remaining_length(unsigned char* buf, uint32_t length) {
    int count = 0;
    do {
        unsigned char d = (unsigned char)(length % 128);
        length /= 128;
        if (length > 0) {
            d |= 128;
        }
        buf[count++] = d;
    } while (length > 0);
    return count;
}

int mqtt_decode_remaining_length(const unsigned char* buf, size_t buf_len, uint32_t* out_length, int* out_bytes_used) {
    uint32_t multiplier = 1;
    uint32_t value = 0;
    int bytes = 0;
    unsigned char encodedByte;

    do {
        if ((size_t)bytes >= buf_len || bytes >= 4) {
            return -1; // Incomplete or invalid
        }
        encodedByte = buf[bytes++];
        value += (encodedByte & 127) * multiplier;
        if (multiplier > 128 * 128 * 128) {
            return -1; // Malformed Remaining Length
        }
        multiplier *= 128;
    } while ((encodedByte & 128) != 0);

    *out_length = value;
    *out_bytes_used = bytes;
    return 0;
}

static int write_utf8_string(unsigned char* buf, const char* str) {
    size_t len = strlen(str);
    buf[0] = (unsigned char)((len >> 8) & 0xFF);
    buf[1] = (unsigned char)(len & 0xFF);
    memcpy(buf + 2, str, len);
    return (int)(2 + len);
}

int mqtt_build_connect(unsigned char* buf, size_t max_len,
                       const char* client_id,
                       const char* will_topic,
                       const char* will_payload,
                       const char* username,
                       uint16_t keepalive_sec) {
    unsigned char payload_buf[2048];
    int p_pos = 0;

    // Protocol Name: "MQTT"
    p_pos += write_utf8_string(payload_buf + p_pos, "MQTT");
    // Protocol Level: 4 (MQTT 3.1.1)
    payload_buf[p_pos++] = 4;
    // Flags: UserName | WillRetain | WillQoS1 | WillFlag | CleanSession
    payload_buf[p_pos++] = MQTT_FLAG_USERNAME | MQTT_FLAG_WILL_RETAIN | MQTT_FLAG_WILL_QOS1 | MQTT_FLAG_WILL_FLAG | MQTT_FLAG_CLEAN_SESSION;
    // Keepalive
    payload_buf[p_pos++] = (unsigned char)((keepalive_sec >> 8) & 0xFF);
    payload_buf[p_pos++] = (unsigned char)(keepalive_sec & 0xFF);

    // Payload:
    // 1. Client Identifier
    p_pos += write_utf8_string(payload_buf + p_pos, client_id);
    // 2. Will Topic
    p_pos += write_utf8_string(payload_buf + p_pos, will_topic);
    // 3. Will Payload
    p_pos += write_utf8_string(payload_buf + p_pos, will_payload);
    // 4. Username
    p_pos += write_utf8_string(payload_buf + p_pos, username);

    int out_pos = 0;
    buf[out_pos++] = (unsigned char)MQTT_PKT_CONNECT;
    out_pos += mqtt_encode_remaining_length(buf + out_pos, (uint32_t)p_pos);

    if ((size_t)(out_pos + p_pos) > max_len) return -1;
    memcpy(buf + out_pos, payload_buf, p_pos);
    return out_pos + p_pos;
}

int mqtt_build_publish(unsigned char* buf, size_t max_len,
                       const char* topic,
                       const void* payload,
                       size_t payload_len,
                       uint16_t packet_id,
                       uint8_t qos,
                       uint8_t retain) {
    size_t topic_len = strlen(topic);
    size_t var_header_len = 2 + topic_len + (qos > 0 ? 2 : 0);
    size_t rem_len = var_header_len + payload_len;

    uint8_t header_byte = (uint8_t)(MQTT_PKT_PUBLISH | ((qos & 0x03) << 1) | (retain & 0x01));

    int out_pos = 0;
    buf[out_pos++] = header_byte;
    out_pos += mqtt_encode_remaining_length(buf + out_pos, (uint32_t)rem_len);

    if ((size_t)(out_pos + rem_len) > max_len) return -1;

    // Variable header: topic
    out_pos += write_utf8_string(buf + out_pos, topic);
    if (qos > 0) {
        buf[out_pos++] = (unsigned char)((packet_id >> 8) & 0xFF);
        buf[out_pos++] = (unsigned char)(packet_id & 0xFF);
    }

    // Payload
    if (payload && payload_len > 0) {
        memcpy(buf + out_pos, payload, payload_len);
        out_pos += (int)payload_len;
    }

    return out_pos;
}

int mqtt_build_subscribe(unsigned char* buf, size_t max_len,
                         const char* topic,
                         uint16_t packet_id,
                         uint8_t qos) {
    size_t topic_len = strlen(topic);
    size_t var_payload_len = 2 + (2 + topic_len) + 1; // packet_id (2) + topic (2+len) + req_qos (1)

    int out_pos = 0;
    buf[out_pos++] = (unsigned char)MQTT_PKT_SUBSCRIBE;
    out_pos += mqtt_encode_remaining_length(buf + out_pos, (uint32_t)var_payload_len);

    if ((size_t)(out_pos + var_payload_len) > max_len) return -1;

    buf[out_pos++] = (unsigned char)((packet_id >> 8) & 0xFF);
    buf[out_pos++] = (unsigned char)(packet_id & 0xFF);

    out_pos += write_utf8_string(buf + out_pos, topic);
    buf[out_pos++] = qos & 0x03;

    return out_pos;
}

int mqtt_build_puback(unsigned char* buf, uint16_t packet_id) {
    buf[0] = (unsigned char)MQTT_PKT_PUBACK;
    buf[1] = 0x02;
    buf[2] = (unsigned char)((packet_id >> 8) & 0xFF);
    buf[3] = (unsigned char)(packet_id & 0xFF);
    return 4;
}

int mqtt_build_pingreq(unsigned char* buf) {
    buf[0] = (unsigned char)MQTT_PKT_PINGREQ;
    buf[1] = 0x00;
    return 2;
}

int mqtt_build_disconnect(unsigned char* buf) {
    buf[0] = (unsigned char)MQTT_PKT_DISCONNECT;
    buf[1] = 0x00;
    return 2;
}

int mqtt_parse_publish(const unsigned char* var_header_and_payload,
                       uint32_t rem_len,
                       uint8_t pkt_flags,
                       char* out_topic, size_t max_topic_len,
                       uint16_t* out_packet_id,
                       const char** out_payload,
                       size_t* out_payload_len) {
    if (rem_len < 2) return -1;
    uint16_t topic_len = (uint16_t)((var_header_and_payload[0] << 8) | var_header_and_payload[1]);
    if ((size_t)(2 + topic_len) > rem_len) return -1;

    if (topic_len >= max_topic_len) return -1;
    memcpy(out_topic, var_header_and_payload + 2, topic_len);
    out_topic[topic_len] = '\0';

    size_t pos = 2 + topic_len;
    uint8_t qos = (pkt_flags >> 1) & 0x03;
    if (qos > 0) {
        if (pos + 2 > rem_len) return -1;
        if (out_packet_id) {
            *out_packet_id = (uint16_t)((var_header_and_payload[pos] << 8) | var_header_and_payload[pos + 1]);
        }
        pos += 2;
    } else {
        if (out_packet_id) *out_packet_id = 0;
    }

    *out_payload = (const char*)(var_header_and_payload + pos);
    *out_payload_len = rem_len - pos;
    return 0;
}

void json_escape_string(const char* src, size_t src_len, char* dst, size_t dst_max_len) {
    if (!dst || dst_max_len == 0) return;
    size_t dst_pos = 0;

    for (size_t i = 0; i < src_len && src[i] != '\0'; i++) {
        unsigned char c = (unsigned char)src[i];
        if (dst_pos + 6 >= dst_max_len) break; // Ensure room for \u00XX

        switch (c) {
            case '\"': dst[dst_pos++] = '\\'; dst[dst_pos++] = '\"'; break;
            case '\\': dst[dst_pos++] = '\\'; dst[dst_pos++] = '\\'; break;
            case '\b': dst[dst_pos++] = '\\'; dst[dst_pos++] = 'b'; break;
            case '\f': dst[dst_pos++] = '\\'; dst[dst_pos++] = 'f'; break;
            case '\n': dst[dst_pos++] = '\\'; dst[dst_pos++] = 'n'; break;
            case '\r': dst[dst_pos++] = '\\'; dst[dst_pos++] = 'r'; break;
            case '\t': dst[dst_pos++] = '\\'; dst[dst_pos++] = 't'; break;
            default:
                if (c < 0x20) {
                    snprintf(dst + dst_pos, 7, "\\u%04x", c);
                    dst_pos += 6;
                } else {
                    dst[dst_pos++] = (char)c;
                }
                break;
        }
    }
    dst[dst_pos] = '\0';
}

static const char* find_key(const char* json, const char* key) {
    if (!json || !key) return NULL;
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char* p = json;
    while ((p = strstr(p, pattern)) != NULL) {
        // Check if preceded by non-alnum/non-quote
        const char* colon = p + strlen(pattern);
        while (*colon && (isspace((unsigned char)*colon) || *colon == '\r' || *colon == '\n')) {
            colon++;
        }
        if (*colon == ':') {
            return colon + 1;
        }
        p += strlen(pattern);
    }
    return NULL;
}

bool json_extract_string(const char* json, const char* key, char* out_val, size_t out_max_len) {
    if (!json || !key || !out_val || out_max_len == 0) return false;
    out_val[0] = '\0';

    const char* val_start = find_key(json, key);
    if (!val_start) return false;

    while (*val_start && isspace((unsigned char)*val_start)) val_start++;
    if (*val_start != '\"') return false;
    val_start++; // Skip opening quote

    size_t out_idx = 0;
    const char* p = val_start;
    while (*p && *p != '\"' && out_idx + 1 < out_max_len) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case '\"': out_val[out_idx++] = '\"'; break;
                case '\\': out_val[out_idx++] = '\\'; break;
                case 'n':  out_val[out_idx++] = '\n'; break;
                case 'r':  out_val[out_idx++] = '\r'; break;
                case 't':  out_val[out_idx++] = '\t'; break;
                default:   out_val[out_idx++] = *p; break;
            }
        } else {
            out_val[out_idx++] = *p;
        }
        p++;
    }
    out_val[out_idx] = '\0';
    return true;
}

bool json_extract_int(const char* json, const char* key, int* out_val) {
    if (!json || !key || !out_val) return false;

    const char* val_start = find_key(json, key);
    if (!val_start) return false;

    while (*val_start && isspace((unsigned char)*val_start)) val_start++;
    if (*val_start == '\"') val_start++; // Handle stringified int

    char* endptr = NULL;
    long val = strtol(val_start, &endptr, 10);
    if (endptr != val_start) {
        *out_val = (int)val;
        return true;
    }
    return false;
}

bool json_extract_bool(const char* json, const char* key, bool* out_val) {
    if (!json || !key || !out_val) return false;

    const char* val_start = find_key(json, key);
    if (!val_start) return false;

    while (*val_start && isspace((unsigned char)*val_start)) val_start++;
    if (strncmp(val_start, "true", 4) == 0) {
        *out_val = true;
        return true;
    } else if (strncmp(val_start, "false", 5) == 0) {
        *out_val = false;
        return true;
    }
    return false;
}
