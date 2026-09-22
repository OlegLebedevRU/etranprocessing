/*
 * Autonomous C test runner — no external framework dependencies.
 * Each test module exposes test_X functions returning 0 on success.
 * This runner calls them all and reports totals.
 */
#include <stdio.h>
#include <string.h>

/* Forward declarations from test modules. */
extern int test_ipc_pack_unpack(void);
extern int test_ipc_fragmentation(void);
extern int test_ipc_bad_version(void);
extern int test_ipc_overflow_length(void);
extern int test_ipc_eof(void);
extern int test_ipc_roundtrip_all_types(void);

extern int test_limits_checked_mul(void);
extern int test_limits_checked_add(void);
extern int test_limits_bgra_layout(void);
extern int test_limits_4k_cap(void);
extern int test_limits_stride_alignment(void);
extern int test_limits_memory_reserve(void);

extern int test_deadline_basic(void);
extern int test_deadline_expired(void);
extern int test_deadline_renew(void);
extern int test_deadline_dedup(void);
extern int test_deadline_remaining(void);

extern int test_safety_init_destroy(void);
extern int test_safety_stop(void);
extern int test_safety_deadline_trigger(void);
extern int test_safety_session0_reject(void);
extern int test_safety_pipeline_drain(void);

extern int test_cursor_basic(void);
extern int test_cursor_negative_origin(void);
extern int test_cursor_leak_stress(void);

extern int test_gdi_create_destroy(void);
extern int test_gdi_init_capture_release(void);
extern int test_gdi_reuse_buffer(void);
extern int test_gdi_overflow_reject(void);

extern int test_scale_solid_fill(void);
extern int test_scale_checkerboard(void);
extern int test_scale_edge_preservation(void);
extern int test_scale_large_to_480p(void);
extern int test_scale_invalid_params(void);

extern int test_color_white(void);
extern int test_color_black(void);
extern int test_color_range_clamp(void);
extern int test_color_stride_alignment(void);
extern int test_color_create_destroy(void);

extern int test_openh264_create_destroy(void);
extern int test_openh264_encode_first_frame_idr(void);
extern int test_openh264_strip_start_codes(void);
extern int test_openh264_sps_profile_level(void);
extern int test_openh264_periodic_idr_cadence(void);
extern int test_openh264_force_idr_coalescing(void);
extern int test_openh264_decoder_smoke(void);
extern int test_openh264_memory_soak(void);

extern int test_rtp_single_nal_small(void);
extern int test_rtp_boundary_1200_1201(void);
extern int test_rtp_fua_fragmentation_large(void);
extern int test_rtp_marker_bit_au_boundary(void);
extern int test_rtp_timestamp_consistency(void);
extern int test_rtp_sequence_monotonicity_and_wrap(void);
extern int test_rtp_timestamp_wrap(void);
extern int test_rtcp_sr_sdes_generation(void);
extern int test_rtcp_bye_generation(void);
extern int test_rtp_fua_reassembly_roundtrip(void);
extern int test_network_nonblocking_drop_on_error(void);
extern int test_pipeline_e2e_loopback(void);

typedef struct {
    const char *name;
    int (*fn)(void);
} test_entry_t;

#define TEST(fn) { #fn, fn }

static const test_entry_t all_tests[] = {
    /* IPC framing tests */
    TEST(test_ipc_pack_unpack),
    TEST(test_ipc_fragmentation),
    TEST(test_ipc_bad_version),
    TEST(test_ipc_overflow_length),
    TEST(test_ipc_eof),
    TEST(test_ipc_roundtrip_all_types),
    /* Limits tests */
    TEST(test_limits_checked_mul),
    TEST(test_limits_checked_add),
    TEST(test_limits_bgra_layout),
    TEST(test_limits_4k_cap),
    TEST(test_limits_stride_alignment),
    TEST(test_limits_memory_reserve),
    /* Deadline tests */
    TEST(test_deadline_basic),
    TEST(test_deadline_expired),
    TEST(test_deadline_renew),
    TEST(test_deadline_dedup),
    TEST(test_deadline_remaining),
    /* Safety gate tests */
    TEST(test_safety_init_destroy),
    TEST(test_safety_stop),
    TEST(test_safety_deadline_trigger),
    TEST(test_safety_session0_reject),
    TEST(test_safety_pipeline_drain),
    /* Cursor tests */
    TEST(test_cursor_basic),
    TEST(test_cursor_negative_origin),
    TEST(test_cursor_leak_stress),
    /* GDI capture tests */
    TEST(test_gdi_create_destroy),
    TEST(test_gdi_init_capture_release),
    TEST(test_gdi_reuse_buffer),
    TEST(test_gdi_overflow_reject),
    /* Scale tests */
    TEST(test_scale_solid_fill),
    TEST(test_scale_checkerboard),
    TEST(test_scale_edge_preservation),
    TEST(test_scale_large_to_480p),
    TEST(test_scale_invalid_params),
    /* Color conversion tests */
    TEST(test_color_white),
    TEST(test_color_black),
    TEST(test_color_range_clamp),
    TEST(test_color_stride_alignment),
    TEST(test_color_create_destroy),
    /* OpenH264 encoder tests */
    TEST(test_openh264_create_destroy),
    TEST(test_openh264_encode_first_frame_idr),
    TEST(test_openh264_strip_start_codes),
    TEST(test_openh264_sps_profile_level),
    TEST(test_openh264_periodic_idr_cadence),
    TEST(test_openh264_force_idr_coalescing),
    TEST(test_openh264_decoder_smoke),
    TEST(test_openh264_memory_soak),
    /* RTP sender tests */
    TEST(test_rtp_single_nal_small),
    TEST(test_rtp_boundary_1200_1201),
    TEST(test_rtp_fua_fragmentation_large),
    TEST(test_rtp_marker_bit_au_boundary),
    TEST(test_rtp_timestamp_consistency),
    TEST(test_rtp_sequence_monotonicity_and_wrap),
    TEST(test_rtp_timestamp_wrap),
    TEST(test_rtcp_sr_sdes_generation),
    TEST(test_rtcp_bye_generation),
    TEST(test_rtp_fua_reassembly_roundtrip),
    TEST(test_network_nonblocking_drop_on_error),
    TEST(test_pipeline_e2e_loopback),
};

int main(void) {
    int pass = 0, fail = 0;
    size_t i, total = sizeof(all_tests) / sizeof(all_tests[0]);
    printf("l4capture test runner: %zu tests\n\n", total);
    for (i = 0; i < total; ++i) {
        int rc = all_tests[i].fn();
        if (rc == 0) {
            printf("  [PASS] %s\n", all_tests[i].name);
            ++pass;
        } else {
            printf("  [FAIL] %s (rc=%d)\n", all_tests[i].name, rc);
            ++fail;
        }
    }
    printf("\n%d passed, %d failed, %zu total\n", pass, fail, total);
    return fail ? 1 : 0;
}
