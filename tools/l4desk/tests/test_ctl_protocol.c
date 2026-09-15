#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "ctl_protocol.h"
#include "json_min.h"
#include "display_inventory.h"
#include "input_inject.h"
#include "dedup_cache.h"
#include "ffmpeg_cmdline.h"

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("[FAIL] Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

static void test_json_min(void) {
    const char* json = "{\"str\":\"hello world\",\"num\":42,\"big\":1234567890123,\"flt\":0.75,\"b\":true,\"f\":false}";
    char s[32];
    int n = 0;
    int64_t b = 0;
    double d = 0.0;
    bool bt = false, bf = true;

    ASSERT_TRUE(json_extract_str(json, "str", s, sizeof(s)));
    ASSERT_TRUE(strcmp(s, "hello world") == 0);

    ASSERT_TRUE(json_extract_int(json, "num", &n));
    ASSERT_TRUE(n == 42);

    ASSERT_TRUE(json_extract_int64(json, "big", &b));
    ASSERT_TRUE(b == 1234567890123LL);

    ASSERT_TRUE(json_extract_double(json, "flt", &d));
    ASSERT_TRUE(d > 0.74 && d < 0.76);

    ASSERT_TRUE(json_extract_bool(json, "b", &bt));
    ASSERT_TRUE(bt == true);

    ASSERT_TRUE(json_extract_bool(json, "f", &bf));
    ASSERT_TRUE(bf == false);

    printf("[PASS] test_json_min\n");
}

static void test_fnv1a_stability(void) {
    const char* str1 = "\\\\?\\DISPLAY#MONITOR1#INTERFACE";
    const char* str2 = "@device:pnp:\\\\?\\usb#vid_046d&pid_0825#camera1";

    uint32_t h1 = fnv1a_32_str(str1);
    uint32_t h2 = fnv1a_32_str(str2);

    ASSERT_TRUE(h1 != 0);
    ASSERT_TRUE(h2 != 0);

    /* Verify stability across calls */
    ASSERT_TRUE(h1 == fnv1a_32_str(str1));
    ASSERT_TRUE(h2 == fnv1a_32_str(str2));

    char id1[32], id2[32];
    snprintf(id1, sizeof(id1), "disp:%08x", h1);
    snprintf(id2, sizeof(id2), "cam:%08x", h2);

    ASSERT_TRUE(strncmp(id1, "disp:", 5) == 0);
    ASSERT_TRUE(strncmp(id2, "cam:", 4) == 0);
    ASSERT_TRUE(strlen(id1) == 13);
    ASSERT_TRUE(strlen(id2) == 12);

    printf("[PASS] test_fnv1a_stability (%s, %s)\n", id1, id2);
}

static void test_mouse_mapping(void) {
    int nx = 0, ny = 0;

    /* 1. Single primary screen: (0, 0, 1920, 1080), Virtual: (0, 0, 1920, 1080) */
    input_map_coordinates_custom(0.0, 0.0, 0, 0, 1920, 1080, 0, 0, 1920, 1080, &nx, &ny);
    ASSERT_TRUE(nx == 0 && ny == 0);

    input_map_coordinates_custom(1.0, 1.0, 0, 0, 1920, 1080, 0, 0, 1920, 1080, &nx, &ny);
    ASSERT_TRUE(nx == 65535 && ny == 65535);

    input_map_coordinates_custom(0.5, 0.5, 0, 0, 1920, 1080, 0, 0, 1920, 1080, &nx, &ny);
    ASSERT_TRUE(nx >= 32700 && nx <= 32800);
    ASSERT_TRUE(ny >= 32700 && ny <= 32800);

    /* 2. Dual monitor with negative origin:
          Left monitor: (-1920, 0, 1920, 1080)
          Primary monitor: (0, 0, 1920, 1080)
          Virtual desk: (-1920, 0, 3840, 1080) */
    input_map_coordinates_custom(0.0, 0.0, -1920, 0, 1920, 1080, -1920, 0, 3840, 1080, &nx, &ny);
    ASSERT_TRUE(nx == 0 && ny == 0);

    input_map_coordinates_custom(1.0, 0.5, -1920, 0, 1920, 1080, -1920, 0, 3840, 1080, &nx, &ny);
    /* x maps to -1920 + 1920 = 0. In virtual desk (-1920..1920, width 3840), 0 is middle (0.5) */
    ASSERT_TRUE(nx >= 32700 && nx <= 32800);
    ASSERT_TRUE(ny >= 32700 && ny <= 32800);

    /* 3. Out-of-bounds coordinates clamp */
    input_map_coordinates_custom(-0.5, 70000.0, 0, 0, 1920, 1080, 0, 0, 1920, 1080, &nx, &ny);
    ASSERT_TRUE(nx == 0 && ny == 65535);

    /* 4. Integer / float 0..65535 scale mapping */
    input_map_coordinates_custom(32768.0, 32768.0, 0, 0, 1920, 1080, 0, 0, 1920, 1080, &nx, &ny);
    ASSERT_TRUE(nx >= 32700 && nx <= 32800);
    ASSERT_TRUE(ny >= 32700 && ny <= 32800);

    input_map_coordinates_custom(65535.0, 65535.0, 0, 0, 1920, 1080, 0, 0, 1920, 1080, &nx, &ny);
    ASSERT_TRUE(nx == 65535 && ny == 65535);

    printf("[PASS] test_mouse_mapping\n");
}

static void test_keyboard_whitelist(void) {
    /* Allowed keys */
    ASSERT_TRUE(input_is_vk_allowed('A'));
    ASSERT_TRUE(input_is_vk_allowed('Z'));
    ASSERT_TRUE(input_is_vk_allowed('0'));
    ASSERT_TRUE(input_is_vk_allowed('9'));
    ASSERT_TRUE(input_is_vk_allowed(VK_RETURN));
    ASSERT_TRUE(input_is_vk_allowed(VK_ESCAPE));
    ASSERT_TRUE(input_is_vk_allowed(VK_TAB));
    ASSERT_TRUE(input_is_vk_allowed(VK_BACK));
    ASSERT_TRUE(input_is_vk_allowed(VK_SPACE));
    ASSERT_TRUE(input_is_vk_allowed(VK_LEFT));
    ASSERT_TRUE(input_is_vk_allowed(VK_RIGHT));
    ASSERT_TRUE(input_is_vk_allowed(VK_UP));
    ASSERT_TRUE(input_is_vk_allowed(VK_DOWN));
    ASSERT_TRUE(input_is_vk_allowed(VK_F1));
    /* By default F12 is disallowed by policy */
    ASSERT_TRUE(!input_is_vk_allowed(VK_F12));
    input_set_shortcut_policy(true, false, false, NULL);
    ASSERT_TRUE(input_is_vk_allowed(VK_F12));
    input_set_shortcut_policy(false, false, false, NULL);
    ASSERT_TRUE(!input_is_vk_allowed(VK_F12));
    ASSERT_TRUE(input_is_vk_allowed(VK_DELETE));
    ASSERT_TRUE(input_is_vk_allowed(VK_INSERT));

    /* Forbidden keys */
    ASSERT_TRUE(!input_is_vk_allowed(VK_LWIN));
    ASSERT_TRUE(!input_is_vk_allowed(VK_RWIN));
    ASSERT_TRUE(!input_is_vk_allowed(VK_APPS));
    ASSERT_TRUE(!input_is_vk_allowed(0));
    ASSERT_TRUE(!input_is_vk_allowed(0xFF));

    printf("[PASS] test_keyboard_whitelist\n");
}

static void test_protocol_payloads(void) {
    char buf[4096];

    /* 1. ACK */
    int len = ctl_build_ack_payload(buf, sizeof(buf), "cmd_1", "lease_1", "TERM001", 1700000000000LL);
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(buf, "\"type\":\"ack\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"result\":\"injected\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"command_id\":\"cmd_1\"") != NULL);

    /* 2. Stream ACK */
    len = ctl_build_ack_stream_payload(buf, sizeof(buf), "cmd_2", "lease_1", "TERM001",
                                       "started", "stream_99", "running", 1700000000000LL);
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(buf, "\"result\":\"started\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"stream_instance_id\":\"stream_99\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"state\":\"running\"") != NULL);

    /* 2b. Stream ACK with empty stream_instance_id -> null */
    len = ctl_build_ack_stream_payload(buf, sizeof(buf), "cmd_2b", "lease_1", "TERM001",
                                       "stopped", "", "stopped", 1700000000000LL);
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(buf, "\"result\":\"stopped\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"stream_instance_id\":null") != NULL);
    ASSERT_TRUE(strstr(buf, "\"stream_instance_id\":\"\"") == NULL);
    ASSERT_TRUE(strstr(buf, "\"state\":\"stopped\"") != NULL);

    /* 2c. Stream ACK with NULL stream_instance_id -> null */
    len = ctl_build_ack_stream_payload(buf, sizeof(buf), "cmd_2c", "lease_1", "TERM001",
                                       "stopped", NULL, "stopped", 1700000000000LL);
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(buf, "\"result\":\"stopped\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"stream_instance_id\":null") != NULL);
    ASSERT_TRUE(strstr(buf, "\"state\":\"stopped\"") != NULL);

    /* 3. NACK */
    len = ctl_build_nack_payload(buf, sizeof(buf), "cmd_3", "lease_1", "TERM001",
                                 "source_not_allowed", "Display is denied", 1700000000000LL);
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(buf, "\"result\":\"nack\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"code\":\"source_not_allowed\"") != NULL);

    /* 4. Stream Event */
    len = ctl_build_stream_event_payload(buf, sizeof(buf), "TERM001", "stream_99", "running", "");
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(buf, "\"type\":\"stream_event\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"stream_instance_id\":\"stream_99\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"state\":\"running\"") != NULL);

    /* 5. Extended Presence */
    SystemInventory inv;
    memset(&inv, 0, sizeof(inv));
    strcpy_s(inv.displays[0].desktop_id, sizeof(inv.displays[0].desktop_id), "disp:12345678");
    strcpy_s(inv.displays[0].name, sizeof(inv.displays[0].name), "\\\\.\\DISPLAY1");
    inv.displays[0].primary = true;
    inv.displays[0].width = 1920;
    inv.displays[0].height = 1080;
    strcpy_s(inv.displays[0].policy, sizeof(inv.displays[0].policy), "input");
    inv.display_count = 1;

    strcpy_s(inv.cameras[0].camera_id, sizeof(inv.cameras[0].camera_id), "cam:87654321");
    strcpy_s(inv.cameras[0].name, sizeof(inv.cameras[0].name), "USB Cam");
    inv.cameras[0].available = true;
    inv.camera_count = 1;

    StreamStateInfo sinfo;
    memset(&sinfo, 0, sizeof(sinfo));
    strcpy_s(sinfo.state, sizeof(sinfo.state), "stopped");

    ScreenMetrics sm = { 0, 0, 1920, 1080 };
    len = ctl_build_extended_presence_payload(buf, sizeof(buf), "online", true, &sm, &inv, &sinfo);
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(buf, "\"type\":\"presence\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"version\":\"1.7.6\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"capabilities\":[\"quick_actions\",\"shortcut_action\",\"right_click\"]") != NULL);
    ASSERT_TRUE(strstr(buf, "\"inventory\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"disp:12345678\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"cam:87654321\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"stream\":{\"state\":\"stopped\",\"mode\":\"stopped\",") != NULL);
    ASSERT_TRUE(strstr(buf, "\"source_id\":null") != NULL);
    ASSERT_TRUE(strstr(buf, "\"stream_instance_id\":null") != NULL);
    ASSERT_TRUE(strstr(buf, "\"profile\":\"default\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"reason\":null") != NULL);
    ASSERT_TRUE(strstr(buf, "\"started_at\":null") != NULL);

    /* Test running stream with timestamp */
    sinfo.started_at = 1789043052;
    strcpy_s(sinfo.state, sizeof(sinfo.state), "running");
    strcpy_s(sinfo.mode, sizeof(sinfo.mode), "desktop");
    strcpy_s(sinfo.source_id, sizeof(sinfo.source_id), "disp:12345678");
    strcpy_s(sinfo.stream_instance_id, sizeof(sinfo.stream_instance_id), "str_abc");
    len = ctl_build_extended_presence_payload(buf, sizeof(buf), "online", true, &sm, &inv, &sinfo);
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(buf, "\"stream\":{\"state\":\"running\",\"mode\":\"desktop\",") != NULL);
    ASSERT_TRUE(strstr(buf, "\"source_id\":\"disp:12345678\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"stream_instance_id\":\"str_abc\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"started_at\":\"") != NULL);

    /* 6. FFmpeg H.264 Baseline Level 3.1 command line verification */
    wchar_t cmdline[2048];
    ASSERT_TRUE(ffmpeg_build_desktop_cmdline(L"C:\\l4tools\\ffmpeg\\ffmpeg.exe",
                                            "test_str_inst", "default",
                                            0, 0, 1920, 1080,
                                            cmdline, sizeof(cmdline) / sizeof(wchar_t)));
    ASSERT_TRUE(wcsstr(cmdline, L"-profile:v baseline -level 3.1 -x264-params bframes=0:force-cfr=1") != NULL);
    ASSERT_TRUE(wcsstr(cmdline, L"-sc_threshold 0") != NULL);
    ASSERT_TRUE(wcsstr(cmdline, L"-g 50 -keyint_min 50") != NULL);

    ASSERT_TRUE(ffmpeg_build_camera_cmdline(L"C:\\l4tools\\ffmpeg\\ffmpeg.exe",
                                           "test_str_inst", "low",
                                           "@device:pnp:camera1", "USB Camera",
                                           cmdline, sizeof(cmdline) / sizeof(wchar_t)));
    ASSERT_TRUE(wcsstr(cmdline, L"-profile:v baseline -level 3.1 -x264-params bframes=0:force-cfr=1") != NULL);
    ASSERT_TRUE(wcsstr(cmdline, L"-sc_threshold 0") != NULL);
    ASSERT_TRUE(wcsstr(cmdline, L"-g 30 -keyint_min 30") != NULL);

    printf("[PASS] test_protocol_payloads\n");
}

static void test_command_handling_validation(void) {
    dedup_cache_init();
    SystemInventory inv;
    memset(&inv, 0, sizeof(inv));
    strcpy_s(inv.displays[0].desktop_id, sizeof(inv.displays[0].desktop_id), "disp:11223344");
    strcpy_s(inv.displays[0].name, sizeof(inv.displays[0].name), "\\\\.\\DISPLAY1");
    inv.displays[0].primary = true;
    inv.displays[0].width = 1920;
    inv.displays[0].height = 1080;
    strcpy_s(inv.displays[0].policy, sizeof(inv.displays[0].policy), "input");
    inv.display_count = 1;

    strcpy_s(inv.allowed_profiles[0], sizeof(inv.allowed_profiles[0]), "default");
    strcpy_s(inv.allowed_profiles[1], sizeof(inv.allowed_profiles[1]), "low");
    inv.profile_count = 2;

    char resp[4096];
    size_t resp_len = 0;
    bool should_pub = false;
    uint8_t qos = 1;

    /* 1. inventory_get */
    const char* cmd_inv = "{\"v\":1,\"type\":\"inventory_get\",\"command_id\":\"inv_001\",\"lease_id\":\"lease_1\",\"sn\":\"TERM001\"}";
    ASSERT_TRUE(ctl_handle_command(cmd_inv, strlen(cmd_inv), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"result\":\"inventory\"") != NULL);
    ASSERT_TRUE(strstr(resp, "\"disp:11223344\"") != NULL);

    /* Dedup test: same command_id returns cached response */
    char resp_dedup[4096];
    size_t resp_dedup_len = 0;
    bool pub_dedup = false;
    ASSERT_TRUE(ctl_handle_command(cmd_inv, strlen(cmd_inv), "TERM001", &inv, resp_dedup, sizeof(resp_dedup), &resp_dedup_len, &pub_dedup, &qos));
    ASSERT_TRUE(pub_dedup);
    ASSERT_TRUE(strcmp(resp, resp_dedup) == 0);

    /* 2. SN mismatch */
    const char* cmd_bad_sn = "{\"v\":1,\"type\":\"inventory_get\",\"command_id\":\"inv_002\",\"lease_id\":\"lease_1\",\"sn\":\"OTHER_TERM\"}";
    ASSERT_TRUE(ctl_handle_command(cmd_bad_sn, strlen(cmd_bad_sn), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"invalid_sn\"") != NULL);

    /* 3. Unsupported command */
    const char* cmd_unknown = "{\"v\":1,\"type\":\"reboot\",\"command_id\":\"reb_001\",\"sn\":\"TERM001\"}";
    ASSERT_TRUE(ctl_handle_command(cmd_unknown, strlen(cmd_unknown), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"unsupported\"") != NULL);

    /* 4. Invalid profile */
    const char* cmd_bad_prof = "{\"v\":1,\"type\":\"stream_start\",\"command_id\":\"str_001\",\"lease_id\":\"l1\","
                               "\"mode\":\"desktop\",\"source_id\":\"disp:11223344\",\"profile\":\"ultra_4k\",\"stream_instance_id\":\"s1\"}";
    ASSERT_TRUE(ctl_handle_command(cmd_bad_prof, strlen(cmd_bad_prof), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"invalid_profile\"") != NULL);

    /* 5. Source unavailable */
    const char* cmd_bad_src = "{\"v\":1,\"type\":\"stream_start\",\"command_id\":\"str_002\",\"lease_id\":\"l1\","
                              "\"mode\":\"desktop\",\"source_id\":\"disp:99999999\",\"profile\":\"default\",\"stream_instance_id\":\"s1\"}";
    ASSERT_TRUE(ctl_handle_command(cmd_bad_src, strlen(cmd_bad_src), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"source_unavailable\"") != NULL);

    /* 5b. Source fallback: source_id="disp" finds primary display disp:11223344 */
    DisplayInfo d_fallback;
    ASSERT_TRUE(inventory_find_display(&inv, "disp", &d_fallback));
    ASSERT_TRUE(strcmp(d_fallback.desktop_id, "disp:11223344") == 0);

    /* 6. Mouse click when stream not running -> stream_mismatch */
    const char* cmd_click = "{\"v\":1,\"type\":\"mouse_click\",\"command_id\":\"clk_001\",\"lease_id\":\"l1\","
                            "\"desktop_id\":\"disp:11223344\",\"stream_instance_id\":\"s1\",\"x\":0.5,\"y\":0.5}";
    ASSERT_TRUE(ctl_handle_command(cmd_click, strlen(cmd_click), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"stream_mismatch\"") != NULL);

    /* 7. Key event when stream not running -> stream_mismatch */
    const char* cmd_key = "{\"v\":1,\"type\":\"key_event\",\"command_id\":\"key_001\",\"lease_id\":\"l1\","
                          "\"desktop_id\":\"disp:11223344\",\"stream_instance_id\":\"s1\",\"kind\":\"press\",\"vk\":65}";
    ASSERT_TRUE(ctl_handle_command(cmd_key, strlen(cmd_key), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"stream_mismatch\"") != NULL);

    /* 8. stream_stop when stream stopped with empty stream_instance_id -> stream_instance_id: null */
    const char* cmd_stop = "{\"v\":1,\"type\":\"stream_stop\",\"command_id\":\"stp_001\",\"lease_id\":\"l1\","
                           "\"sn\":\"TERM001\",\"stream_instance_id\":\"\"}";
    ASSERT_TRUE(ctl_handle_command(cmd_stop, strlen(cmd_stop), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"result\":\"already_stopped\"") != NULL);
    ASSERT_TRUE(strstr(resp, "\"stream_instance_id\":null") != NULL);
    ASSERT_TRUE(strstr(resp, "\"stream_instance_id\":\"\"") == NULL);

    /* 9. Lease watchdog update verification */
    ffmpeg_supervisor_init("C:\\l4tools", "TERM001");
    StreamStateInfo sinfo;
    ffmpeg_supervisor_get_info(&sinfo);
    ASSERT_TRUE(sinfo.lease_expires_at_ms == 0);

    ffmpeg_supervisor_update_lease("l1", 1700000000000ULL);
    ffmpeg_supervisor_get_info(&sinfo);
    ASSERT_TRUE(sinfo.lease_expires_at_ms == 1700000000000ULL);

    /* Smaller lease_expires_at_ms is not overwritten (monotonic) */
    ffmpeg_supervisor_update_lease("l1", 1600000000000ULL);
    ffmpeg_supervisor_get_info(&sinfo);
    ASSERT_TRUE(sinfo.lease_expires_at_ms == 1700000000000ULL);

    /* 10. lease_renew when stream not running -> stream_not_running */
    const char* cmd_renew_nostream = "{\"v\":1,\"type\":\"lease_renew\",\"command_id\":\"rnw_001\",\"lease_id\":\"l1\","
                                     "\"sn\":\"TERM001\",\"expires_at_ms\":4102444800000}";
    ASSERT_TRUE(ctl_handle_command(cmd_renew_nostream, strlen(cmd_renew_nostream), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"stream_not_running\"") != NULL);

    /* 11. Start stream, then lease_renew with wrong lease_id -> lease_mismatch */
    char start_res[32], start_err_code[64], start_err_msg[256];
    ffmpeg_supervisor_start("s_test", "lease_valid", "desktop", "disp:11223344", "default", &inv,
                            start_res, sizeof(start_res), start_err_code, sizeof(start_err_code), start_err_msg, sizeof(start_err_msg));

    const char* cmd_renew_badlease = "{\"v\":1,\"type\":\"lease_renew\",\"command_id\":\"rnw_002\",\"lease_id\":\"lease_wrong\","
                                     "\"sn\":\"TERM001\",\"expires_at_ms\":4102444800000}";
    ASSERT_TRUE(ctl_handle_command(cmd_renew_badlease, strlen(cmd_renew_badlease), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"lease_mismatch\"") != NULL);

    /* 12. lease_renew with matching lease_id -> ACK and lease updated */
    const char* cmd_renew_ok = "{\"v\":1,\"type\":\"lease_renew\",\"command_id\":\"rnw_003\",\"lease_id\":\"lease_valid\","
                               "\"sn\":\"TERM001\",\"expires_at_ms\":4102444800000}";
    ASSERT_TRUE(ctl_handle_command(cmd_renew_ok, strlen(cmd_renew_ok), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"type\":\"ack\"") != NULL);
    ffmpeg_supervisor_get_info(&sinfo);
    ASSERT_TRUE(sinfo.lease_expires_at_ms == 4102444800000ULL);

    /* 13. stream_renew alias -> ACK and lease updated */
    const char* cmd_renew_alias = "{\"v\":1,\"type\":\"stream_renew\",\"command_id\":\"rnw_004\",\"lease_id\":\"lease_valid\","
                                 "\"sn\":\"TERM001\",\"expires_at_ms\":4102444900000}";
    ASSERT_TRUE(ctl_handle_command(cmd_renew_alias, strlen(cmd_renew_alias), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"type\":\"ack\"") != NULL);
    ffmpeg_supervisor_get_info(&sinfo);
    ASSERT_TRUE(sinfo.lease_expires_at_ms == 4102444900000ULL);

    /* 14. Contract failing fixture (app1 incident 2026-09-11: cmd_id instead of command_id) -> rejected as invalid_payload */
    uint64_t prev_lease_expires = sinfo.lease_expires_at_ms;
    const char* cmd_renew_failing_fixture =
        "{\"v\":1,\"type\":\"lease_renew\",\"cmd_id\":\"831028de-8f1a-42b9-986a-6a2ee4e5e578\","
        "\"lease_id\":\"lease_valid\",\"sn\":\"TERM001\",\"expires_at_ms\":4102444950000,\"ttl_sec\":15,"
        "\"timestamp\":\"2026-09-11T10:53:37.642Z\"}";
    ASSERT_TRUE(ctl_handle_command(cmd_renew_failing_fixture, strlen(cmd_renew_failing_fixture), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"result\":\"nack\"") != NULL);
    ASSERT_TRUE(strstr(resp, "\"code\":\"invalid_payload\"") != NULL);
    ffmpeg_supervisor_get_info(&sinfo);
    ASSERT_TRUE(sinfo.lease_expires_at_ms == prev_lease_expires); /* Not updated! Fail-closed verified */

    /* 15. Contract canonical fixture (canonical command_id UUID) -> ACK and lease updated */
    const char* cmd_renew_canonical_fixture =
        "{\"v\":1,\"type\":\"lease_renew\",\"command_id\":\"dbca37f5-5b0c-427c-a009-a4ddccfe28d0\","
        "\"lease_id\":\"lease_valid\",\"sn\":\"TERM001\",\"expires_at_ms\":4102445000000,\"ttl_sec\":15,"
        "\"timestamp\":\"2026-09-11T10:53:37.642Z\"}";
    ASSERT_TRUE(ctl_handle_command(cmd_renew_canonical_fixture, strlen(cmd_renew_canonical_fixture), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"type\":\"ack\"") != NULL);
    ASSERT_TRUE(strstr(resp, "\"command_id\":\"dbca37f5-5b0c-427c-a009-a4ddccfe28d0\"") != NULL);
    ffmpeg_supervisor_get_info(&sinfo);
    ASSERT_TRUE(sinfo.lease_expires_at_ms == 4102445000000ULL);

    /* 16. Negative: stream_instance_id mismatch -> stream_mismatch */
    const char* cmd_renew_bad_epoch =
        "{\"v\":1,\"type\":\"lease_renew\",\"command_id\":\"rnw_005\",\"lease_id\":\"lease_valid\","
        "\"stream_instance_id\":\"wrong_epoch_id\",\"sn\":\"TERM001\",\"expires_at_ms\":4102445100000}";
    ASSERT_TRUE(ctl_handle_command(cmd_renew_bad_epoch, strlen(cmd_renew_bad_epoch), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"stream_mismatch\"") != NULL);

    /* 17. Negative: missing / 0 expires_at_ms -> invalid_payload */
    const char* cmd_renew_zero_exp =
        "{\"v\":1,\"type\":\"lease_renew\",\"command_id\":\"rnw_006\",\"lease_id\":\"lease_valid\","
        "\"sn\":\"TERM001\",\"expires_at_ms\":0}";
    ASSERT_TRUE(ctl_handle_command(cmd_renew_zero_exp, strlen(cmd_renew_zero_exp), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"invalid_payload\"") != NULL);

    ffmpeg_supervisor_stop(NULL, NULL, start_res, sizeof(start_res), start_err_code, sizeof(start_err_code), start_err_msg, sizeof(start_err_msg));

    printf("[PASS] test_command_handling_validation\n");
}

static void test_input_gate_and_shortcuts(void) {
    SystemInventory inv;
    memset(&inv, 0, sizeof(inv));
    inv.display_count = 1;
    strcpy_s(inv.displays[0].desktop_id, sizeof(inv.displays[0].desktop_id), "disp:11223344");
    strcpy_s(inv.displays[0].policy, sizeof(inv.displays[0].policy), "input");

    char resp[8192];
    size_t resp_len = 0;
    bool should_pub = false;
    uint8_t qos = 1;

    ffmpeg_supervisor_init("C:\\l4tools", "TERM001");

    char start_res[32], start_err_code[64], start_err_msg[256];
    ffmpeg_supervisor_start("s_input_test", "lease_input", "desktop", "disp:11223344", "default", &inv,
                            start_res, sizeof(start_res), start_err_code, sizeof(start_err_code), start_err_msg, sizeof(start_err_msg));

    /* 1. Desktop locked check */
    desktop_set_test_override(DESKTOP_ACCESS_LOCKED);
    const char* cmd_clk_locked = "{\"v\":1,\"type\":\"mouse_click\",\"command_id\":\"clk_lock_01\",\"lease_id\":\"lease_input\","
                                 "\"desktop_id\":\"disp:11223344\",\"stream_instance_id\":\"s_input_test\",\"x\":0.5,\"y\":0.5}";
    ASSERT_TRUE(ctl_handle_command(cmd_clk_locked, strlen(cmd_clk_locked), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"desktop_locked\"") != NULL);

    /* 2. Session unavailable check */
    desktop_set_test_override(DESKTOP_ACCESS_SESSION_UNAVAILABLE);
    const char* cmd_clk_nosess = "{\"v\":1,\"type\":\"mouse_click\",\"command_id\":\"clk_nosess_01\",\"lease_id\":\"lease_input\","
                                 "\"desktop_id\":\"disp:11223344\",\"stream_instance_id\":\"s_input_test\",\"x\":0.5,\"y\":0.5}";
    ASSERT_TRUE(ctl_handle_command(cmd_clk_nosess, strlen(cmd_clk_nosess), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"session_unavailable\"") != NULL);

    /* Reset desktop override to OK for remaining input tests */
    desktop_set_test_override(DESKTOP_ACCESS_OK);

    /* 3. Mouse click: middle button rejected */
    const char* cmd_clk_middle = "{\"v\":1,\"type\":\"mouse_click\",\"command_id\":\"clk_mid_01\",\"lease_id\":\"lease_input\","
                                 "\"desktop_id\":\"disp:11223344\",\"stream_instance_id\":\"s_input_test\",\"button\":\"middle\",\"x\":0.5,\"y\":0.5}";
    ASSERT_TRUE(ctl_handle_command(cmd_clk_middle, strlen(cmd_clk_middle), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"invalid_payload\"") != NULL);

    /* 4. Mouse click: unknown button rejected */
    const char* cmd_clk_unknown = "{\"v\":1,\"type\":\"mouse_click\",\"command_id\":\"clk_unk_01\",\"lease_id\":\"lease_input\","
                                  "\"desktop_id\":\"disp:11223344\",\"stream_instance_id\":\"s_input_test\",\"button\":\"wheel_up\",\"x\":0.5,\"y\":0.5}";
    ASSERT_TRUE(ctl_handle_command(cmd_clk_unknown, strlen(cmd_clk_unknown), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"invalid_payload\"") != NULL);

    /* 5. Shortcuts policy: default policy is all false */
    input_set_shortcut_policy(false, false, false, NULL);

    // f12 disabled by policy
    const char* cmd_sc_f12_blocked = "{\"v\":1,\"type\":\"shortcut_action\",\"command_id\":\"sc_f12_01\",\"lease_id\":\"lease_input\","
                                     "\"source_id\":\"disp:11223344\",\"stream_instance_id\":\"s_input_test\",\"action\":\"f12\"}";
    ASSERT_TRUE(ctl_handle_command(cmd_sc_f12_blocked, strlen(cmd_sc_f12_blocked), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"action_blocked_policy\"") != NULL);

    // alt_f4 disabled by policy
    const char* cmd_sc_altf4_blocked = "{\"v\":1,\"type\":\"shortcut_action\",\"command_id\":\"sc_af4_01\",\"lease_id\":\"lease_input\","
                                       "\"source_id\":\"disp:11223344\",\"stream_instance_id\":\"s_input_test\",\"action\":\"alt_f4\"}";
    ASSERT_TRUE(ctl_handle_command(cmd_sc_altf4_blocked, strlen(cmd_sc_altf4_blocked), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"action_blocked_policy\"") != NULL);

    // win_d disabled by policy
    const char* cmd_sc_wind_blocked = "{\"v\":1,\"type\":\"shortcut_action\",\"command_id\":\"sc_wd_01\",\"lease_id\":\"lease_input\","
                                      "\"source_id\":\"disp:11223344\",\"stream_instance_id\":\"s_input_test\",\"action\":\"win_d\"}";
    ASSERT_TRUE(ctl_handle_command(cmd_sc_wind_blocked, strlen(cmd_sc_wind_blocked), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"action_blocked_policy\"") != NULL);

    // unknown action rejected
    const char* cmd_sc_unk = "{\"v\":1,\"type\":\"shortcut_action\",\"command_id\":\"sc_unk_01\",\"lease_id\":\"lease_input\","
                             "\"source_id\":\"disp:11223344\",\"stream_instance_id\":\"s_input_test\",\"action\":\"ctrl_alt_del\"}";
    ASSERT_TRUE(ctl_handle_command(cmd_sc_unk, strlen(cmd_sc_unk), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"invalid_payload\"") != NULL);

    // Enable f12 policy -> now f12 action succeeds
    input_set_shortcut_policy(true, false, false, NULL);
    const char* cmd_sc_f12_ok = "{\"v\":1,\"type\":\"shortcut_action\",\"command_id\":\"sc_f12_02\",\"lease_id\":\"lease_input\","
                                "\"source_id\":\"disp:11223344\",\"stream_instance_id\":\"s_input_test\",\"action\":\"f12\"}";
    ASSERT_TRUE(ctl_handle_command(cmd_sc_f12_ok, strlen(cmd_sc_f12_ok), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"type\":\"ack\"") != NULL);
    ASSERT_TRUE(strstr(resp, "\"result\":\"injected\"") != NULL);

    // Key event with VK_F12 when allow_f12 is enabled
    const char* cmd_key_f12 = "{\"v\":1,\"type\":\"key_event\",\"command_id\":\"key_f12_01\",\"lease_id\":\"lease_input\","
                              "\"desktop_id\":\"disp:11223344\",\"stream_instance_id\":\"s_input_test\",\"kind\":\"press\",\"vk\":123}";
    ASSERT_TRUE(ctl_handle_command(cmd_key_f12, strlen(cmd_key_f12), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"type\":\"ack\"") != NULL);

    // Disable f12 policy -> VK_F12 in key_event must now be blocked
    input_set_shortcut_policy(false, false, false, NULL);
    const char* cmd_key_f12_blocked = "{\"v\":1,\"type\":\"key_event\",\"command_id\":\"key_f12_02\",\"lease_id\":\"lease_input\","
                                      "\"desktop_id\":\"disp:11223344\",\"stream_instance_id\":\"s_input_test\",\"kind\":\"press\",\"vk\":123}";
    ASSERT_TRUE(ctl_handle_command(cmd_key_f12_blocked, strlen(cmd_key_f12_blocked), "TERM001", &inv, resp, sizeof(resp), &resp_len, &should_pub, &qos));
    ASSERT_TRUE(should_pub);
    ASSERT_TRUE(strstr(resp, "\"code\":\"action_blocked_policy\"") != NULL);

    desktop_set_test_override(-1);
    ffmpeg_supervisor_stop(NULL, NULL, start_res, sizeof(start_res), start_err_code, sizeof(start_err_code), start_err_msg, sizeof(start_err_msg));
    printf("[PASS] test_input_gate_and_shortcuts\n");
}

int main(void) {
    printf("=== Running l4desk Unit Tests (Protocol & Inventory & Input) ===\n");
    test_json_min();
    test_fnv1a_stability();
    test_mouse_mapping();
    test_keyboard_whitelist();
    test_protocol_payloads();
    test_command_handling_validation();
    test_input_gate_and_shortcuts();
    printf("=== ALL PROTOCOL UNIT TESTS PASSED ===\n");
    return 0;
}
