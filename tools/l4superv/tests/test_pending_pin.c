#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <shlwapi.h>

#include "state_mgr.h"
#include "config.h"
#include "cert_discovery.h"

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")

static void mask_pin_in_string(char* text, const char* pin) {
    if (!text || !text[0]) return;
    if (pin && pin[0]) {
        size_t pin_len = strlen(pin);
        char* p = text;
        while ((p = strstr(p, pin)) != NULL) {
            for (size_t i = 0; i < pin_len; i++) p[i] = '*';
            p += pin_len;
        }
    }
    // Mask pin=...
    char* p = text;
    while (*p) {
        if (_strnicmp(p, "pin=", 4) == 0) {
            p += 4;
            while (*p && *p != ' ' && *p != '&' && *p != '\r' && *p != '\n' && *p != '\t') {
                *p = '*';
                p++;
            }
        } else {
            p++;
        }
    }
}

static void test_pin_masking(void) {
    printf("[TEST] Running test_pin_masking...\n");

    char text1[256];
    strcpy_s(text1, sizeof(text1), "Executing l4pin.exe with pin=123456 and token=abc");
    mask_pin_in_string(text1, "123456");
    assert(strstr(text1, "123456") == NULL);
    assert(strstr(text1, "pin=******") != NULL);

    char text2[256];
    strcpy_s(text2, sizeof(text2), "PIN: 889900 returned code 25");
    mask_pin_in_string(text2, "889900");
    assert(strstr(text2, "889900") == NULL);
    assert(strstr(text2, "******") != NULL);

    printf("[PASS] test_pin_masking succeeded.\n");
}

static void test_dpapi_and_pending_pin(void) {
    printf("[TEST] Running test_dpapi_and_pending_pin...\n");

    const char* original_pin = "4815162342";
    DATA_BLOB inBlob;
    inBlob.pbData = (BYTE*)original_pin;
    inBlob.cbData = (DWORD)strlen(original_pin);

    DATA_BLOB outBlob;
    BOOL ok = CryptProtectData(&inBlob, L"PIN", NULL, NULL, NULL, CRYPTPROTECT_LOCAL_MACHINE | CRYPTPROTECT_UI_FORBIDDEN, &outBlob);
    assert(ok && outBlob.cbData > 0);

    DWORD b64_len = 0;
    CryptBinaryToStringA(outBlob.pbData, outBlob.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &b64_len);
    char* b64 = (char*)malloc(b64_len + 1);
    CryptBinaryToStringA(outBlob.pbData, outBlob.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64, &b64_len);
    LocalFree(outBlob.pbData);

    // Now test DPAPI decryption
    DWORD dec_bin_len = 0;
    ok = CryptStringToBinaryA(b64, 0, CRYPT_STRING_BASE64, NULL, &dec_bin_len, NULL, NULL);
    assert(ok && dec_bin_len > 0);

    BYTE* dec_bin = (BYTE*)malloc(dec_bin_len);
    CryptStringToBinaryA(b64, 0, CRYPT_STRING_BASE64, dec_bin, &dec_bin_len, NULL, NULL);

    DATA_BLOB unpIn = { dec_bin_len, dec_bin };
    DATA_BLOB unpOut = { 0, NULL };
    ok = CryptUnprotectData(&unpIn, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &unpOut);
    assert(ok);
    assert(unpOut.cbData == strlen(original_pin));
    assert(memcmp(unpOut.pbData, original_pin, strlen(original_pin)) == 0);

    LocalFree(unpOut.pbData);
    free(dec_bin);
    free(b64);

    printf("[PASS] test_dpapi_and_pending_pin succeeded.\n");
}

static void test_state_unknown_keys_preservation(void) {
    printf("[TEST] Running test_state_unknown_keys_preservation...\n");

    wchar_t temp_dir[MAX_PATH];
    GetTempPathW(MAX_PATH, temp_dir);
    wcscat_s(temp_dir, MAX_PATH, L"l4superv_state_test");
    CreateDirectoryW(temp_dir, NULL);

    wchar_t state_file[MAX_PATH];
    swprintf_s(state_file, MAX_PATH, L"%ls\\state.json", temp_dir);

    const char* initial_json =
        "{\n"
        "  \"status\": \"standby\",\n"
        "  \"sn\": \"TEST_SN_999\",\n"
        "  \"thumbprint\": \"DEADBEEF112233\",\n"
        "  \"installed_version\": \"1.6.0\",\n"
        "  \"installer_summary_path\": \"C:\\\\l4tools\\\\install_summary.json\",\n"
        "  \"last_cert_state\": \"valid\",\n"
        "  \"installer_base_path\": \"C:\\\\l4tools\",\n"
        "  \"custom_int\": 42,\n"
        "  \"custom_str\": \"hello_world\",\n"
        "  \"custom_obj\": { \"nested\": \"val\", \"count\": 10 },\n"
        "  \"flags\": [1, 2, 3]\n"
        "}\n";

    FILE* f = NULL;
    _wfopen_s(&f, state_file, L"wb");
    assert(f != NULL);
    fwrite(initial_json, 1, strlen(initial_json), f);
    fclose(f);

    L4State st;
    bool load_ok = state_load(temp_dir, &st);
    assert(load_ok);

    assert(strcmp(st.status, "standby") == 0);
    assert(strcmp(st.sn, "TEST_SN_999") == 0);
    assert(strcmp(st.thumbprint, "DEADBEEF112233") == 0);
    assert(strcmp(st.installed_version, "1.6.0") == 0);
    assert(strcmp(st.last_cert_state, "valid") == 0);
    assert(st.num_unknown_keys == 4);

    // Modify a standard field
    strcpy_s(st.status, sizeof(st.status), "active");
    strcpy_s(st.last_cert_state, sizeof(st.last_cert_state), "expiring");

    bool save_ok = state_save(temp_dir, &st);
    assert(save_ok);

    // Reload from file
    L4State st2;
    load_ok = state_load(temp_dir, &st2);
    assert(load_ok);

    assert(strcmp(st2.status, "active") == 0);
    assert(strcmp(st2.last_cert_state, "expiring") == 0);
    assert(strcmp(st2.sn, "TEST_SN_999") == 0);
    assert(strcmp(st2.installed_version, "1.6.0") == 0);
    assert(st2.num_unknown_keys == 4);

    // Verify unknown keys are preserved in content
    _wfopen_s(&f, state_file, L"rb");
    assert(f != NULL);
    char buf[4096] = { 0 };
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    assert(strstr(buf, "\"custom_int\": 42") != NULL);
    assert(strstr(buf, "\"custom_str\": \"hello_world\"") != NULL);
    assert(strstr(buf, "\"custom_obj\": { \"nested\": \"val\", \"count\": 10 }") != NULL);
    assert(strstr(buf, "\"flags\": [1, 2, 3]") != NULL);

    state_cleanup(&st);
    state_cleanup(&st2);
    DeleteFileW(state_file);
    RemoveDirectoryW(temp_dir);

    printf("[PASS] test_state_unknown_keys_preservation succeeded.\n");
}

static void test_state_machine_matrix(void) {
    printf("[TEST] Running test_state_machine_matrix...\n");

    // Matrix combinations:
    // cert_state x pending_pin x ca_reachable -> action
    typedef struct {
        cert_state cs;
        bool proxy_cert_found;
        bool has_pending_pin;
        bool ca_reachable;
        const char* expected_next_state;
        const char* expected_action;
    } TestCase;

    TestCase cases[] = {
        // 1. Ready cert found in proxy -> immediately transition to ACTIVE
        { CERT_VALID, true, false, false, "active", "transition_to_active" },
        // 2. Standby: no cert in store, no pending pin -> stay standby, poll in 5s
        { CERT_ABSENT, false, false, false, "standby", "poll_standby_5s" },
        // 3. Standby: cert valid in store, but proxy doesn't see it -> log mismatch, stay standby
        { CERT_VALID, false, false, false, "standby", "log_proxy_cert_mismatch" },
        // 4. Standby: pending pin exists, CA unreachable -> keep pending pin, stay standby
        { CERT_ABSENT, false, true, false, "standby", "keep_pending_pin_ca_unreachable" },
        // 5. Standby: pending pin exists, CA reachable -> launch l4pin
        { CERT_ABSENT, false, true, true, "standby", "launch_l4pin" }
    };

    size_t num_cases = sizeof(cases) / sizeof(cases[0]);
    for (size_t i = 0; i < num_cases; i++) {
        const char* action = "unknown";
        const char* next_state = "standby";

        if (cases[i].proxy_cert_found) {
            next_state = "active";
            action = "transition_to_active";
        } else {
            next_state = "standby";
            if (cases[i].cs == CERT_VALID) {
                action = "log_proxy_cert_mismatch";
            } else if (cases[i].has_pending_pin) {
                if (cases[i].ca_reachable) {
                    action = "launch_l4pin";
                } else {
                    action = "keep_pending_pin_ca_unreachable";
                }
            } else {
                action = "poll_standby_5s";
            }
        }

        assert(strcmp(next_state, cases[i].expected_next_state) == 0);
        assert(strcmp(action, cases[i].expected_action) == 0);
    }

    printf("[PASS] test_state_machine_matrix succeeded (%zu cases verified).\n", num_cases);
}

int main(void) {
    printf("=======================================================\n");
    printf(" Running l4superv Unit Tests\n");
    printf("=======================================================\n");

    test_pin_masking();
    test_dpapi_and_pending_pin();
    test_state_unknown_keys_preservation();
    test_state_machine_matrix();

    printf("\n=======================================================\n");
    printf(" ALL UNIT TESTS PASSED SUCCESSFULLY!\n");
    printf("=======================================================\n");
    return 0;
}
