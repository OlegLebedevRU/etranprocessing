#include "../src/mqtt_protocol.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); failures++; \
} } while (0)

static void test_connect(void) {
    unsigned char packet[256];
    int len = mqtt_build_connect(packet, sizeof(packet), "test_extra",
                                 "dev/test/svc", "svc_offline",
                                 "extra_service", 30);
    CHECK(len > 0 && packet[0] == MQTT_PKT_CONNECT);
    uint32_t remaining = 0;
    int length_bytes = 0;
    CHECK(mqtt_decode_remaining_length(packet + 1, (size_t)len - 1,
                                       &remaining, &length_bytes) == 0);
    const unsigned char* header = packet + 1 + length_bytes;
    CHECK(header[6] == 5); // MQTT protocol level
    CHECK(header[10] == 0); // CONNECT property length
    size_t client_id_size = strlen("test_extra");
    CHECK(header[11] == 0 && header[12] == client_id_size);
    CHECK(header[13 + client_id_size] == 0); // Will property length
}

static void test_publish_properties_and_parse(void) {
    const MqttUserProperty properties[] = {
        { "event_type_code", "75" },
        { "dev_event_id", "123" },
    };
    const char* payload = "{\"200\":75}";
    unsigned char packet[512];
    int len = mqtt_build_publish_with_properties(packet, sizeof(packet),
                                                  "dev/test/evt", payload,
                                                  strlen(payload), 41, 1, 0,
                                                  properties, 2);
    CHECK(len > 0 && packet[0] == 0x32);
    uint32_t remaining = 0;
    int length_bytes = 0;
    CHECK(mqtt_decode_remaining_length(packet + 1, (size_t)len - 1,
                                       &remaining, &length_bytes) == 0);
    const unsigned char* body = packet + 1 + length_bytes;
    size_t pos = 2 + strlen("dev/test/evt") + 2;
    uint32_t property_len = 0;
    int property_len_bytes = 0;
    CHECK(mqtt_decode_remaining_length(body + pos, remaining - pos,
                                       &property_len, &property_len_bytes) == 0);
    pos += (size_t)property_len_bytes;
    CHECK(property_len > 0 && body[pos] == 0x26);
    CHECK(memcmp(body + pos + 3, "event_type_code", 15) == 0);

    char topic[64];
    uint16_t packet_id = 0;
    const char* parsed_payload = NULL;
    size_t payload_len = 0;
    CHECK(mqtt_parse_publish(body, remaining, 2, topic, sizeof(topic),
                             &packet_id, &parsed_payload, &payload_len) == 0);
    CHECK(strcmp(topic, "dev/test/evt") == 0 && packet_id == 41);
    CHECK(payload_len == strlen(payload) &&
          memcmp(parsed_payload, payload, payload_len) == 0);
    CHECK(mqtt_parse_publish(body, (uint32_t)(pos + property_len - 1), 2,
                             topic, sizeof(topic), &packet_id,
                             &parsed_payload, &payload_len) == -1);
}

static void test_subscribe(void) {
    unsigned char packet[128];
    int len = mqtt_build_subscribe(packet, sizeof(packet), "srv/test/tsk", 7, 1);
    CHECK(len > 0 && packet[0] == MQTT_PKT_SUBSCRIBE);
    CHECK(packet[2] == 0 && packet[3] == 7 && packet[4] == 0);
}
static void test_probe_expiry(void){
    unsigned char packet[512];const MqttUserProperty properties[]={{"iot_probe","1"},{"correlationData","22222222-2222-4222-8222-222222222222"}};
    int length=mqtt_build_publish_expiring(packet,sizeof(packet),"dev/test/req","{}",2,1,1,0,properties,2,10);
    CHECK(length>0 && packet[0]==0x32);uint32_t remaining=0;int used=0;
    CHECK(!mqtt_decode_remaining_length(packet+1,length-1,&remaining,&used));
    const unsigned char* body=packet+1+used;size_t offset=2+strlen("dev/test/req")+2;uint32_t properties_size=0;
    CHECK(!mqtt_decode_remaining_length(body+offset,remaining-(uint32_t)offset,&properties_size,&used));offset+=(size_t)used;
    CHECK(properties_size>=5 && body[offset]==2 && !body[offset+1] && !body[offset+2] && !body[offset+3] && body[offset+4]==10);
    MqttRpcMetadata metadata;CHECK(!mqtt_parse_rpc_metadata(body,remaining,2,&metadata));
    CHECK(!strcmp(metadata.iot_probe,"1") && !strcmp(metadata.correlation,"22222222-2222-4222-8222-222222222222"));
}

int main(void) {
    test_connect();
    test_publish_properties_and_parse();
    test_subscribe();
    test_probe_expiry();
    if (failures) return 1;
    puts("MQTT 5 protocol tests passed");
    return 0;
}
