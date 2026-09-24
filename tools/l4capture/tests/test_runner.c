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

extern int test_dxgi_probe_graceful(void);
extern int test_dxgi_create_destroy(void);
extern int test_dxgi_init_and_single_output_validation(void);
extern int test_dxgi_virtual_desktop_rejection(void);
extern int test_dxgi_acquire_timeout_no_frame(void);
extern int test_dxgi_release_frame_leak_stress(void);
extern int test_dxgi_rotation_transform(void);
extern int test_dxgi_cursor_shape_handling(void);
extern int test_dxgi_access_lost_session_dead(void);
extern int test_dxgi_access_lost_retry_and_gdi_fallback(void);

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

extern int test_mf_probe_graceful(void);
extern int test_mf_create_destroy(void);
extern int test_mf_init_types_and_sdp_compat(void);
extern int test_mf_color_convert_bgra_to_nv12(void);
extern int test_mf_nal_normalization_and_stripping(void);
extern int test_mf_first_frame_idr_sps_pps(void);
extern int test_mf_idr_cadence_and_sps_repetition(void);
extern int test_mf_force_idr_coalescing(void);
extern int test_mf_mft_failure_openh264_fallback(void);
extern int test_mf_capability_cache_persist(void);
extern int test_mf_stress_100_frames_zero_leak(void);

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
extern int test_pts_session_relative_wall(void);
extern int test_rtp_ts_wall_monotonic_and_idle_gap(void);
extern int test_rtp_sndbuf_live_budget(void);
extern int test_network_wouldblock_drops_au_forces_idr(void);

extern int test_profile_params_table(void);
extern int test_profile_resolve_low_never_upgrades(void);
extern int test_profile_resolve_default_win7_is_480p(void);
extern int test_profile_resolve_default_maps_upper_bound(void);
extern int test_input_gate_denied_for_default_even_at_480p(void);
extern int test_input_gate_allowed_only_low_and_480p(void);
extern int test_overload_detector_two_windows(void);
extern int test_degrade_ladder_monotonic_single_step(void);
extern int test_no_oscillation_upgrade_forbidden(void);
extern int test_raster_down_fps_not_up(void);
extern int test_config_change_clears_pending_and_first_idr(void);
extern int test_stop_on_persistent_overload_at_480p(void);
extern int test_bitrate_cut_requires_delivery_or_measured_exceed(void);
extern int test_degrade_does_not_break_input_gate(void);
extern int test_safety_stop_latency_preserved_under_degrade(void);
extern int test_no_oscillation_100_cycles_forced_overload(void);
extern int test_detector_threshold_boundaries(void);
extern int test_detector_p95_min_samples_guard(void);
extern int test_detector_classes_independent(void);
extern int test_p95_from_samples(void);
extern int test_window_boundary_rejects_partial(void);
extern int test_holdoff_blocks_second_action(void);
extern int test_start_720p_10_skips_d1(void);
extern int test_nodata_resets_streaks(void);
extern int test_reference_timeline_480p_stop_21s(void);

extern int test_telemetry_p95_window_rank(void);
extern int test_telemetry_p95_window_reset(void);
extern int test_telemetry_p95_window_capacity(void);
extern int test_telemetry_rate_window_fps_bitrate(void);
extern int test_telemetry_rate_window_zero(void);
extern int test_telemetry_process_resources_smoke(void);
extern int test_telemetry_private_bytes_nonzero_match_taskmgr_scale(void);
extern int test_telemetry_platform_inventory_fields(void);
extern int test_logger_scrub_secrets(void);
extern int test_logger_write_and_inventory(void);
extern int test_logger_rotation_5mib_x2(void);
extern int test_logger_queue_and_p95_wire_contract(void);

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
    /* DXGI capture tests */
    TEST(test_dxgi_probe_graceful),
    TEST(test_dxgi_create_destroy),
    TEST(test_dxgi_init_and_single_output_validation),
    TEST(test_dxgi_virtual_desktop_rejection),
    TEST(test_dxgi_acquire_timeout_no_frame),
    TEST(test_dxgi_release_frame_leak_stress),
    TEST(test_dxgi_rotation_transform),
    TEST(test_dxgi_cursor_shape_handling),
    TEST(test_dxgi_access_lost_session_dead),
    TEST(test_dxgi_access_lost_retry_and_gdi_fallback),
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
    /* Media Foundation hardware encoder tests */
    TEST(test_mf_probe_graceful),
    TEST(test_mf_capability_cache_persist),
    TEST(test_mf_create_destroy),
    TEST(test_mf_init_types_and_sdp_compat),
    TEST(test_mf_color_convert_bgra_to_nv12),
    TEST(test_mf_nal_normalization_and_stripping),
    TEST(test_mf_first_frame_idr_sps_pps),
    TEST(test_mf_idr_cadence_and_sps_repetition),
    TEST(test_mf_force_idr_coalescing),
    TEST(test_mf_mft_failure_openh264_fallback),
    TEST(test_mf_stress_100_frames_zero_leak),
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
    TEST(test_pts_session_relative_wall),
    TEST(test_rtp_ts_wall_monotonic_and_idle_gap),
    TEST(test_rtp_sndbuf_live_budget),
    TEST(test_network_wouldblock_drops_au_forces_idr),
    /* Profiles / degrade controller */
    TEST(test_profile_params_table),
    TEST(test_profile_resolve_low_never_upgrades),
    TEST(test_profile_resolve_default_win7_is_480p),
    TEST(test_profile_resolve_default_maps_upper_bound),
    TEST(test_input_gate_denied_for_default_even_at_480p),
    TEST(test_input_gate_allowed_only_low_and_480p),
    TEST(test_overload_detector_two_windows),
    TEST(test_degrade_ladder_monotonic_single_step),
    TEST(test_no_oscillation_upgrade_forbidden),
    TEST(test_raster_down_fps_not_up),
    TEST(test_config_change_clears_pending_and_first_idr),
    TEST(test_stop_on_persistent_overload_at_480p),
    TEST(test_bitrate_cut_requires_delivery_or_measured_exceed),
    TEST(test_degrade_does_not_break_input_gate),
    TEST(test_safety_stop_latency_preserved_under_degrade),
    TEST(test_no_oscillation_100_cycles_forced_overload),
    TEST(test_detector_threshold_boundaries),
    TEST(test_detector_p95_min_samples_guard),
    TEST(test_detector_classes_independent),
    TEST(test_p95_from_samples),
    TEST(test_window_boundary_rejects_partial),
    TEST(test_holdoff_blocks_second_action),
    TEST(test_start_720p_10_skips_d1),
    TEST(test_nodata_resets_streaks),
    TEST(test_reference_timeline_480p_stop_21s),
    /* Telemetry / inventory logger (L4C-10) */
    TEST(test_telemetry_p95_window_rank),
    TEST(test_telemetry_p95_window_reset),
    TEST(test_telemetry_p95_window_capacity),
    TEST(test_telemetry_rate_window_fps_bitrate),
    TEST(test_telemetry_rate_window_zero),
    TEST(test_telemetry_process_resources_smoke),
    TEST(test_telemetry_private_bytes_nonzero_match_taskmgr_scale),
    TEST(test_telemetry_platform_inventory_fields),
    TEST(test_logger_scrub_secrets),
    TEST(test_logger_write_and_inventory),
    TEST(test_logger_rotation_5mib_x2),
    TEST(test_logger_queue_and_p95_wire_contract),
};

int main(void) {
    int pass = 0, fail = 0;
    size_t i, total = sizeof(all_tests) / sizeof(all_tests[0]);
    printf("l4capture test runner: %zu tests\n\n", total);
    fflush(stdout);
    for (i = 0; i < total; ++i) {
        int rc = all_tests[i].fn();
        if (rc == 0) {
            printf("  [PASS] %s\n", all_tests[i].name);
            ++pass;
        } else {
            printf("  [FAIL] %s (rc=%d)\n", all_tests[i].name, rc);
            ++fail;
        }
        fflush(stdout);
    }
    printf("\n%d passed, %d failed, %zu total\n", pass, fail, total);
    fflush(stdout);
    return fail ? 1 : 0;
}
