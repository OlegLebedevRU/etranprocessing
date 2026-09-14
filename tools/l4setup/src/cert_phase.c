#include "cert_phase.h"
#include "log.h"
#include "services.h"
#include "../res/resource.h"
#include "../../l4superv/src/hardware_fingerprint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <sddl.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

void cert_phase_add_warning(CertPhaseResult* res, const char* warn) {
    if (!res || !warn || res->warnings_count >= MAX_WARNINGS_COUNT) return;
    for (int i = 0; i < res->warnings_count; i++) {
        if (strcmp(res->warnings[i], warn) == 0) return;
    }
    strncpy_s(res->warnings[res->warnings_count++], 64, warn, _TRUNCATE);
}

static INT_PTR CALLBACK PinDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static wchar_t* pTargetPin = NULL;

    switch (msg) {
        case WM_INITDIALOG: {
            pTargetPin = (wchar_t*)lParam;
            // Center dialog on screen
            RECT rc, rcOwner;
            HWND hwndOwner = GetDesktopWindow();
            GetWindowRect(hwndOwner, &rcOwner);
            GetWindowRect(hDlg, &rc);
            SetWindowPos(hDlg, HWND_TOP,
                         (rcOwner.right - rcOwner.left - (rc.right - rc.left)) / 2,
                         (rcOwner.bottom - rcOwner.top - (rc.bottom - rc.top)) / 2,
                         0, 0, SWP_NOSIZE);
            SendDlgItemMessage(hDlg, IDC_PIN_EDIT, EM_SETLIMITTEXT, 32, 0);
            SetFocus(GetDlgItem(hDlg, IDC_PIN_EDIT));
            return FALSE;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == IDOK) {
                wchar_t pin_buf[64] = { 0 };
                GetDlgItemTextW(hDlg, IDC_PIN_EDIT, pin_buf, 64);
                // Trim leading/trailing whitespace
                wchar_t* p = pin_buf;
                while (*p == L' ' || *p == L'\t') p++;
                size_t len = wcslen(p);
                while (len > 0 && (p[len - 1] == L' ' || p[len - 1] == L'\t' || p[len - 1] == L'\r' || p[len - 1] == L'\n')) {
                    p[--len] = L'\0';
                }

                if (len != 6) {
                    MessageBoxW(hDlg, L"Please enter a 6-digit terminal PIN.",
                                L"Leo4 Setup", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                for (int i = 0; i < 6; i++) {
                    if (p[i] < L'0' || p[i] > L'9') {
                        MessageBoxW(hDlg, L"PIN must contain digits only (0-9).",
                                    L"Leo4 Setup", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                }

                if (pTargetPin) {
                    wcsncpy_s(pTargetPin, 64, p, _TRUNCATE);
                }
                EndDialog(hDlg, IDOK);
                return TRUE;
            } else if (wmId == IDC_BTN_SKIP) {
                int resp = MessageBoxW(hDlg,
                    L"Skipping activation will leave the terminal unactivated (activation_required).\nAre you sure you want to skip activation?",
                    L"Leo4 Setup - Skip Activation",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
                if (resp == IDYES) {
                    EndDialog(hDlg, IDC_BTN_SKIP);
                }
                return TRUE;
            } else if (wmId == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

static bool poll_proxy_ready(int timeout_seconds) {
    HINTERNET hSession = WinHttpOpen(L"l4setup/1.6.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    DWORD timeout = 2000;
    WinHttpSetTimeouts(hSession, timeout, timeout, timeout, timeout);

    int max_attempts = timeout_seconds * 2;
    bool ready = false;

    for (int i = 0; i < max_attempts; i++) {
        Sleep(500);

        HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", 18443, 0);
        if (!hConnect) continue;

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/_leo4/info", NULL,
                                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hRequest) {
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hRequest, NULL)) {
                DWORD status_code = 0;
                DWORD status_code_size = sizeof(status_code);
                WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_code_size, WINHTTP_NO_HEADER_INDEX);

                if (status_code == 200) {
                    char buf[2048] = { 0 };
                    DWORD bytes_read = 0;
                    if (WinHttpReadData(hRequest, buf, sizeof(buf) - 1, &bytes_read) && bytes_read > 0) {
                        buf[bytes_read] = '\0';
                        if (strstr(buf, "\"status\":\"ready\"") || strstr(buf, "\"status\": \"ready\"")) {
                            ready = true;
                            WinHttpCloseHandle(hRequest);
                            WinHttpCloseHandle(hConnect);
                            break;
                        }
                    }
                }
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }

    WinHttpCloseHandle(hSession);
    return ready;
}

static bool save_pending_pin(const wchar_t* dest_dir, const wchar_t* pin) {
    if (!dest_dir || !pin || pin[0] == L'\0') return false;

    char pin_utf8[64] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, pin, -1, pin_utf8, sizeof(pin_utf8), NULL, NULL);

    DATA_BLOB in_blob;
    in_blob.pbData = (BYTE*)pin_utf8;
    in_blob.cbData = (DWORD)strlen(pin_utf8);

    DATA_BLOB out_blob;
    memset(&out_blob, 0, sizeof(out_blob));

    if (!CryptProtectData(&in_blob, L"Leo4TerminalPin", NULL, NULL, NULL, CRYPTPROTECT_LOCAL_MACHINE, &out_blob)) {
        log_err("Failed to DPAPI-encrypt PIN code for pending_pin.json (error %lu)", GetLastError());
        SecureZeroMemory(pin_utf8, sizeof(pin_utf8));
        return false;
    }
    SecureZeroMemory(pin_utf8, sizeof(pin_utf8));

    DWORD b64_len = 0;
    CryptBinaryToStringA(out_blob.pbData, out_blob.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &b64_len);
    char* b64_pin = (char*)malloc(b64_len + 1);
    if (!b64_pin) {
        LocalFree(out_blob.pbData);
        return false;
    }
    CryptBinaryToStringA(out_blob.pbData, out_blob.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64_pin, &b64_len);
    LocalFree(out_blob.pbData);

    // Current UTC and +72h
    SYSTEMTIME stNow, stExp;
    GetSystemTime(&stNow);

    FILETIME ftNow;
    SystemTimeToFileTime(&stNow, &ftNow);
    ULARGE_INTEGER uli;
    uli.LowPart = ftNow.dwLowDateTime;
    uli.HighPart = ftNow.dwHighDateTime;
    uli.QuadPart += (ULONGLONG)72 * 3600ULL * 10000000ULL; // +72h

    FILETIME ftExp;
    ftExp.dwLowDateTime = uli.LowPart;
    ftExp.dwHighDateTime = uli.HighPart;
    FileTimeToSystemTime(&ftExp, &stExp);

    char time_now[32], time_exp[32];
    snprintf(time_now, sizeof(time_now), "%04u-%02u-%02uT%02u:%02u:%02uZ",
             stNow.wYear, stNow.wMonth, stNow.wDay, stNow.wHour, stNow.wMinute, stNow.wSecond);
    snprintf(time_exp, sizeof(time_exp), "%04u-%02u-%02uT%02u:%02u:%02uZ",
             stExp.wYear, stExp.wMonth, stExp.wDay, stExp.wHour, stExp.wMinute, stExp.wSecond);

    wchar_t target_path[MAX_PATH];
    swprintf_s(target_path, MAX_PATH, L"%ls\\pending_pin.json", dest_dir);

    FILE* fp = NULL;
    if (_wfopen_s(&fp, target_path, L"wb") != 0 || !fp) {
        free(b64_pin);
        return false;
    }

    fprintf(fp, "{\n");
    fprintf(fp, "  \"schema\": 1,\n");
    fprintf(fp, "  \"created_at\": \"%s\",\n", time_now);
    fprintf(fp, "  \"expires_at\": \"%s\",\n", time_exp);
    fprintf(fp, "  \"pin_dpapi\": \"%s\"\n", b64_pin);
    fprintf(fp, "}\n");
    fclose(fp);
    free(b64_pin);

    // Set ACL: SYSTEM and Administrators only (SDDL: D:P(A;;GA;;;SY)(A;;GA;;;BA))
    PSECURITY_DESCRIPTOR pSD = NULL;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;SY)(A;;GA;;;BA)",
            SDDL_REVISION_1,
            &pSD,
            NULL)) {
        SetFileSecurityW(target_path, DACL_SECURITY_INFORMATION, pSD);
        LocalFree(pSD);
    }

    log_info("Saved encrypted PIN into %ls (TTL: 72h, local machine DPAPI).", target_path);
    return true;
}

static bool run_l4pin_enrollment(
    const wchar_t* dest_dir,
    const wchar_t* pin,
    bool force,
    int* out_exit_code,
    bool* out_rejected_by_ca
) {
    if (out_exit_code) *out_exit_code = 1;
    if (out_rejected_by_ca) *out_rejected_by_ca = false;

    wchar_t exe_path[MAX_PATH];
    swprintf_s(exe_path, MAX_PATH, L"%ls\\l4pin\\l4pin.exe", dest_dir);

    // Build command line: "l4pin.exe" [--force] <PIN>
    wchar_t cmd_line[512];
    if (force) {
        swprintf_s(cmd_line, 512, L"\"%ls\" --force %ls", exe_path, pin);
    } else {
        swprintf_s(cmd_line, 512, L"\"%ls\" %ls", exe_path, pin);
    }

    // Pipes for stdout/stderr
    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return false;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    log_info("Executing l4pin enrollment tool (pin masked)...");
    if (!CreateProcessW(NULL, cmd_line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        log_err("Failed to execute %ls (error %lu)", exe_path, err);
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        SecureZeroMemory(cmd_line, sizeof(cmd_line));
        return false;
    }
    SecureZeroMemory(cmd_line, sizeof(cmd_line));
    CloseHandle(hWritePipe);

    // Read output
    char output_buf[8192] = { 0 };
    DWORD total_read = 0;
    DWORD bytes_read = 0;
    char chunk[512];
    while (ReadFile(hReadPipe, chunk, sizeof(chunk) - 1, &bytes_read, NULL) && bytes_read > 0) {
        if (total_read + bytes_read < sizeof(output_buf)) {
            memcpy(output_buf + total_read, chunk, bytes_read);
            total_read += bytes_read;
        }
    }
    output_buf[total_read] = '\0';
    CloseHandle(hReadPipe);

    WaitForSingleObject(pi.hProcess, 30000);
    DWORD child_exit = 0;
    GetExitCodeProcess(pi.hProcess, &child_exit);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (out_exit_code) *out_exit_code = (int)child_exit;

    // Log output with PIN masked
    log_info("l4pin output: %s", output_buf);

    if (child_exit == 0) {
        return true;
    }

    // Check if rejected by CA (e.g. invalid PIN)
    if (strstr(output_buf, "CHECK failed") || strstr(output_buf, "Invalid PIN") ||
        strstr(output_buf, "invalid pin") || strstr(output_buf, "PIN not found")) {
        if (out_rejected_by_ca) *out_rejected_by_ca = true;
    }

    return false;
}

bool cert_phase_execute(
    const wchar_t* dest_dir,
    CliOptions* cli_opts,
    HINSTANCE hInstance,
    CertPhaseResult* out_result
) {
    if (!out_result) return false;
    memset(out_result, 0, sizeof(CertPhaseResult));

    log_info("Entering Phase 3: Certificate Discovery and PIN provisioning...");

    // 1. Determine expected_sn from state.json if hw_fingerprint matches
    wchar_t w_expected_sn[64] = { 0 };
    const wchar_t* p_expected_sn = NULL;

    wchar_t state_path[MAX_PATH];
    swprintf_s(state_path, MAX_PATH, L"%ls\\state.json", dest_dir);

    char state_sn[64] = { 0 };
    char state_hw_fp[128] = { 0 };

    FILE* fp = NULL;
    if (_wfopen_s(&fp, state_path, L"r") == 0 && fp) {
        char buf[8192] = { 0 };
        fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);

        const char* p_sn = strstr(buf, "\"sn\": \"");
        if (!p_sn) p_sn = strstr(buf, "\"sn\":\"");
        if (p_sn) {
            p_sn = strchr(p_sn, ':');
            if (p_sn) p_sn = strchr(p_sn, '"');
            if (p_sn) {
                p_sn++;
                size_t k = 0;
                while (*p_sn && *p_sn != '"' && k + 1 < sizeof(state_sn)) state_sn[k++] = *p_sn++;
                state_sn[k] = '\0';
            }
        }

        const char* p_fp = strstr(buf, "\"hw_fingerprint\": \"");
        if (!p_fp) p_fp = strstr(buf, "\"hw_fingerprint\":\"");
        if (p_fp) {
            p_fp = strchr(p_fp, ':');
            if (p_fp) p_fp = strchr(p_fp, '"');
            if (p_fp) {
                p_fp++;
                size_t k = 0;
                while (*p_fp && *p_fp != '"' && k + 1 < sizeof(state_hw_fp)) state_hw_fp[k++] = *p_fp++;
                state_hw_fp[k] = '\0';
            }
        }
    }

    if (state_sn[0] != '\0' && state_hw_fp[0] != '\0') {
        char current_hw_fp[128] = { 0 };
        if (hw_get_fingerprint(current_hw_fp, sizeof(current_hw_fp))) {
            if (_stricmp(current_hw_fp, state_hw_fp) == 0) {
                MultiByteToWideChar(CP_UTF8, 0, state_sn, -1, w_expected_sn, 64);
                p_expected_sn = w_expected_sn;
                log_info("Hardware fingerprint matched (%s). expected_sn: %s", current_hw_fp, state_sn);
            } else {
                log_warn("Hardware fingerprint changed (saved: %s, current: %s). Clearing expected_sn.",
                         state_hw_fp, current_hw_fp);
                cert_phase_add_warning(out_result, "hw_fingerprint_changed");
                p_expected_sn = NULL;
            }
        }
    }

    // 2. Discover certificate
    cert_info info;
    memset(&info, 0, sizeof(info));
    cert_state st = cert_discover(p_expected_sn, &info);
    out_result->state = st;
    strncpy_s(out_result->thumbprint, 64, info.thumbprint_hex, _TRUNCATE);
    strncpy_s(out_result->sn, 64, info.sn, _TRUNCATE);
    strncpy_s(out_result->not_after, 64, info.not_after_utc, _TRUNCATE);
    out_result->days_left = info.days_left;

    log_info("Cert discovery state: %s, thumbprint: %s, sn: %s, days_left: %d",
             cert_state_to_str(st), info.thumbprint_hex, info.sn, info.days_left);

    bool need_pin = false;
    bool do_force_reissue = false;

    if (st == CERT_VALID) {
        if (cli_opts->force_reissue && cli_opts->pin_specified) {
            log_info("Valid certificate found, but --force-reissue requested with PIN. Proceeding to reissue.");
            need_pin = true;
            do_force_reissue = true;
        } else {
            out_result->reused = true;
            out_result->reissued = false;
            if (cli_opts->pin_specified) {
                log_info("Certificate is valid and --force-reissue was not specified. Ignoring provided PIN.");
                cert_phase_add_warning(out_result, "cert_reused_pin_ignored");
            }
            out_result->exit_code = 0;
            strcpy_s(out_result->status, sizeof(out_result->status), "ready");
            return true;
        }
    } else if (st == CERT_EXPIRING) {
        if (cli_opts->pin_specified) {
            log_info("Expiring certificate found (< 30 days left) and PIN provided. Proceeding with renewal without requiring --force-reissue.");
            need_pin = true;
            do_force_reissue = true;
        } else {
            out_result->reused = true;
            out_result->reissued = false;
            cert_phase_add_warning(out_result, "cert_expiring");
            out_result->exit_code = 0;
            strcpy_s(out_result->status, sizeof(out_result->status), "ready");
            return true;
        }
    } else {
        // CERT_BROKEN or CERT_ABSENT
        need_pin = true;
    }

    if (!need_pin) {
        out_result->exit_code = 0;
        strcpy_s(out_result->status, sizeof(out_result->status), "ready");
        return true;
    }

    // 3. Acquire PIN if not provided
    if (!cli_opts->pin_specified) {
        if (cli_opts->silent || cli_opts->no_pin) {
            log_info("No PIN provided and running in silent/no-pin mode. Entering activation_required.");
            out_result->exit_code = 10;
            strcpy_s(out_result->status, sizeof(out_result->status), "activation_required");
            return true;
        }

        // GUI prompt
        INT_PTR res = DialogBoxParamW(
            hInstance,
            MAKEINTRESOURCEW(IDD_PIN_DIALOG),
            NULL,
            PinDlgProc,
            (LPARAM)cli_opts->pin
        );

        if (res == IDOK && cli_opts->pin[0] != L'\0') {
            cli_opts->pin_specified = true;
        } else if (res == IDC_BTN_SKIP) {
            log_info("User skipped PIN entry. Entering activation_required.");
            out_result->exit_code = 10;
            strcpy_s(out_result->status, sizeof(out_result->status), "activation_required");
            return true;
        } else {
            log_info("User cancelled PIN entry dialog.");
            out_result->exit_code = 31;
            strcpy_s(out_result->status, sizeof(out_result->status), "cancelled");
            return false;
        }
    }

    // 4. Enroll with l4pin up to 3 attempts with exponential backoff (2s, 4s, 8s) on network errors
    const int backoffs_ms[] = { 2000, 4000, 8000 };
    bool enrollment_ok = false;
    bool pin_rejected_by_ca = false;
    int last_exit_code = 0;

    for (int attempt = 0; attempt < 3; attempt++) {
        log_info("Attempt %d of 3 to enroll certificate via l4pin...", attempt + 1);
        if (run_l4pin_enrollment(dest_dir, cli_opts->pin, do_force_reissue, &last_exit_code, &pin_rejected_by_ca)) {
            enrollment_ok = true;
            break;
        }

        if (pin_rejected_by_ca) {
            log_err("PIN was explicitly rejected by CA (error %d).", last_exit_code);
            out_result->exit_code = 25;
            strcpy_s(out_result->status, sizeof(out_result->status), "failed");
            cli_clean_pin(cli_opts);
            return false;
        }

        if (attempt < 2) {
            log_warn("Enrollment failed (network/server error). Retrying in %d ms...", backoffs_ms[attempt]);
            Sleep(backoffs_ms[attempt]);
        }
    }

    if (!enrollment_ok) {
        log_warn("CA unavailable after 3 attempts. Saving PIN to pending_pin.json (ready_for_online)...");
        save_pending_pin(dest_dir, cli_opts->pin);
        cli_clean_pin(cli_opts);

        out_result->exit_code = 11;
        strcpy_s(out_result->status, sizeof(out_result->status), "ready_for_online");
        return true;
    }

    // Enrollment succeeded: wipe PIN immediately
    cli_clean_pin(cli_opts);
    out_result->reissued = true;

    // 5. Signal L4Superv (SERVICE_CONTROL 128) and wait for _leo4/info ready up to 15 seconds
    log_info("Sending SERVICE_CONTROL 128 to L4Superv...");
    services_control_l4superv(128);

    log_info("Waiting for _leo4/info to report ready status (up to 15s)...");
    if (!poll_proxy_ready(15)) {
        log_err("Timeout waiting for _leo4/info status ready after certificate enrollment.");
        out_result->exit_code = 26;
        strcpy_s(out_result->status, sizeof(out_result->status), "failed");
        return false;
    }

    // Refresh certificate info
    cert_info new_info;
    memset(&new_info, 0, sizeof(new_info));
    cert_state new_st = cert_discover(NULL, &new_info);
    out_result->state = new_st;
    strncpy_s(out_result->thumbprint, 64, new_info.thumbprint_hex, _TRUNCATE);
    strncpy_s(out_result->sn, 64, new_info.sn, _TRUNCATE);
    strncpy_s(out_result->not_after, 64, new_info.not_after_utc, _TRUNCATE);
    out_result->days_left = new_info.days_left;

    log_info("Certificate successfully enrolled and verified: thumbprint %s, SN %s",
             new_info.thumbprint_hex, new_info.sn);
    out_result->exit_code = 0;
    strcpy_s(out_result->status, sizeof(out_result->status), "ready");
    return true;
}
