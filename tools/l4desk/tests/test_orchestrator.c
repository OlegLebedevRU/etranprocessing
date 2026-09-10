#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "ffmpeg_supervisor.h"
#include "ffmpeg_cmdline.h"
#include "display_inventory.h"
#include "json_min.h"

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("[FAIL] Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

static void get_test_dir(char* out, size_t max_len) {
    char temp_dir[MAX_PATH];
    GetTempPathA(sizeof(temp_dir), temp_dir);
    snprintf(out, max_len, "%sl4desk_test_%lu", temp_dir, GetCurrentProcessId());
}

static void setup_test_inventory(SystemInventory* inv) {
    memset(inv, 0, sizeof(SystemInventory));
    strcpy_s(inv->displays[0].desktop_id, sizeof(inv->displays[0].desktop_id), "disp:11223344");
    strcpy_s(inv->displays[0].name, sizeof(inv->displays[0].name), "\\\\.\\DISPLAY1");
    inv->displays[0].primary = true;
    inv->displays[0].x = 0;
    inv->displays[0].y = 0;
    inv->displays[0].width = 1920;
    inv->displays[0].height = 1080;
    strcpy_s(inv->displays[0].policy, sizeof(inv->displays[0].policy), "input");
    inv->display_count = 1;

    strcpy_s(inv->cameras[0].camera_id, sizeof(inv->cameras[0].camera_id), "cam:55667788");
    strcpy_s(inv->cameras[0].name, sizeof(inv->cameras[0].name), "USB Mock Cam");
    strcpy_s(inv->cameras[0].device_path, sizeof(inv->cameras[0].device_path), "@device:pnp:\\\\mock_cam");
    inv->cameras[0].available = true;
    inv->camera_count = 1;

    strcpy_s(inv->allowed_profiles[0], sizeof(inv->allowed_profiles[0]), "default");
    strcpy_s(inv->allowed_profiles[1], sizeof(inv->allowed_profiles[1]), "low");
    inv->profile_count = 2;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    printf("=== Running l4desk FFmpeg Orchestrator Unit Tests ===\n");

    char test_dir[MAX_PATH];
    get_test_dir(test_dir, sizeof(test_dir));
    CreateDirectoryA(test_dir, NULL);

    wchar_t fake_bin[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, fake_bin);
    wcscat_s(fake_bin, MAX_PATH, L"\\bin\\fake_ffmpeg.exe");

    if (GetFileAttributesW(fake_bin) == INVALID_FILE_ATTRIBUTES) {
        /* Fallback check in current dir */
        if (GetFileAttributesW(L"fake_ffmpeg.exe") != INVALID_FILE_ATTRIBUTES) {
            wcscpy_s(fake_bin, MAX_PATH, L"fake_ffmpeg.exe");
        } else if (GetFileAttributesW(L"bin\\x64\\fake_ffmpeg.exe") != INVALID_FILE_ATTRIBUTES) {
            wcscpy_s(fake_bin, MAX_PATH, L"bin\\x64\\fake_ffmpeg.exe");
        } else if (GetFileAttributesW(L"bin\\x86\\fake_ffmpeg.exe") != INVALID_FILE_ATTRIBUTES) {
            wcscpy_s(fake_bin, MAX_PATH, L"bin\\x86\\fake_ffmpeg.exe");
        }
    }

    wprintf(L"Using mock binary: %ls\n", fake_bin);

    SystemInventory inv;
    setup_test_inventory(&inv);

    ffmpeg_supervisor_init(test_dir, "TERM_TEST_01");
    ffmpeg_supervisor_set_custom_binary(fake_bin);

    char result[32] = { 0 };
    char err_code[64] = { 0 };
    char err_msg[256] = { 0 };

    /* 1. Test stream_start */
    printf("[TEST 1] Testing stream_start (desktop)...\n");
    bool ok = ffmpeg_supervisor_start("inst_001", "lease_001", "desktop", "disp:11223344",
                                      "default", &inv, result, sizeof(result),
                                      err_code, sizeof(err_code), err_msg, sizeof(err_msg));
    ASSERT_TRUE(ok);
    ASSERT_TRUE(strcmp(result, "started") == 0);

    StreamStateInfo sinfo;
    ffmpeg_supervisor_get_info(&sinfo);
    ASSERT_TRUE(strcmp(sinfo.state, "running") == 0);
    ASSERT_TRUE(sinfo.ffmpeg_pid > 0);
    DWORD first_pid = sinfo.ffmpeg_pid;
    printf("  [OK] Stream started, PID=%lu\n", first_pid);

    /* 2. Test already_running */
    printf("[TEST 2] Testing already_running idempotency...\n");
    ok = ffmpeg_supervisor_start("inst_001", "lease_001", "desktop", "disp:11223344",
                                 "default", &inv, result, sizeof(result),
                                 err_code, sizeof(err_code), err_msg, sizeof(err_msg));
    ASSERT_TRUE(ok);
    ASSERT_TRUE(strcmp(result, "already_running") == 0);
    printf("  [OK] Returned already_running\n");

    /* 3. Test controlled switch */
    printf("[TEST 3] Testing controlled switch to camera...\n");
    ok = ffmpeg_supervisor_start("inst_002", "lease_001", "usb-camera", "cam:55667788",
                                 "default", &inv, result, sizeof(result),
                                 err_code, sizeof(err_code), err_msg, sizeof(err_msg));
    ASSERT_TRUE(ok);
    ASSERT_TRUE(strcmp(result, "switched") == 0);

    ffmpeg_supervisor_get_info(&sinfo);
    ASSERT_TRUE(strcmp(sinfo.state, "running") == 0);
    ASSERT_TRUE(sinfo.ffmpeg_pid > 0);
    ASSERT_TRUE(sinfo.ffmpeg_pid != first_pid);
    ASSERT_TRUE(strcmp(sinfo.mode, "usb-camera") == 0);
    printf("  [OK] Switched cleanly to new PID=%lu\n", sinfo.ffmpeg_pid);

    /* 4. Test two-phase soft stop */
    printf("[TEST 4] Testing two-phase soft stop (phase 1: q\\n)...\n");
    ok = ffmpeg_supervisor_stop("inst_002", "lease_001", result, sizeof(result),
                                err_code, sizeof(err_code), err_msg, sizeof(err_msg));
    ASSERT_TRUE(ok);
    ASSERT_TRUE(strcmp(result, "stopped") == 0);

    ffmpeg_supervisor_get_info(&sinfo);
    ASSERT_TRUE(strcmp(sinfo.state, "stopped") == 0);
    ASSERT_TRUE(sinfo.ffmpeg_pid == 0);
    printf("  [OK] Stopped cleanly\n");

    /* 5. Test already_stopped */
    printf("[TEST 5] Testing already_stopped idempotency...\n");
    ok = ffmpeg_supervisor_stop("inst_002", "lease_001", result, sizeof(result),
                                err_code, sizeof(err_code), err_msg, sizeof(err_msg));
    ASSERT_TRUE(ok);
    ASSERT_TRUE(strcmp(result, "already_stopped") == 0);
    printf("  [OK] Returned already_stopped\n");

    /* 6. Test hard kill on hanging process */
    printf("[TEST 6] Testing hard kill phase 2 on hanging process...\n");
    SetEnvironmentVariableA("FAKE_FFMPEG_MODE", "hang");
    ok = ffmpeg_supervisor_start("inst_003", "lease_001", "desktop", "disp:11223344",
                                 "default", &inv, result, sizeof(result),
                                 err_code, sizeof(err_code), err_msg, sizeof(err_msg));
    ASSERT_TRUE(ok);
    ASSERT_TRUE(strcmp(result, "started") == 0);

    DWORD t0 = GetTickCount();
    ok = ffmpeg_supervisor_stop("inst_003", "lease_001", result, sizeof(result),
                                err_code, sizeof(err_code), err_msg, sizeof(err_msg));
    DWORD elapsed = GetTickCount() - t0;
    ASSERT_TRUE(ok);
    ASSERT_TRUE(strcmp(result, "stopped") == 0);
    ASSERT_TRUE(elapsed >= 4500); /* Soft stop timed out at ~5s, then hard kill succeeded */
    printf("  [OK] Hanging process killed in %lu ms\n", elapsed);
    SetEnvironmentVariableA("FAKE_FFMPEG_MODE", NULL);

    /* 7. Test crash detection & restart backoff */
    printf("[TEST 7] Testing crash detection...\n");
    SetEnvironmentVariableA("FAKE_FFMPEG_MODE", "crash");
    ok = ffmpeg_supervisor_start("inst_004", "lease_001", "desktop", "disp:11223344",
                                 "default", &inv, result, sizeof(result),
                                 err_code, sizeof(err_code), err_msg, sizeof(err_msg));
    ASSERT_TRUE(ok);
    Sleep(200); /* Allow process to crash */

    bool changed = false;
    char nstate[32] = { 0 };
    char nreason[64] = { 0 };
    ffmpeg_supervisor_tick(&inv, &changed, nstate, sizeof(nstate), nreason, sizeof(nreason));
    ASSERT_TRUE(changed);
    ASSERT_TRUE(strcmp(nstate, "restarting") == 0);
    printf("  [OK] Crash detected, transitioned to 'restarting'\n");
    SetEnvironmentVariableA("FAKE_FFMPEG_MODE", NULL);

    ffmpeg_supervisor_stop(NULL, NULL, result, sizeof(result), err_code, sizeof(err_code), err_msg, sizeof(err_msg));

    /* 8. Test reconciliation and PID reuse protection */
    printf("[TEST 8] Testing reconciliation & PID reuse protection...\n");
    char state_file[MAX_PATH];
    snprintf(state_file, sizeof(state_file), "%s\\l4desk\\state\\ffmpeg_state.json", test_dir);

    /* Case A: Reused PID with wrong creation time must NOT be killed */
    FILE* fState = NULL;
    fopen_s(&fState, state_file, "wb");
    if (fState) {
        /* Write current process PID but fictitious creation time */
        fprintf(fState,
            "{\n"
            "  \"pid\": %lu,\n"
            "  \"creation_time\": 999999999,\n"
            "  \"stream_instance_id\": \"inst_orphan_01\",\n"
            "  \"state\": \"running\"\n"
            "}\n", GetCurrentProcessId());
        fclose(fState);
    }
    ffmpeg_supervisor_reconcile();
    /* If protection works, current process is alive and we reach here! */
    printf("  [OK] Reused PID with different creation time preserved\n");

    /* Case B: Real orphaned child process with matching PID and creation time */
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcessW(NULL, fake_bin, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        FILETIME ftC, ftE, ftK, ftU;
        GetProcessTimes(pi.hProcess, &ftC, &ftE, &ftK, &ftU);
        uint64_t create_time = (((uint64_t)ftC.dwHighDateTime) << 32) | ftC.dwLowDateTime;

        fopen_s(&fState, state_file, "wb");
        if (fState) {
            fprintf(fState,
                "{\n"
                "  \"pid\": %lu,\n"
                "  \"creation_time\": %llu,\n"
                "  \"stream_instance_id\": \"inst_orphan_real\",\n"
                "  \"state\": \"running\"\n"
                "}\n", pi.dwProcessId, (unsigned long long)create_time);
            fclose(fState);
        }

        /* Now call reconcile: it must kill the orphaned child process */
        ffmpeg_supervisor_reconcile();

        DWORD child_exit = 0;
        GetExitCodeProcess(pi.hProcess, &child_exit);
        ASSERT_TRUE(child_exit != STILL_ACTIVE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        printf("  [OK] Orphaned child process correctly detected and terminated\n");
    }

    ffmpeg_supervisor_cleanup();
    printf("=== ALL ORCHESTRATOR TESTS PASSED ===\n");
    return 0;
}
