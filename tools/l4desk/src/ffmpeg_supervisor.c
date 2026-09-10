#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "ffmpeg_supervisor.h"
#include "ffmpeg_cmdline.h"
#include "desktop_state.h"
#include "json_min.h"
#include "log.h"
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_LOG_SIZE (5 * 1024 * 1024) /* 5 MB */

typedef struct {
    char stream_instance_id[64];
    char lease_id[64];
    char mode[32];
    char source_id[64];
    char profile[32];
    RECT desktop_rect;
    DWORD session_id;
    char state[32];
    char reason[64];
    DWORD ffmpeg_pid;
    uint64_t ffmpeg_start_time;
    uint64_t started_at;
    int restart_count;
    time_t last_restart_time;
    time_t restart_window_start;
    time_t last_progress_time;

    HANDLE hProcess;
    HANDLE hJob;
    HANDLE hStdinWrite;
    HANDLE hStdoutRead;
    HANDLE hReaderThread;
} FFmpegSupervisorState;

static FFmpegSupervisorState g_sup;
static CRITICAL_SECTION g_sup_cs;
static bool g_sup_cs_inited = false;
static char g_base_path[MAX_PATH] = "C:\\l4tools";
static char g_sn[64] = "UNKNOWN";
static wchar_t g_custom_ffmpeg_binary[MAX_PATH] = { 0 };
static HANDLE g_hFfmpegMutex = NULL;

static void ensure_dir_exists(const char* dir) {
    char tmp[MAX_PATH];
    strcpy_s(tmp, sizeof(tmp), dir);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '\\' || *p == '/') {
            char save = *p;
            *p = '\0';
            CreateDirectoryA(tmp, NULL);
            *p = save;
        }
    }
    CreateDirectoryA(tmp, NULL);
}

static void get_state_file_path(char* out, size_t max_len) {
    snprintf(out, max_len, "%s\\l4desk\\state\\ffmpeg_state.json", g_base_path);
}

static void get_log_dir_path(char* out, size_t max_len) {
    snprintf(out, max_len, "%s\\ffmpeg\\log", g_base_path);
}

static void rotate_logs(const char* base_log) {
    char old_path[MAX_PATH];
    char new_path[MAX_PATH];

    snprintf(old_path, sizeof(old_path), "%s.4", base_log);
    DeleteFileA(old_path);

    for (int i = 3; i >= 1; i--) {
        snprintf(old_path, sizeof(old_path), "%s.%d", base_log, i);
        snprintf(new_path, sizeof(new_path), "%s.%d", base_log, i + 1);
        MoveFileA(old_path, new_path);
    }
    snprintf(new_path, sizeof(new_path), "%s.1", base_log);
    MoveFileA(base_log, new_path);
}

typedef struct {
    HANDLE hStdoutRead;
    char log_file[MAX_PATH];
    time_t* p_last_progress_time;
} LogReaderContext;

static DWORD WINAPI log_reader_thread(LPVOID lpParam) {
    LogReaderContext* ctx = (LogReaderContext*)lpParam;
    if (!ctx) return 0;

    FILE* fLog = NULL;
    fopen_s(&fLog, ctx->log_file, "a+b");

    char buf[2048];
    DWORD bytesRead = 0;

    while (ReadFile(ctx->hStdoutRead, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buf[bytesRead] = '\0';

        /* Update progress if frame=, out_time_ms=, or fps= is detected */
        if (strstr(buf, "frame=") || strstr(buf, "out_time_ms=") || strstr(buf, "fps=")) {
            if (ctx->p_last_progress_time) {
                *ctx->p_last_progress_time = time(NULL);
            }
        }

        if (fLog) {
            fwrite(buf, 1, bytesRead, fLog);
            fflush(fLog);

            long sz = ftell(fLog);
            if (sz >= MAX_LOG_SIZE) {
                fclose(fLog);
                rotate_logs(ctx->log_file);
                fopen_s(&fLog, ctx->log_file, "a+b");
            }
        }
    }

    if (fLog) fclose(fLog);
    free(ctx);
    return 0;
}

static void save_state_file(void) {
    char state_file[MAX_PATH];
    get_state_file_path(state_file, sizeof(state_file));

    char state_dir[MAX_PATH];
    snprintf(state_dir, sizeof(state_dir), "%s\\l4desk\\state", g_base_path);
    ensure_dir_exists(state_dir);

    char tmp_file[MAX_PATH];
    snprintf(tmp_file, sizeof(tmp_file), "%s.tmp", state_file);

    FILE* f = NULL;
    if (fopen_s(&f, tmp_file, "wb") == 0 && f) {
        fprintf(f,
            "{\n"
            "  \"pid\": %u,\n"
            "  \"creation_time\": %llu,\n"
            "  \"stream_instance_id\": \"%s\",\n"
            "  \"session_id\": %u,\n"
            "  \"mode\": \"%s\",\n"
            "  \"source_id\": \"%s\",\n"
            "  \"state\": \"%s\",\n"
            "  \"owner\": \"l4desk\",\n"
            "  \"sn\": \"%s\"\n"
            "}\n",
            g_sup.ffmpeg_pid,
            (unsigned long long)g_sup.ffmpeg_start_time,
            g_sup.stream_instance_id,
            g_sup.session_id,
            g_sup.mode,
            g_sup.source_id,
            g_sup.state,
            g_sn
        );
        fclose(f);
        MoveFileExA(tmp_file, state_file, MOVEFILE_REPLACE_EXISTING);
    }
}

static void wait_udp_ports_released(void) {
    Sleep(100);
}

static bool stop_active_process_internal(void) {
    if (!g_sup.hProcess) {
        strcpy_s(g_sup.state, sizeof(g_sup.state), "stopped");
        g_sup.ffmpeg_pid = 0;
        g_sup.ffmpeg_start_time = 0;
        save_state_file();
        return true;
    }

    strcpy_s(g_sup.state, sizeof(g_sup.state), "stopping");
    save_state_file();

    log_info("Stopping FFmpeg process (PID=%u, soft stop phase 1)...", g_sup.ffmpeg_pid);

    /* Phase 1: Soft stop via 'q\n' */
    if (g_sup.hStdinWrite) {
        DWORD written = 0;
        WriteFile(g_sup.hStdinWrite, "q\n", 2, &written, NULL);
        CloseHandle(g_sup.hStdinWrite);
        g_sup.hStdinWrite = NULL;
    }

    DWORD wait_res = WaitForSingleObject(g_sup.hProcess, 5000);
    if (wait_res == WAIT_TIMEOUT) {
        log_warn("FFmpeg PID=%u did not exit in 5s. Phase 2: hard kill...", g_sup.ffmpeg_pid);
        if (g_sup.hJob) {
            TerminateJobObject(g_sup.hJob, 1);
        }
        TerminateProcess(g_sup.hProcess, 1);
        WaitForSingleObject(g_sup.hProcess, 3000);
    }

    if (g_sup.hReaderThread) {
        WaitForSingleObject(g_sup.hReaderThread, 2000);
        CloseHandle(g_sup.hReaderThread);
        g_sup.hReaderThread = NULL;
    }

    if (g_sup.hStdoutRead) {
        CloseHandle(g_sup.hStdoutRead);
        g_sup.hStdoutRead = NULL;
    }

    CloseHandle(g_sup.hProcess);
    g_sup.hProcess = NULL;

    if (g_sup.hJob) {
        CloseHandle(g_sup.hJob);
        g_sup.hJob = NULL;
    }

    wait_udp_ports_released();

    strcpy_s(g_sup.state, sizeof(g_sup.state), "stopped");
    g_sup.ffmpeg_pid = 0;
    g_sup.ffmpeg_start_time = 0;
    save_state_file();

    log_info("FFmpeg stopped and resources released.");
    return true;
}

static void ffmpeg_supervisor_ensure_inited(void) {
    if (!g_sup_cs_inited) {
        InitializeCriticalSection(&g_sup_cs);
        g_sup_cs_inited = true;
        memset(&g_sup, 0, sizeof(g_sup));
        strcpy_s(g_sup.state, sizeof(g_sup.state), "stopped");
    }
}

void ffmpeg_supervisor_init(const char* base_path, const char* sn) {
    ffmpeg_supervisor_ensure_inited();

    EnterCriticalSection(&g_sup_cs);
    memset(&g_sup, 0, sizeof(g_sup));
    strcpy_s(g_sup.state, sizeof(g_sup.state), "stopped");

    if (base_path && base_path[0] != '\0') {
        strcpy_s(g_base_path, sizeof(g_base_path), base_path);
    }
    if (sn && sn[0] != '\0') {
        strcpy_s(g_sn, sizeof(g_sn), sn);
    }

    /* Named global mutex */
    wchar_t mutex_name[128];
    swprintf_s(mutex_name, sizeof(mutex_name) / sizeof(wchar_t),
               L"Global\\L4Desk_FFmpeg_%hs", g_sn);
    g_hFfmpegMutex = CreateMutexW(NULL, FALSE, mutex_name);

    LeaveCriticalSection(&g_sup_cs);
}

void ffmpeg_supervisor_cleanup(void) {
    if (!g_sup_cs_inited) return;

    EnterCriticalSection(&g_sup_cs);
    stop_active_process_internal();
    if (g_hFfmpegMutex) {
        CloseHandle(g_hFfmpegMutex);
        g_hFfmpegMutex = NULL;
    }
    LeaveCriticalSection(&g_sup_cs);

    DeleteCriticalSection(&g_sup_cs);
    g_sup_cs_inited = false;
}

void ffmpeg_supervisor_set_custom_binary(const wchar_t* path) {
    if (path) {
        wcscpy_s(g_custom_ffmpeg_binary, MAX_PATH, path);
    } else {
        g_custom_ffmpeg_binary[0] = L'\0';
    }
}

void ffmpeg_supervisor_reconcile(void) {
    EnterCriticalSection(&g_sup_cs);

    char state_file[MAX_PATH];
    get_state_file_path(state_file, sizeof(state_file));

    FILE* f = NULL;
    if (fopen_s(&f, state_file, "rb") == 0 && f) {
        char buf[2048] = { 0 };
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = '\0';

        int pid = 0;
        int64_t creation_time = 0;
        char state[32] = { 0 };
        char stream_id[64] = { 0 };

        json_extract_int(buf, "pid", &pid);
        json_extract_int64(buf, "creation_time", &creation_time);
        json_extract_str(buf, "state", state, sizeof(state));
        json_extract_str(buf, "stream_instance_id", stream_id, sizeof(stream_id));

        if (pid > 0 && strcmp(state, "running") == 0) {
            log_info("Reconciling orphaned FFmpeg PID=%d from state file...", pid);
            HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE, FALSE, (DWORD)pid);
            if (hProc) {
                FILETIME ftC, ftE, ftK, ftU;
                if (GetProcessTimes(hProc, &ftC, &ftE, &ftK, &ftU)) {
                    uint64_t actual_c = (((uint64_t)ftC.dwHighDateTime) << 32) | ftC.dwLowDateTime;
                    if (actual_c == (uint64_t)creation_time) {
                        log_warn("Confirmed orphaned process PID=%d creation_time matches. Terminating...", pid);
                        TerminateProcess(hProc, 1);
                        WaitForSingleObject(hProc, 3000);
                    } else {
                        log_info("Process PID=%d has different creation time (PID reused). Skipping.", pid);
                    }
                }
                CloseHandle(hProc);
            }
        }
    }

    strcpy_s(g_sup.state, sizeof(g_sup.state), "stopped");
    g_sup.ffmpeg_pid = 0;
    save_state_file();

    LeaveCriticalSection(&g_sup_cs);
}

static bool launch_ffmpeg_process(const wchar_t* cmdline,
                                  const char* stream_instance_id,
                                  char* out_err_code, size_t max_err_code,
                                  char* out_err_msg, size_t max_err_msg) {
    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hStdinRead = NULL;
    HANDLE hStdinWrite = NULL;
    if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0)) {
        strcpy_s(out_err_code, max_err_code, "ffmpeg_integrity");
        strcpy_s(out_err_msg, max_err_msg, "Failed to create stdin pipe");
        return false;
    }
    SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);

    HANDLE hStdoutRead = NULL;
    HANDLE hStdoutWrite = NULL;
    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
        CloseHandle(hStdinRead);
        CloseHandle(hStdinWrite);
        strcpy_s(out_err_code, max_err_code, "ffmpeg_integrity");
        strcpy_s(out_err_msg, max_err_msg, "Failed to create stdout pipe");
        return false;
    }
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = hStdinRead;
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStdoutWrite;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    wchar_t cmd_buf[4096];
    wcscpy_s(cmd_buf, sizeof(cmd_buf) / sizeof(wchar_t), cmdline);

    DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED;

    BOOL ok = CreateProcessW(NULL, cmd_buf, NULL, NULL, TRUE, flags, NULL, NULL, &si, &pi);
    CloseHandle(hStdinRead);
    CloseHandle(hStdoutWrite);

    if (!ok) {
        CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead);
        DWORD err = GetLastError();
        strcpy_s(out_err_code, max_err_code, "ffmpeg_integrity");
        snprintf(out_err_msg, max_err_msg, "CreateProcessW failed with error %lu", err);
        return false;
    }

    // Try Job object assignment
    HANDLE hJob = CreateJobObjectW(NULL, NULL);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
        ZeroMemory(&jeli, sizeof(jeli));
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        AssignProcessToJobObject(hJob, pi.hProcess);
    }

    FILETIME ftC, ftE, ftK, ftU;
    GetProcessTimes(pi.hProcess, &ftC, &ftE, &ftK, &ftU);
    uint64_t create_time = (((uint64_t)ftC.dwHighDateTime) << 32) | ftC.dwLowDateTime;

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    g_sup.hProcess = pi.hProcess;
    g_sup.hJob = hJob;
    g_sup.hStdinWrite = hStdinWrite;
    g_sup.hStdoutRead = hStdoutRead;
    g_sup.ffmpeg_pid = pi.dwProcessId;
    g_sup.ffmpeg_start_time = create_time;
    g_sup.started_at = (uint64_t)time(NULL);
    g_sup.last_progress_time = time(NULL);

    // Start background log & progress reader
    char log_dir[MAX_PATH];
    get_log_dir_path(log_dir, sizeof(log_dir));
    ensure_dir_exists(log_dir);

    LogReaderContext* rctx = (LogReaderContext*)malloc(sizeof(LogReaderContext));
    if (rctx) {
        rctx->hStdoutRead = hStdoutRead;
        snprintf(rctx->log_file, sizeof(rctx->log_file), "%s\\ffmpeg_%s.log",
                 log_dir, stream_instance_id);
        rctx->p_last_progress_time = &g_sup.last_progress_time;
        g_sup.hReaderThread = CreateThread(NULL, 0, log_reader_thread, rctx, 0, NULL);
    }

    strcpy_s(g_sup.state, sizeof(g_sup.state), "running");
    g_sup.reason[0] = '\0';
    save_state_file();

    log_info("FFmpeg started successfully: PID=%u, stream_instance_id=%s",
             g_sup.ffmpeg_pid, stream_instance_id);
    return true;
}

bool ffmpeg_supervisor_start(const char* stream_instance_id,
                             const char* lease_id,
                             const char* mode,
                             const char* source_id,
                             const char* profile,
                             const SystemInventory* inv,
                             char* out_result, size_t max_result,
                             char* out_err_code, size_t max_err_code,
                             char* out_err_msg, size_t max_err_msg) {
    ffmpeg_supervisor_ensure_inited();
    EnterCriticalSection(&g_sup_cs);

    if (strcmp(g_sup.state, "starting") == 0 || strcmp(g_sup.state, "stopping") == 0) {
        strcpy_s(out_err_code, max_err_code, "busy_transition");
        strcpy_s(out_err_msg, max_err_msg, "Transition in progress");
        LeaveCriticalSection(&g_sup_cs);
        return false;
    }

    if (!inventory_is_profile_allowed(inv, profile)) {
        strcpy_s(out_err_code, max_err_code, "invalid_profile");
        snprintf(out_err_msg, max_err_msg, "Profile '%s' is not allowed", profile ? profile : "");
        LeaveCriticalSection(&g_sup_cs);
        return false;
    }

    DisplayInfo target_disp;
    ZeroMemory(&target_disp, sizeof(target_disp));

    CameraInfo target_cam;
    ZeroMemory(&target_cam, sizeof(target_cam));

    if (mode && _stricmp(mode, "desktop") == 0) {
        if (!inventory_find_display(inv, source_id, &target_disp)) {
            strcpy_s(out_err_code, max_err_code, "source_unavailable");
            strcpy_s(out_err_msg, max_err_msg, "Target display not found in inventory");
            LeaveCriticalSection(&g_sup_cs);
            return false;
        }
        if (_stricmp(target_disp.policy, "denied") == 0) {
            strcpy_s(out_err_code, max_err_code, "source_not_allowed");
            strcpy_s(out_err_msg, max_err_msg, "Target display is denied by local policy");
            LeaveCriticalSection(&g_sup_cs);
            return false;
        }
        if (!desktop_is_interactive_available()) {
            strcpy_s(out_err_code, max_err_code, "session_unavailable");
            strcpy_s(out_err_msg, max_err_msg, "Desktop session is not available or locked");
            LeaveCriticalSection(&g_sup_cs);
            return false;
        }
    } else if (mode && _stricmp(mode, "usb-camera") == 0) {
        if (!inventory_find_camera(inv, source_id, &target_cam) || !target_cam.available) {
            strcpy_s(out_err_code, max_err_code, "source_unavailable");
            strcpy_s(out_err_msg, max_err_msg, "Target camera not found or unavailable");
            LeaveCriticalSection(&g_sup_cs);
            return false;
        }
    } else {
        strcpy_s(out_err_code, max_err_code, "source_not_allowed");
        strcpy_s(out_err_msg, max_err_msg, "Invalid mode");
        LeaveCriticalSection(&g_sup_cs);
        return false;
    }

    /* Check if already running with identical source_id AND stream_instance_id */
    if (strcmp(g_sup.state, "running") == 0 &&
        strcmp(g_sup.source_id, source_id) == 0 &&
        strcmp(g_sup.stream_instance_id, stream_instance_id) == 0) {
        strcpy_s(out_result, max_result, "already_running");
        LeaveCriticalSection(&g_sup_cs);
        return true;
    }

    bool is_switched = (strcmp(g_sup.state, "running") == 0);

    /* Controlled switch if another stream is active */
    if (is_switched) {
        log_info("Controlled switch: stopping existing stream %s before starting %s...",
                 g_sup.stream_instance_id, stream_instance_id);
        stop_active_process_internal();
    }

    /* Locate FFmpeg binary */
    wchar_t ffmpeg_bin[MAX_PATH];
    if (g_custom_ffmpeg_binary[0] != L'\0') {
        wcscpy_s(ffmpeg_bin, MAX_PATH, g_custom_ffmpeg_binary);
    } else {
        swprintf_s(ffmpeg_bin, MAX_PATH, L"%hs\\ffmpeg\\ffmpeg.exe", g_base_path);
    }

    if (GetFileAttributesW(ffmpeg_bin) == INVALID_FILE_ATTRIBUTES) {
        strcpy_s(out_err_code, max_err_code, "ffmpeg_missing");
        snprintf(out_err_msg, max_err_msg, "FFmpeg binary missing at %ls", ffmpeg_bin);
        LeaveCriticalSection(&g_sup_cs);
        return false;
    }

    /* Build command line */
    wchar_t cmdline[4096];
    bool cmd_ok = false;
    if (_stricmp(mode, "desktop") == 0) {
        cmd_ok = ffmpeg_build_desktop_cmdline(ffmpeg_bin, stream_instance_id, profile,
                                              target_disp.x, target_disp.y,
                                              target_disp.width, target_disp.height,
                                              cmdline, sizeof(cmdline) / sizeof(wchar_t));
    } else {
        cmd_ok = ffmpeg_build_camera_cmdline(ffmpeg_bin, stream_instance_id, profile,
                                             target_cam.device_path, target_cam.name,
                                             cmdline, sizeof(cmdline) / sizeof(wchar_t));
    }

    if (!cmd_ok) {
        strcpy_s(out_err_code, max_err_code, "ffmpeg_integrity");
        strcpy_s(out_err_msg, max_err_msg, "Failed to construct FFmpeg command line");
        LeaveCriticalSection(&g_sup_cs);
        return false;
    }

    strcpy_s(g_sup.stream_instance_id, sizeof(g_sup.stream_instance_id), stream_instance_id);
    if (lease_id) strcpy_s(g_sup.lease_id, sizeof(g_sup.lease_id), lease_id);
    strcpy_s(g_sup.mode, sizeof(g_sup.mode), mode);
    strcpy_s(g_sup.source_id, sizeof(g_sup.source_id), source_id);
    if (profile) strcpy_s(g_sup.profile, sizeof(g_sup.profile), profile);
    g_sup.session_id = target_disp.session_id;
    g_sup.desktop_rect.left = target_disp.x;
    g_sup.desktop_rect.top = target_disp.y;
    g_sup.desktop_rect.right = target_disp.x + target_disp.width;
    g_sup.desktop_rect.bottom = target_disp.y + target_disp.height;

    bool launched = launch_ffmpeg_process(cmdline, stream_instance_id,
                                          out_err_code, max_err_code,
                                          out_err_msg, max_err_msg);
    if (launched) {
        strcpy_s(out_result, max_result, is_switched ? "switched" : "started");
    } else {
        strcpy_s(g_sup.state, sizeof(g_sup.state), "failed");
        strcpy_s(g_sup.reason, sizeof(g_sup.reason), out_err_code);
        save_state_file();
    }

    LeaveCriticalSection(&g_sup_cs);
    return launched;
}

bool ffmpeg_supervisor_stop(const char* stream_instance_id,
                            const char* lease_id,
                            char* out_result, size_t max_result,
                            char* out_err_code, size_t max_err_code,
                            char* out_err_msg, size_t max_err_msg) {
    ffmpeg_supervisor_ensure_inited();
    EnterCriticalSection(&g_sup_cs);

    if (strcmp(g_sup.state, "stopped") == 0) {
        strcpy_s(out_result, max_result, "already_stopped");
        LeaveCriticalSection(&g_sup_cs);
        return true;
    }

    if (g_sup.lease_id[0] != '\0' && lease_id && lease_id[0] != '\0' &&
        strcmp(g_sup.lease_id, lease_id) != 0) {
        strcpy_s(out_err_code, max_err_code, "lease_mismatch");
        strcpy_s(out_err_msg, max_err_msg, "Lease ID does not match active stream");
        LeaveCriticalSection(&g_sup_cs);
        return false;
    }

    if (stream_instance_id && stream_instance_id[0] != '\0' &&
        strcmp(g_sup.stream_instance_id, stream_instance_id) != 0) {
        strcpy_s(out_err_code, max_err_code, "stream_mismatch");
        strcpy_s(out_err_msg, max_err_msg, "Stream instance ID does not match active stream");
        LeaveCriticalSection(&g_sup_cs);
        return false;
    }

    stop_active_process_internal();
    strcpy_s(out_result, max_result, "stopped");

    LeaveCriticalSection(&g_sup_cs);
    return true;
}

bool ffmpeg_supervisor_tick(const SystemInventory* inv,
                            bool* p_state_changed,
                            char* out_new_state, size_t max_state_len,
                            char* out_reason, size_t max_reason_len) {
    ffmpeg_supervisor_ensure_inited();
    EnterCriticalSection(&g_sup_cs);
    if (p_state_changed) *p_state_changed = false;

    if (strcmp(g_sup.state, "running") != 0) {
        LeaveCriticalSection(&g_sup_cs);
        return true;
    }

    DWORD exit_code = 0;
    if (GetExitCodeProcess(g_sup.hProcess, &exit_code) && exit_code != STILL_ACTIVE) {
        log_warn("FFmpeg process PID=%u exited unexpectedly with code %lu",
                 g_sup.ffmpeg_pid, exit_code);

        time_t now = time(NULL);
        if (g_sup.restart_window_start == 0 || (now - g_sup.restart_window_start > 600)) {
            g_sup.restart_window_start = now;
            g_sup.restart_count = 0;
        }

        g_sup.restart_count++;
        if (g_sup.restart_count > 5) {
            strcpy_s(g_sup.state, sizeof(g_sup.state), "failed");
            strcpy_s(g_sup.reason, sizeof(g_sup.reason), "restart_limit");
            stop_active_process_internal();

            if (p_state_changed) *p_state_changed = true;
            if (out_new_state) strcpy_s(out_new_state, max_state_len, g_sup.state);
            if (out_reason) strcpy_s(out_reason, max_reason_len, g_sup.reason);
            LeaveCriticalSection(&g_sup_cs);
            return true;
        }

        strcpy_s(g_sup.state, sizeof(g_sup.state), "restarting");
        save_state_file();
        if (p_state_changed) *p_state_changed = true;
        if (out_new_state) strcpy_s(out_new_state, max_state_len, g_sup.state);
        if (out_reason) strcpy_s(out_reason, max_reason_len, "unexpected_exit");

        LeaveCriticalSection(&g_sup_cs);
        return true;
    }

    /* Check interactive session if desktop */
    if (_stricmp(g_sup.mode, "desktop") == 0 && !desktop_is_interactive_available()) {
        log_warn("Interactive session became unavailable for desktop stream.");
        strcpy_s(g_sup.state, sizeof(g_sup.state), "session_unavailable");
        strcpy_s(g_sup.reason, sizeof(g_sup.reason), "session_unavailable");
        stop_active_process_internal();

        if (p_state_changed) *p_state_changed = true;
        if (out_new_state) strcpy_s(out_new_state, max_state_len, g_sup.state);
        if (out_reason) strcpy_s(out_reason, max_reason_len, g_sup.reason);
        LeaveCriticalSection(&g_sup_cs);
        return true;
    }

    /* Check source device in inventory */
    if (inv) {
        if (_stricmp(g_sup.mode, "desktop") == 0) {
            DisplayInfo d;
            if (!inventory_find_display(inv, g_sup.source_id, &d)) {
                log_warn("Target display %s disappeared from inventory.", g_sup.source_id);
                strcpy_s(g_sup.state, sizeof(g_sup.state), "source_unavailable");
                strcpy_s(g_sup.reason, sizeof(g_sup.reason), "source_unavailable");
                stop_active_process_internal();

                if (p_state_changed) *p_state_changed = true;
                if (out_new_state) strcpy_s(out_new_state, max_state_len, g_sup.state);
                if (out_reason) strcpy_s(out_reason, max_reason_len, g_sup.reason);
                LeaveCriticalSection(&g_sup_cs);
                return true;
            }
        } else if (_stricmp(g_sup.mode, "usb-camera") == 0) {
            CameraInfo c;
            if (!inventory_find_camera(inv, g_sup.source_id, &c) || !c.available) {
                log_warn("Target camera %s disappeared or became unavailable.", g_sup.source_id);
                strcpy_s(g_sup.state, sizeof(g_sup.state), "source_unavailable");
                strcpy_s(g_sup.reason, sizeof(g_sup.reason), "source_unavailable");
                stop_active_process_internal();

                if (p_state_changed) *p_state_changed = true;
                if (out_new_state) strcpy_s(out_new_state, max_state_len, g_sup.state);
                if (out_reason) strcpy_s(out_reason, max_reason_len, g_sup.reason);
                LeaveCriticalSection(&g_sup_cs);
                return true;
            }
        }
    }

    /* Check stall (> 10s without progress) */
    time_t now = time(NULL);
    if (g_sup.last_progress_time > 0 && (now - g_sup.last_progress_time > 10)) {
        log_warn("Stream stall detected (> 10s without progress). Triggering restart.");
        strcpy_s(g_sup.state, sizeof(g_sup.state), "restarting");
        strcpy_s(g_sup.reason, sizeof(g_sup.reason), "stall");
        stop_active_process_internal();

        if (p_state_changed) *p_state_changed = true;
        if (out_new_state) strcpy_s(out_new_state, max_state_len, g_sup.state);
        if (out_reason) strcpy_s(out_reason, max_reason_len, g_sup.reason);
        LeaveCriticalSection(&g_sup_cs);
        return true;
    }

    LeaveCriticalSection(&g_sup_cs);
    return true;
}

void ffmpeg_supervisor_get_info(StreamStateInfo* out_info) {
    if (!out_info) return;
    ffmpeg_supervisor_ensure_inited();
    EnterCriticalSection(&g_sup_cs);
    strcpy_s(out_info->stream_instance_id, sizeof(out_info->stream_instance_id), g_sup.stream_instance_id);
    strcpy_s(out_info->lease_id, sizeof(out_info->lease_id), g_sup.lease_id);
    strcpy_s(out_info->mode, sizeof(out_info->mode), g_sup.mode);
    strcpy_s(out_info->source_id, sizeof(out_info->source_id), g_sup.source_id);
    strcpy_s(out_info->profile, sizeof(out_info->profile), g_sup.profile);
    out_info->desktop_rect = g_sup.desktop_rect;
    out_info->session_id = g_sup.session_id;
    strcpy_s(out_info->state, sizeof(out_info->state), g_sup.state);
    strcpy_s(out_info->reason, sizeof(out_info->reason), g_sup.reason);
    out_info->ffmpeg_pid = g_sup.ffmpeg_pid;
    out_info->ffmpeg_start_time = g_sup.ffmpeg_start_time;
    out_info->started_at = g_sup.started_at;
    out_info->restart_count = g_sup.restart_count;
    LeaveCriticalSection(&g_sup_cs);
}
