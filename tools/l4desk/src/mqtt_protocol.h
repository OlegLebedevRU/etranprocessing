#ifndef L4DESK_MQTT_PROTOCOL_H
#define L4DESK_MQTT_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MQTT_PKT_CONNECT     0x10
#define MQTT_PKT_CONNACK     0x20
#define MQTT_PKT_PUBLISH     0x30
#define MQTT_PKT_PUBACK      0x40
#define MQTT_PKT_SUBSCRIBE   0x82
#define MQTT_PKT_SUBACK      0x90
#define MQTT_PKT_PINGREQ     0xC0
#define MQTT_PKT_PINGRESP    0xD0
#define MQTT_PKT_DISCONNECT  0xE0

#define MQTT_FLAG_CLEAN_SESSION 0x02
#define MQTT_FLAG_WILL_FLAG     0x04
#define MQTT_FLAG_WILL_QOS1     0x08
#define MQTT_FLAG_WILL_RETAIN   0x20
#define MQTT_FLAG_USERNAME      0x80

int mqtt_encode_remaining_length(unsigned char* buf, uint32_t length);
int mqtt_decode_remaining_length(const unsigned char* buf, size_t buf_len, uint32_t* out_length, int* out_bytes_used);

int mqtt_build_connect(unsigned char* buf, size_t max_len,
                       const char* client_id,
                       const char* will_topic,
                       const char* will_payload,
                       const char* username,
                       uint16_t keepalive_sec);

int mqtt_build_publish(unsigned char* buf, size_t max_len,
                       const char* topic,
                       const void* payload,
                       size_t payload_len,
                       uint16_t packet_id,
                       uint8_t qos,
                       uint8_t retain);

int mqtt_build_subscribe(unsigned char* buf, size_t max_len,
                         const char* topic,
                         uint16_t packet_id,
                         uint8_t qos);

int mqtt_build_puback(unsigned char* buf, uint16_t packet_id);
int mqtt_build_pingreq(unsigned char* buf);
int mqtt_build_disconnect(unsigned char* buf);

int mqtt_parse_publish(const unsigned char* var_header_and_payload,
                       uint32_t rem_len,
                       uint8_t pkt_flags,
                       char* out_topic, size_t max_topic_len,
                       uint16_t* out_packet_id,
                       const char** out_payload,
                       size_t* out_payload_len);

#endif /* L4DESK_MQTT_PROTOCOL_H */
