#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "../src/version.h"
#include "../src/cli.h"
#include "../src/log.h"
#include "../src/unpack.h"
#include "../src/summary.h"
#include "../../l4pin/src/cert_discovery.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;

static FILE* g_log = NULL;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        if (g_log) { fprintf(g_log, "  [FAIL] Line %d: %s\n", __LINE__, msg); fflush(g_log); } \
        printf("  [FAIL] Line %d: %s\n", __LINE__, msg); \
        fflush(stdout); \
        return false; \
    } \
} while(0)

#define RUN_TEST(fn) do { \
    g_tests_run++; \
    if (g_log) { fprintf(g_log, "[RUN ] %s\n", #fn); fflush(g_log); } \
    printf("[RUN ] %s\n", #fn); \
    fflush(stdout); \
    if (fn()) { \
        g_tests_passed++; \
        if (g_log) { fprintf(g_log, "[PASS] %s\n", #fn); fflush(g_log); } \
        printf("[PASS] %s\n", #fn); \
    } else { \
        if (g_log) { fprintf(g_log, "[FAIL] %s\n", #fn); fflush(g_log); } \
        printf("[FAIL] %s\n", #fn); \
    } \
    fflush(stdout); \
} while(0)

// ---------------------------------------------------------------------------
// 1. CLI Parser Tests
// ---------------------------------------------------------------------------
static bool test_cli_parser(void) {
    // 1. Defaults
    {
        wchar_t* argv[] = { L"l4setup.exe" };
        CliOptions opts;
        char err[128];
        TEST_ASSERT(cli_parse(1, argv, &opts, err, sizeof(err)), "Failed to parse defaults");
        TEST_ASSERT(wcscmp(opts.dest, L"C:\\l4tools") == 0, "Default dest should be C:\\l4tools");
        TEST_ASSERT(!opts.silent, "Default silent should be false");
        TEST_ASSERT(opts.interactive, "Default interactive should be true");
        TEST_ASSERT(!opts.force_reissue, "Default force_reissue should be false");
        TEST_ASSERT(!opts.no_pin, "Default no_pin should be false");
        TEST_ASSERT(!opts.repair, "Default repair should be false");
        TEST_ASSERT(!opts.smoke_only, "Default smoke_only should be false");
    }

    // 2. Flags: --pin and -p (strictly 6 digits)
    {
        wchar_t* argv[] = { L"l4setup.exe", L"--pin", L"123456" };
        CliOptions opts;
        TEST_ASSERT(cli_parse(3, argv, &opts, NULL, 0), "Failed --pin");
        TEST_ASSERT(opts.pin_specified, "pin_specified should be true");
        TEST_ASSERT(wcscmp(opts.pin, L"123456") == 0, "pin should be 123456");

        cli_clean_pin(&opts);
        TEST_ASSERT(!opts.pin_specified, "pin_specified should be false after clean");
        TEST_ASSERT(opts.pin[0] == L'\0', "pin should be wiped");

        // Invalid pins: 5 digits, 7 digits, non-digits
        wchar_t* argv_short[] = { L"l4setup.exe", L"--pin", L"12345" };
        TEST_ASSERT(!cli_parse(3, argv_short, &opts, NULL, 0), "5-digit PIN should be rejected");

        wchar_t* argv_long[] = { L"l4setup.exe", L"--pin", L"1234567" };
        TEST_ASSERT(!cli_parse(3, argv_long, &opts, NULL, 0), "7-digit PIN should be rejected");

        wchar_t* argv_alpha[] = { L"l4setup.exe", L"--pin", L"12345a" };
        TEST_ASSERT(!cli_parse(3, argv_alpha, &opts, NULL, 0), "Non-digit PIN should be rejected");
    }

    // 3. Flags: --silent without pin implies no_pin
    {
        wchar_t* argv[] = { L"l4setup.exe", L"--silent" };
        CliOptions opts;
        TEST_ASSERT(cli_parse(2, argv, &opts, NULL, 0), "Failed --silent");
        TEST_ASSERT(opts.silent, "silent should be true");
        TEST_ASSERT(!opts.interactive, "interactive should be false in silent mode");
        TEST_ASSERT(opts.no_pin, "no_pin should be implied in silent mode without pin");

        wchar_t* argv_unatt[] = { L"l4setup.exe", L"--unattended" };
        CliOptions opts_unatt;
        TEST_ASSERT(cli_parse(2, argv_unatt, &opts_unatt, NULL, 0), "Failed --unattended");
        TEST_ASSERT(opts_unatt.silent, "silent should be true for --unattended");
        TEST_ASSERT(!opts_unatt.interactive, "interactive should be false for --unattended");

        wchar_t* argv_s[] = { L"l4setup.exe", L"-s" };
        CliOptions opts_s;
        TEST_ASSERT(cli_parse(2, argv_s, &opts_s, NULL, 0), "Failed -s");
        TEST_ASSERT(opts_s.silent, "silent should be true for -s");
        TEST_ASSERT(!opts_s.interactive, "interactive should be false for -s");
    }

    // 3b. Flags: --interactive and conflict with --silent
    {
        wchar_t* argv_inter[] = { L"l4setup.exe", L"--interactive" };
        CliOptions opts;
        TEST_ASSERT(cli_parse(2, argv_inter, &opts, NULL, 0), "Failed --interactive");
        TEST_ASSERT(opts.interactive, "interactive should be true");
        TEST_ASSERT(!opts.silent, "silent should be false");

        wchar_t* argv_conflict[] = { L"l4setup.exe", L"--interactive", L"--silent" };
        char err_conflict[128] = { 0 };
        TEST_ASSERT(!cli_parse(3, argv_conflict, &opts, err_conflict, sizeof(err_conflict)), "Conflict should be rejected");
        TEST_ASSERT(strstr(err_conflict, "Conflict") != NULL, "Expected conflict error message");
    }

    // 4. Flags: /S with pin
    {
        wchar_t* argv[] = { L"l4setup.exe", L"/S", L"-p", L"654321" };
        CliOptions opts;
        TEST_ASSERT(cli_parse(4, argv, &opts, NULL, 0), "Failed /S -p");
        TEST_ASSERT(opts.silent, "silent should be true for /S");
        TEST_ASSERT(opts.pin_specified, "pin_specified should be true");
        TEST_ASSERT(wcscmp(opts.pin, L"654321") == 0, "pin value match");
        TEST_ASSERT(!opts.no_pin, "no_pin should NOT be true when pin is supplied");
    }

    // 5. Flags: --dest, --force-reissue, --repair, --smoke-only, --payload-dir
    {
        wchar_t* argv[] = {
            L"l4setup.exe",
            L"--dest", L"D:\\TestTools",
            L"--force-reissue",
            L"--repair",
            L"--smoke-only",
            L"--payload-dir", L"C:\\Payloads"
        };
        CliOptions opts;
        TEST_ASSERT(cli_parse((int)(sizeof(argv)/sizeof(argv[0])), argv, &opts, NULL, 0), "Failed complex args");
        TEST_ASSERT(wcscmp(opts.dest, L"D:\\TestTools") == 0, "Custom dest match");
        TEST_ASSERT(opts.dest_specified, "dest_specified should be true");
        TEST_ASSERT(opts.force_reissue, "force_reissue match");
        TEST_ASSERT(opts.repair, "repair match");
        TEST_ASSERT(opts.smoke_only, "smoke_only match");
        TEST_ASSERT(wcscmp(opts.payload_dir, L"C:\\Payloads") == 0, "payload_dir match");
        TEST_ASSERT(opts.payload_dir_specified, "payload_dir_specified match");
    }

    // 6. Flags: --version and --help
    {
        wchar_t* argv1[] = { L"l4setup.exe", L"--version" };
        CliOptions opts1;
        TEST_ASSERT(cli_parse(2, argv1, &opts1, NULL, 0), "Failed --version");
        TEST_ASSERT(opts1.show_version, "show_version match");

        wchar_t* argv2[] = { L"l4setup.exe", L"/?" };
        CliOptions opts2;
        TEST_ASSERT(cli_parse(2, argv2, &opts2, NULL, 0), "Failed /?");
        TEST_ASSERT(opts2.show_help, "show_help match");
    }

    return true;
}

// ---------------------------------------------------------------------------
// 2. PIN Masking in Logs Tests
// ---------------------------------------------------------------------------
static bool test_pin_masking(void) {
    char out[512];

    // Case 1: --pin
    log_mask_pin("Starting installer with --pin 123456 and dest C:\\l4tools", out, sizeof(out));
    TEST_ASSERT(strstr(out, "123456") == NULL, "PIN leaked in output!");
    TEST_ASSERT(strstr(out, "--pin ******") != NULL, "Expected masked PIN");

    // Case 2: pin= in URL
    log_mask_pin("HTTP GET https://ca.local/api?function=check&pin=021358&v=26", out, sizeof(out));
    TEST_ASSERT(strstr(out, "021358") == NULL, "PIN leaked in URL output!");
    TEST_ASSERT(strstr(out, "pin=******&v=26") != NULL, "Expected masked PIN in URL");

    // Case 3: JSON "pin": "999888"
    log_mask_pin("Request body: {\"pin\":\"999888\", \"sn\":\"123\"}", out, sizeof(out));
    TEST_ASSERT(strstr(out, "999888") == NULL, "PIN leaked in JSON output!");
    TEST_ASSERT(strstr(out, "\"pin\": \"******\"") != NULL || strstr(out, "\"pin\":\"******\"") != NULL,
                "Expected masked PIN in JSON");

    // Case 4: No PIN present
    const char* safe_text = "All services started successfully in order.";
    log_mask_pin(safe_text, out, sizeof(out));
    TEST_ASSERT(strcmp(out, safe_text) == 0, "Safe text should not be altered");

    return true;
}

// ---------------------------------------------------------------------------
// 3. Payload Selection by Architecture Tests
// ---------------------------------------------------------------------------
extern bool transform_arch_path(const wchar_t* in_path, bool is_dir, const wchar_t* target_arch, wchar_t* out_path, size_t out_size);

static bool test_payload_selection(void) {
    wchar_t out[MAX_PATH];

    // 1. Target x64: strip x64 segment
    TEST_ASSERT(transform_arch_path(L"leo4proxy\\x64\\leo4proxy.exe", false, L"x64", out, MAX_PATH), "x64 transform failed");
    TEST_ASSERT(wcscmp(out, L"leo4proxy\\leo4proxy.exe") == 0, "Stripped x64 path mismatch");

    // 2. Target x64: skip opposite x86 segment
    TEST_ASSERT(!transform_arch_path(L"leo4proxy\\x86\\leo4proxy.exe", false, L"x64", out, MAX_PATH), "x86 path should be skipped on x64 target");

    // 3. Target x86: strip x86 segment
    TEST_ASSERT(transform_arch_path(L"leo4proxy\\x86\\leo4proxy.exe", false, L"x86", out, MAX_PATH), "x86 transform failed");
    TEST_ASSERT(wcscmp(out, L"leo4proxy\\leo4proxy.exe") == 0, "Stripped x86 path mismatch");

    // 4. Target x86: skip opposite x64 segment
    TEST_ASSERT(!transform_arch_path(L"leo4proxy\\x64\\leo4proxy.exe", false, L"x86", out, MAX_PATH), "x64 path should be skipped on x86 target");

    // 5. Common files: keep as-is
    TEST_ASSERT(transform_arch_path(L"mosquitto\\mosquitto.conf", false, L"x64", out, MAX_PATH), "common file transform failed");
    TEST_ASSERT(wcscmp(out, L"mosquitto\\mosquitto.conf") == 0, "Common file altered");

    return true;
}

// ---------------------------------------------------------------------------
// 4. Phase 3 State Machine Simulation Tests
// ---------------------------------------------------------------------------
typedef enum {
    DECISION_REUSE_CERT,
    DECISION_REISSUE_CERT,
    DECISION_STANDBY_WAITING_PIN,
    DECISION_READY_FOR_ONLINE,
    DECISION_FAIL_PIN_REJECTED
} Phase3Decision;

static Phase3Decision simulate_phase3(
    cert_state st,
    bool pin_given,
    bool force_reissue,
    bool ca_reachable,
    bool ca_rejected,
    int* out_exit_code,
    char* out_warning
) {
    if (out_warning) out_warning[0] = '\0';

    if (st == CERT_VALID) {
        if (force_reissue && pin_given) {
            if (ca_rejected) {
                *out_exit_code = 25;
                return DECISION_FAIL_PIN_REJECTED;
            }
            if (!ca_reachable) {
                *out_exit_code = 11;
                return DECISION_READY_FOR_ONLINE;
            }
            *out_exit_code = 0;
            return DECISION_REISSUE_CERT;
        } else {
            if (pin_given && out_warning) {
                strcpy_s(out_warning, 64, "cert_reused_pin_ignored");
            }
            *out_exit_code = 0;
            return DECISION_REUSE_CERT;
        }
    } else if (st == CERT_EXPIRING) {
        if (pin_given) {
            if (ca_rejected) {
                *out_exit_code = 25;
                return DECISION_FAIL_PIN_REJECTED;
            }
            if (!ca_reachable) {
                *out_exit_code = 11;
                return DECISION_READY_FOR_ONLINE;
            }
            *out_exit_code = 0;
            return DECISION_REISSUE_CERT;
        } else {
            if (out_warning) {
                strcpy_s(out_warning, 64, "cert_expiring");
            }
            *out_exit_code = 0;
            return DECISION_REUSE_CERT;
        }
    } else {
        // CERT_BROKEN or CERT_ABSENT
        if (!pin_given) {
            *out_exit_code = 10;
            return DECISION_STANDBY_WAITING_PIN;
        }
        if (ca_rejected) {
            *out_exit_code = 25;
            return DECISION_FAIL_PIN_REJECTED;
        }
        if (!ca_reachable) {
            *out_exit_code = 11;
            return DECISION_READY_FOR_ONLINE;
        }
        *out_exit_code = 0;
        return DECISION_REISSUE_CERT;
    }
}

static bool test_phase3_matrix(void) {
    int code = 0;
    char warn[64];

    // Row 1: VALID, no pin, no force -> REUSE, code 0
    TEST_ASSERT(simulate_phase3(CERT_VALID, false, false, true, false, &code, warn) == DECISION_REUSE_CERT, "R1 action");
    TEST_ASSERT(code == 0, "R1 exit code");

    // Row 2: VALID, pin given, no force -> REUSE, warning cert_reused_pin_ignored, code 0
    TEST_ASSERT(simulate_phase3(CERT_VALID, true, false, true, false, &code, warn) == DECISION_REUSE_CERT, "R2 action");
    TEST_ASSERT(code == 0 && strcmp(warn, "cert_reused_pin_ignored") == 0, "R2 warning");

    // Row 3: VALID, pin given, force=true, ca_ok -> REISSUE, code 0
    TEST_ASSERT(simulate_phase3(CERT_VALID, true, true, true, false, &code, warn) == DECISION_REISSUE_CERT, "R3 action");
    TEST_ASSERT(code == 0, "R3 exit code");

    // Row 4: VALID, pin given, force=true, ca_rejected -> FAIL, code 25
    TEST_ASSERT(simulate_phase3(CERT_VALID, true, true, true, true, &code, warn) == DECISION_FAIL_PIN_REJECTED, "R4 action");
    TEST_ASSERT(code == 25, "R4 exit code");

    // Row 5: VALID, pin given, force=true, ca_unreachable -> READY_FOR_ONLINE, code 11
    TEST_ASSERT(simulate_phase3(CERT_VALID, true, true, false, false, &code, warn) == DECISION_READY_FOR_ONLINE, "R5 action");
    TEST_ASSERT(code == 11, "R5 exit code");

    // Row 6: EXPIRING, no pin, no force -> REUSE, warning cert_expiring, code 0
    TEST_ASSERT(simulate_phase3(CERT_EXPIRING, false, false, true, false, &code, warn) == DECISION_REUSE_CERT, "R6 action");
    TEST_ASSERT(code == 0 && strcmp(warn, "cert_expiring") == 0, "R6 warning");

    // Row 6b: EXPIRING, pin given, no force -> REISSUE, code 0
    TEST_ASSERT(simulate_phase3(CERT_EXPIRING, true, false, true, false, &code, warn) == DECISION_REISSUE_CERT, "R6b action");
    TEST_ASSERT(code == 0, "R6b exit code");

    // Row 7: EXPIRING, force=true, pin given, ca_ok -> REISSUE, code 0
    TEST_ASSERT(simulate_phase3(CERT_EXPIRING, true, true, true, false, &code, warn) == DECISION_REISSUE_CERT, "R7 action");
    TEST_ASSERT(code == 0, "R7 exit code");

    // Row 8: ABSENT, no pin -> STANDBY_WAITING_PIN, code 10
    TEST_ASSERT(simulate_phase3(CERT_ABSENT, false, false, true, false, &code, warn) == DECISION_STANDBY_WAITING_PIN, "R8 action");
    TEST_ASSERT(code == 10, "R8 exit code");

    // Row 9: ABSENT, pin given, ca_ok -> REISSUE, code 0
    TEST_ASSERT(simulate_phase3(CERT_ABSENT, true, false, true, false, &code, warn) == DECISION_REISSUE_CERT, "R9 action");
    TEST_ASSERT(code == 0, "R9 exit code");

    // Row 10: ABSENT, pin given, ca_rejected -> code 25
    TEST_ASSERT(simulate_phase3(CERT_ABSENT, true, false, true, true, &code, warn) == DECISION_FAIL_PIN_REJECTED, "R10 action");
    TEST_ASSERT(code == 25, "R10 exit code");

    // Row 11: ABSENT, pin given, ca_unreachable -> READY_FOR_ONLINE, code 11
    TEST_ASSERT(simulate_phase3(CERT_ABSENT, true, false, false, false, &code, warn) == DECISION_READY_FOR_ONLINE, "R11 action");
    TEST_ASSERT(code == 11, "R11 exit code");

    // Row 12: BROKEN, no pin -> code 10
    TEST_ASSERT(simulate_phase3(CERT_BROKEN, false, false, true, false, &code, warn) == DECISION_STANDBY_WAITING_PIN, "R12 action");
    TEST_ASSERT(code == 10, "R12 exit code");

    return true;
}

// ---------------------------------------------------------------------------
// 5. Summary Serialization & State Patch Tests
// ---------------------------------------------------------------------------
static bool test_summary_and_state(void) {
    wchar_t test_dir[MAX_PATH];
    GetTempPathW(MAX_PATH, test_dir);
    wcscat_s(test_dir, MAX_PATH, L"l4setup_unit_test");
    CreateDirectoryW(test_dir, NULL);

    InstallSummaryData data;
    memset(&data, 0, sizeof(data));
    strcpy_s(data.installer_version, sizeof(data.installer_version), "1.7.1");
    strcpy_s(data.os, sizeof(data.os), "Windows 10 Pro (10.0.19045) x64");
    strcpy_s(data.target_arch, sizeof(data.target_arch), "x64");
    wcscpy_s(data.dest, MAX_PATH, test_dir);
    strcpy_s(data.status, sizeof(data.status), "ready");
    data.exit_code = 0;

    data.cert.state = CERT_VALID;
    data.cert.reused = true;
    data.cert.reissued = false;
    strcpy_s(data.cert.thumbprint, sizeof(data.cert.thumbprint), "CC88419A4C3763150A4C0905261EC073C58CFC09");
    strcpy_s(data.cert.sn, sizeof(data.cert.sn), "a4b0000773c82116d210826");
    strcpy_s(data.cert.not_after, sizeof(data.cert.not_after), "2027-08-29 17:09:40 UTC");

    strcpy_s(data.drainage.services_stopped[0], 64, "Leo4Proxy");
    strcpy_s(data.drainage.services_stopped[1], 64, "mosquitto");
    strcpy_s(data.drainage.services_stopped[2], 64, "L4Con");
    strcpy_s(data.drainage.services_stopped[3], 64, "L4Superv");
    data.drainage.services_stopped_count = 4;

    strcpy_s(data.probes.proxy_info, 16, "ok");
    strcpy_s(data.probes.mosquitto_port, 16, "ok");
    data.probes.user_session_id = 1;
    data.probes.l4desk_running = true;
    strcpy_s(data.probes.ffmpeg_smoke_capture, 16, "ok");
    data.probes.desktop_locked = false;
    strcpy_s(data.probes.network, 16, "reachable");
    strcpy_s(data.probes.remote_input, 32, "available");

    strcpy_s(data.service_leo4proxy, 32, "running");
    strcpy_s(data.service_mosquitto, 32, "running");
    strcpy_s(data.service_l4con, 32, "running");
    strcpy_s(data.service_l4superv, 32, "running");

    // 1. Write summary
    TEST_ASSERT(summary_write_json(&data, test_dir), "summary_write_json failed");

    wchar_t sum_file[MAX_PATH];
    swprintf_s(sum_file, MAX_PATH, L"%ls\\install_summary.json", test_dir);
    FILE* fp = NULL;
    _wfopen_s(&fp, sum_file, L"rb");
    TEST_ASSERT(fp != NULL, "install_summary.json was not created");

    char buf[8192] = { 0 };
    fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);

    // Verify schema and fields
    TEST_ASSERT(strstr(buf, "\"schema\": 1") != NULL, "Missing schema");
    TEST_ASSERT(strstr(buf, "\"installer_version\": \"1.7.1\"") != NULL, "Missing installer_version");
    TEST_ASSERT(strstr(buf, "\"state\": \"valid\"") != NULL, "Missing cert.state");
    TEST_ASSERT(strstr(buf, "\"thumbprint\": \"CC88419A4C3763150A4C0905261EC073C58CFC09\"") != NULL, "Missing thumbprint");
    TEST_ASSERT(strstr(buf, "\"proxy_info\": \"ok\"") != NULL, "Missing probe");
    TEST_ASSERT(strstr(buf, "\"services\":") != NULL, "Missing services object");
    TEST_ASSERT(strstr(buf, "\"network\": \"reachable\"") != NULL, "Missing network probe");
    TEST_ASSERT(strstr(buf, "\"remote_input\": \"available\"") != NULL, "Missing remote_input probe");

    // 2. State patch test
    wchar_t state_file[MAX_PATH];
    swprintf_s(state_file, MAX_PATH, L"%ls\\state.json", test_dir);
    // Create an initial state.json with some custom fields
    _wfopen_s(&fp, state_file, L"wb");
    TEST_ASSERT(fp != NULL, "Failed to create test state.json");
    const char* init_json = "{\n  \"status\": \"active\",\n  \"custom_key\": 12345\n}\n";
    fwrite(init_json, 1, strlen(init_json), fp);
    fclose(fp);

    TEST_ASSERT(state_patch_version(test_dir, "1.7.1", sum_file), "state_patch_version failed");

    _wfopen_s(&fp, state_file, L"rb");
    TEST_ASSERT(fp != NULL, "Failed to open patched state.json");
    memset(buf, 0, sizeof(buf));
    fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);

    TEST_ASSERT(strstr(buf, "\"installed_version\": \"1.7.1\"") != NULL, "installed_version not added");
    TEST_ASSERT(strstr(buf, "\"custom_key\": 12345") != NULL, "custom_key was overwritten!");
    TEST_ASSERT(strstr(buf, "\"status\": \"active\"") != NULL, "status was overwritten!");

    // Cleanup test dir
    DeleteFileW(sum_file);
    DeleteFileW(state_file);
    RemoveDirectoryW(test_dir);

    return true;
}

// ---------------------------------------------------------------------------
// 6. Numeric Version Comparison Tests
// ---------------------------------------------------------------------------
static bool test_numeric_version_compare(void) {
    TEST_ASSERT(version_compare("1.7.1", "1.7.2") < 0, "1.7.1 < 1.7.2");
    TEST_ASSERT(version_compare("1.7.2", "1.7.2") == 0, "1.7.2 == 1.7.2");
    TEST_ASSERT(version_compare("1.7.2", "1.7.1") > 0, "1.7.2 > 1.7.1");

    // Critical: numeric vs lexical comparison (1.10.0 > 1.7.2)
    TEST_ASSERT(version_compare("1.10.0", "1.7.2") > 0, "1.10.0 > 1.7.2 numerically");
    TEST_ASSERT(version_compare("1.7.2", "1.10.0") < 0, "1.7.2 < 1.10.0 numerically");

    // 4 components and trailing zero equivalences
    TEST_ASSERT(version_compare("2.0.0.0", "1.9.9.9") > 0, "2.0.0.0 > 1.9.9.9");
    TEST_ASSERT(version_compare("1.7.1", "1.7.1.0") == 0, "1.7.1 == 1.7.1.0");

    return true;
}

// ---------------------------------------------------------------------------
// 7. Incomplete Marker & Crash Recovery Tests
// ---------------------------------------------------------------------------
static bool test_incomplete_marker_and_recovery(void) {
    wchar_t test_dir[MAX_PATH];
    GetTempPathW(MAX_PATH, test_dir);
    wcscat_s(test_dir, MAX_PATH, L"l4setup_crash_test");
    CreateDirectoryW(test_dir, NULL);

    // 1. Set marker
    TEST_ASSERT(unpack_set_incomplete_marker(test_dir, "update", "1.7.0", "1.7.2"), "Failed to set incomplete marker");

    char phase[64] = { 0 };
    TEST_ASSERT(unpack_has_incomplete_marker(test_dir, phase, sizeof(phase)), "Failed to detect incomplete marker");
    TEST_ASSERT(strcmp(phase, "update") == 0, "Marker phase mismatch");

    // 2. Setup mock rollback structure: dest\rollback\1.7.0\l4pin\l4pin.exe
    wchar_t rb_parent[MAX_PATH];
    swprintf_s(rb_parent, MAX_PATH, L"%ls\\rollback", test_dir);
    CreateDirectoryW(rb_parent, NULL);

    wchar_t rb_ver[MAX_PATH];
    swprintf_s(rb_ver, MAX_PATH, L"%ls\\rollback\\1.7.0", test_dir);
    CreateDirectoryW(rb_ver, NULL);

    wchar_t rb_dir[MAX_PATH];
    swprintf_s(rb_dir, MAX_PATH, L"%ls\\rollback\\1.7.0\\l4pin", test_dir);
    CreateDirectoryW(rb_dir, NULL);

    wchar_t rb_file[MAX_PATH];
    swprintf_s(rb_file, MAX_PATH, L"%ls\\l4pin.exe", rb_dir);
    FILE* fp = NULL;
    _wfopen_s(&fp, rb_file, L"wb");
    TEST_ASSERT(fp != NULL, "Failed to create mock rollback file");
    fputs("mock_l4pin_binary_content", fp);
    fclose(fp);

    // 3. Trigger crash recovery
    TEST_ASSERT(unpack_recover_from_crash(test_dir), "Crash recovery failed");

    // 4. Verify restored file in dest
    wchar_t restored_file[MAX_PATH];
    swprintf_s(restored_file, MAX_PATH, L"%ls\\l4pin\\l4pin.exe", test_dir);
    TEST_ASSERT(GetFileAttributesW(restored_file) != INVALID_FILE_ATTRIBUTES, "Restored file not found in dest");

    // 5. Verify marker cleared
    TEST_ASSERT(!unpack_has_incomplete_marker(test_dir, NULL, 0), "Marker should be cleared after recovery");

    // Cleanup
    DeleteFileW(restored_file);
    wchar_t restored_dir[MAX_PATH];
    swprintf_s(restored_dir, MAX_PATH, L"%ls\\l4pin", test_dir);
    RemoveDirectoryW(restored_dir);

    DeleteFileW(rb_file);
    RemoveDirectoryW(rb_dir);
    RemoveDirectoryW(rb_ver);
    RemoveDirectoryW(rb_parent);
    RemoveDirectoryW(test_dir);

    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    AttachConsole(ATTACH_PARENT_PROCESS);
    FILE* fp_con = NULL;
    freopen_s(&fp_con, "CONOUT$", "w", stdout);
    freopen_s(&fp_con, "CONOUT$", "w", stderr);

    fopen_s(&g_log, "test_run.log", "w");

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("=======================================================\n");
    printf(" Running tools/l4setup Unit Tests\n");
    printf("=======================================================\n");

    RUN_TEST(test_cli_parser);
    RUN_TEST(test_pin_masking);
    RUN_TEST(test_payload_selection);
    RUN_TEST(test_phase3_matrix);
    RUN_TEST(test_summary_and_state);
    RUN_TEST(test_numeric_version_compare);
    RUN_TEST(test_incomplete_marker_and_recovery);

    printf("=======================================================\n");
    printf(" Unit Tests Summary: %d / %d passed\n", g_tests_passed, g_tests_run);
    printf("=======================================================\n");

    if (g_log) {
        fprintf(g_log, "Unit Tests Summary: %d / %d passed\n", g_tests_passed, g_tests_run);
        fclose(g_log);
    }

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
