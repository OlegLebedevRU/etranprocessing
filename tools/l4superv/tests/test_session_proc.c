#include "session_proc.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        printf("[FAIL] Assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #x); \
        exit(1); \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("[FAIL] Assertion failed at %s:%d: %s == %s (actual %lld != %lld)\n", \
               __FILE__, __LINE__, #a, #b, (long long)(a), (long long)(b)); \
        exit(1); \
    } \
} while (0)

int main(void) {
    printf("=======================================================\n");
    printf("Running l4superv session_proc unit tests\n");
    printf("=======================================================\n");

    // 1. Test status strings
    printf("[1/5] Testing sp_launch_status_to_string...\n");
    ASSERT_TRUE(strcmp(sp_launch_status_to_string(SP_TOKEN_OK), "ok") == 0);
    ASSERT_TRUE(strcmp(sp_launch_status_to_string(SP_TOKEN_ERR_NO_SESSION), "no_active_console_session") == 0);
    ASSERT_TRUE(strcmp(sp_launch_status_to_string(SP_TOKEN_ERR_INSUFFICIENT_INTEGRITY), "insufficient_integrity") == 0);
    ASSERT_TRUE(strcmp(sp_launch_status_to_string(SP_TOKEN_ERR_INVALID_SESSION), "invalid_session") == 0);
    ASSERT_TRUE(strcmp(sp_launch_status_to_string(SP_TOKEN_ERR_INVALID_SID), "invalid_sid") == 0);

    // 2. Test token integrity level query
    printf("[2/5] Testing sp_get_token_integrity_level...\n");
    ASSERT_EQ(sp_get_token_integrity_level(NULL), 0);
    ASSERT_EQ(sp_get_token_integrity_level(INVALID_HANDLE_VALUE), 0);

    HANDLE hCurrentToken = NULL;
    ASSERT_TRUE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hCurrentToken));
    DWORD currentIL = sp_get_token_integrity_level(hCurrentToken);
    ASSERT_TRUE(currentIL > 0);
    printf("       Current process token IL: 0x%04lx\n", currentIL);

    // 3. Test token selection with current token and session validation
    printf("[3/5] Testing sp_select_target_token session validation...\n");
    DWORD current_session = 0;
    ASSERT_TRUE(ProcessIdToSessionId(GetCurrentProcessId(), &current_session));

    HANDLE hSelected = NULL;
    DWORD selectedIL = 0;

    // Test with matching session ID
    SpLaunchStatus status = sp_select_target_token(hCurrentToken, current_session, false, &hSelected, &selectedIL);
    ASSERT_EQ(status, SP_TOKEN_OK);
    ASSERT_TRUE(hSelected != NULL);
    if (hSelected && hSelected != hCurrentToken) CloseHandle(hSelected);
    hSelected = NULL;

    // Test with mismatched session ID (e.g. current_session + 12345)
    status = sp_select_target_token(hCurrentToken, current_session + 12345, false, &hSelected, &selectedIL);
    ASSERT_EQ(status, SP_TOKEN_ERR_INVALID_SESSION);

    // Test with NULL handle
    status = sp_select_target_token(NULL, current_session, false, &hSelected, &selectedIL);
    ASSERT_EQ(status, SP_TOKEN_ERR_QUERY_USER_TOKEN);

    // 4. Test TokenElevationType branches
    printf("[4/5] Testing TokenElevationType branches and High IL requirement...\n");
    TOKEN_ELEVATION_TYPE elevType = TokenElevationTypeDefault;
    DWORD len = 0;
    GetTokenInformation(hCurrentToken, TokenElevationType, &elevType, sizeof(elevType), &len);

    if (elevType == TokenElevationTypeDefault && currentIL < 0x3000) {
        // Standard user token without linked token capability: strict_high_il MUST fail with insufficient_integrity
        status = sp_select_target_token(hCurrentToken, current_session, true, &hSelected, &selectedIL);
        ASSERT_EQ(status, SP_TOKEN_ERR_INSUFFICIENT_INTEGRITY);
        printf("       Confirmed: Standard user + strict_high_il -> insufficient_integrity without prompt\n");

        // Adaptive mode: standard user succeeds with default IL
        status = sp_select_target_token(hCurrentToken, current_session, false, &hSelected, &selectedIL);
        ASSERT_EQ(status, SP_TOKEN_OK);
        ASSERT_EQ(selectedIL, currentIL);
        printf("       Confirmed: Standard user + adaptive mode -> succeeds with default IL\n");
        if (hSelected && hSelected != hCurrentToken) CloseHandle(hSelected);
        hSelected = NULL;
    } else if (elevType == TokenElevationTypeLimited) {
        // Limited token (split-token admin): sp_select_target_token extracts linked token to reach High IL
        status = sp_select_target_token(hCurrentToken, current_session, false, &hSelected, &selectedIL);
        ASSERT_EQ(status, SP_TOKEN_OK);
        ASSERT_TRUE(selectedIL >= 0x3000);
        printf("       Confirmed: Limited token elevates to High IL via TokenLinkedToken in adaptive mode\n");
        if (hSelected && hSelected != hCurrentToken) CloseHandle(hSelected);
        hSelected = NULL;
    } else if (elevType == TokenElevationTypeFull) {
        // Full elevated token: strict_high_il succeeds without switching to linked token
        status = sp_select_target_token(hCurrentToken, current_session, true, &hSelected, &selectedIL);
        ASSERT_EQ(status, SP_TOKEN_OK);
        ASSERT_TRUE(selectedIL >= 0x3000);
        printf("       Confirmed: Full elevated token preserved with High IL\n");
        if (hSelected && hSelected != hCurrentToken) CloseHandle(hSelected);
        hSelected = NULL;
    }

    CloseHandle(hCurrentToken);

    // 5. Test sp_start_in_session_ex input safety
    printf("[5/5] Testing sp_start_in_session_ex input validation...\n");
    PROCESS_INFORMATION pi = { 0 };
    HANDLE hJob = NULL;
    SpLaunchStatus launch_st = SP_TOKEN_OK;

    // Session 0 reject
    BOOL ret = sp_start_in_session_ex(0, L"C:\\Windows\\System32\\cmd.exe", NULL, NULL, false, &pi, &hJob, &launch_st);
    ASSERT_TRUE(!ret);
    ASSERT_EQ(launch_st, SP_TOKEN_ERR_NO_SESSION);

    // Relative path reject
    ret = sp_start_in_session_ex(current_session, L"cmd.exe", NULL, NULL, false, &pi, &hJob, &launch_st);
    ASSERT_TRUE(!ret);
    ASSERT_EQ(launch_st, SP_TOKEN_ERR_PROCESS_CREATE_FAILED);

    // NULL exe reject
    ret = sp_start_in_session_ex(current_session, NULL, NULL, NULL, false, &pi, &hJob, &launch_st);
    ASSERT_TRUE(!ret);
    ASSERT_EQ(launch_st, SP_TOKEN_ERR_PROCESS_CREATE_FAILED);

    printf("=======================================================\n");
    printf("ALL session_proc UNIT TESTS PASSED SUCCESSFULLY!\n");
    printf("=======================================================\n");
    return 0;
}
