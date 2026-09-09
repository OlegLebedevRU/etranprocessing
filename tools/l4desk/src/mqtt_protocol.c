#include "mqtt_protocol.h"
#include <string.h>

int mqtt_encode_remaining_length(unsigned char* buf, uint32_t length) {
    int bytes = 0;
    do {
        unsigned char encodedByte = (unsigned char)(length % 128);
        length /= 128;
        if (length > 0) {
            encodedByte |= 128;
        }
        buf[bytes++] = encodedByte;
    } while (length > 0);
    return bytes;
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

    uint8_t flags = MQTT_FLAG_CLEAN_SESSION;
    if (will_topic && will_payload) {
        flags |= MQTT_FLAG_WILL_FLAG | MQTT_FLAG_WILL_QOS1 | MQTT_FLAG_WILL_RETAIN;
    }
    if (username && strlen(username) > 0) {
        flags |= MQTT_FLAG_USERNAME;
    }
    payload_buf[p_pos++] = flags;

    // Keepalive
    payload_buf[p_pos++] = (unsigned char)((keepalive_sec >> 8) & 0xFF);
    payload_buf[p_pos++] = (unsigned char)(keepalive_sec & 0xFF);

    // Payload:
    // 1. Client Identifier
    p_pos += write_utf8_string(payload_buf + p_pos, client_id ? client_id : "");

    // 2. Will Topic & Payload
    if (will_topic && will_payload) {
        p_pos += write_utf8_string(payload_buf + p_pos, will_topic);
        p_pos += write_utf8_string(payload_buf + p_pos, will_payload);
    }

    // 3. Username
    if (username && strlen(username) > 0) {
        p_pos += write_utf8_string(payload_buf + p_pos, username);
    }

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
    buf[1] = 2; // remaining length = 2 bytes
    buf[2] = (unsigned char)((packet_id >> 8) & 0xFF);
    buf[3] = (unsigned char)(packet_id & 0xFF);
    return 4;
}

int mqtt_build_pingreq(unsigned char* buf) {
    buf[0] = (unsigned char)MQTT_PKT_PINGREQ;
    buf[1] = 0;
    return 2;
}

int mqtt_build_disconnect(unsigned char* buf) {
    buf[0] = (unsigned char)MQTT_PKT_DISCONNECT;
    buf[1] = 0;
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

    size_t topic_len = ((size_t)var_header_and_payload[0] << 8) | var_header_and_payload[1];
    if (rem_len < 2 + topic_len) return -1;

    if (out_topic && max_topic_len > 0) {
        size_t cpy_len = topic_len < max_topic_len - 1 ? topic_len : max_topic_len - 1;
        memcpy(out_topic, var_header_and_payload + 2, cpy_len);
        out_topic[cpy_len] = '\0';
    }

    size_t offset = 2 + topic_len;
    uint8_t qos = (pkt_flags >> 1) & 0x03;

    if (qos > 0) {
        if (rem_len < offset + 2) return -1;
        if (out_packet_id) {
            *out_packet_id = ((uint16_t)var_header_and_payload[offset] << 8) | var_header_and_payload[offset + 1];
        }
        offset += 2;
    } else if (out_packet_id) {
        *out_packet_id = 0;
    }

    if (out_payload) {
        *out_payload = (const char*)(var_header_and_payload + offset);
    }
    if (out_payload_len) {
        *out_payload_len = rem_len - offset;
    }

    return 0;
}
