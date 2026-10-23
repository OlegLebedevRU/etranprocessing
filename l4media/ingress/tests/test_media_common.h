#ifndef TEST_MEDIA_COMMON_H
#define TEST_MEDIA_COMMON_H
static json_t* fake_mounts;
static int fake_failure, creates, destroys;
static bool fake_chunked;
static int fake_janus(const char* body, char* output, size_t size) {
    if (fake_failure) return -1;
    json_error_t error;
    json_t* envelope = json_loads(body, 0, &error);
    assert(envelope);
    json_t* request = json_object_get(envelope, "request");
    const char* verb = mv_string(request, "request");
    char key[32]; snprintf(key, sizeof(key), "%lld", mv_integer(request, "id"));
    json_t* reply = NULL;
    if (!strcmp(verb, "create")) {
        assert(!json_object_get(fake_mounts, key));
        json_object_set(fake_mounts, key, request); creates++;
        reply = json_pack("{s:s}", "streaming", "created");
    } else if (!strcmp(verb, "info")) {
        json_t* mount = json_object_get(fake_mounts, key);
        if (mount) reply = json_pack("{s:s,s:{s:s,s:[{s:s,s:I,s:I,s:I,s:s,s:s}]}}",
            "streaming", "info", "info", "pin", mv_string(mount, "pin"),
            "media", "type", "video", "port", (json_int_t)mv_integer(mount, "videoport"),
            "rtcpport", (json_int_t)mv_integer(mount, "videortcpport"),
            "pt", (json_int_t)mv_integer(mount, "videopt"), "codec", mv_string(mount, "videocodec"), "fmtp", mv_string(mount, "videofmtp"));
        else reply = json_pack("{s:i}", "error_code", 455);
    } else if (!strcmp(verb, "destroy")) {
        json_object_del(fake_mounts, key); destroys++;
        reply = json_pack("{s:s}", "streaming", "destroyed");
    } else if (!strcmp(verb, "list")) reply = json_pack("{s:s,s:[]}", "streaming", "list", "list");
    assert(reply);
    char* text = json_dumps(reply, JSON_COMPACT);
    if (fake_chunked) snprintf(output, size, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n%zx\r\n%s\r\n0\r\n\r\n", strlen(text), text);
    else snprintf(output, size, "HTTP/1.1 200 OK\r\n\r\n%s", text);
    free(text); json_decref(reply); json_decref(envelope); return 0;
}

static json_t* fixture(const char* name) {
    char path[256]; snprintf(path, sizeof(path), "spec/fixtures/%s.json", name);
    json_error_t error; json_t* wrapper = json_load_file(path, JSON_REJECT_DUPLICATES, &error);
    assert(wrapper); json_t* value = json_deep_copy(json_object_get(wrapper, "payload"));
    json_decref(wrapper); return value;
}
static void request(const char* endpoint, json_t* value, int expected, char* output) {
    char* body = json_dumps(value, JSON_COMPACT);
    int status = 0;
    media_v2_http("POST", endpoint, body, output, 16384, &status);
    free(body);
    if (status != expected) fprintf(stderr, "Unexpected HTTP %d (expected %d): %s\n", status, expected, output);
    assert(status == expected);
    json_error_t error; json_t* response = json_loads(output, 0, &error);
    const char* definition = status >= 400 ? "rejection" : "media_response";
    assert(ms_validate(json_object_get(json_object_get(g_media_schema, "$defs"), definition), response, 0));
    json_decref(response);
}

#endif
