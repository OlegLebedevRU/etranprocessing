/* live_start.c — L4C-09 live harness: spawn l4capture, CMD_START, log IPC, renew lease.
 * Usage: live_start.exe [low|default] [path\\to\\l4capture.exe]
 * RTP/RTCP: 127.0.0.1:5004/5005. Ctrl+C or lease end to stop. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "l4capture/ipc_protocol.h"
#include "l4capture/video_profile.h"
#include "l4capture/clock.h"

static void write_all(HANDLE h, const uint8_t *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        DWORD wrote = 0;
        if (!WriteFile(h, buf + off, (DWORD)(n - off), &wrote, NULL) || wrote == 0) break;
        off += wrote;
    }
}

static int send_msg(HANDLE h, uint16_t type, uint64_t seq, const l4c_message_t *msg) {
    uint8_t buf[L4C_IPC_MAX_FRAME];
    size_t written = 0;
    l4c_message_t m = *msg;
    m.type = type;
    m.request_seq = seq;
    if (l4c_ipc_encode(&m, buf, sizeof(buf), &written) != L4C_OK) return 0;
    write_all(h, buf, written);
    return 1;
}

static void print_event(const l4c_message_t *m) {
    switch (m->type) {
    case L4C_EVENT_READY:
        printf("[READY] %ux%u @ %ufps capture=%u encoder=%u seq=%llu\n",
               m->body.ready.actual_width, m->body.ready.actual_height,
               m->body.ready.actual_fps, m->body.ready.capture_backend,
               m->body.ready.encoder_backend, (unsigned long long)m->request_seq);
        break;
    case L4C_EVENT_METRICS:
        printf("[METRICS] fps=%u kbps=%u raw_drop=%u enc_drop=%u tr_drop=%u p95=%u q=%u priv_kb=%u gdi=%u\n",
               m->body.metrics.fps, m->body.metrics.bitrate_kbps,
               m->body.metrics.raw_drops, m->body.metrics.encoder_drops,
               m->body.metrics.transport_drops, m->body.metrics.encode_p95_ms,
               m->body.metrics.queue_depth,
               m->body.metrics.private_bytes_kb, m->body.metrics.gdi_handles);
        break;
    case L4C_EVENT_DEGRADED:
        printf("[DEGRADED] state=%u reason=%u seq=%llu\n",
               m->body.degraded.degrade_state, m->body.degraded.reason,
               (unsigned long long)m->request_seq);
        break;
    case L4C_EVENT_ERROR:
        printf("[ERROR] code=%u seq=%llu\n", m->body.error.error_code,
               (unsigned long long)m->request_seq);
        break;
    default:
        printf("[MSG] type=0x%04x seq=%llu\n", m->type, (unsigned long long)m->request_seq);
        break;
    }
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    const char *profile_arg = (argc > 1) ? argv[1] : "default";
    const char *exe = (argc > 2) ? argv[2] : "D:\\repo\\platerra\\Public\\etranprocessing\\tools\\l4capture\\bin\\x64\\l4capture.exe";
    uint16_t profile_id = L4C_PROFILE_REQ_DEFAULT;
    SECURITY_ATTRIBUTES sa;
    HANDLE hInR, hInW, hOutR, hOutW, hJob;
    HANDLE inherit[2];
    SIZE_T attr_size = 0;
    LPPROC_THREAD_ATTRIBUTE_LIST attr_list = NULL;
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
    wchar_t cmd[MAX_PATH * 2];
    uint8_t rbuf[8192];
    l4c_ipc_parser_t parser;
    uint64_t seq = 1, deadline, last_renew, last_idr_req;
    DWORD session_id = 0;
    int exit_code = 0;

    if (_stricmp(profile_arg, "low") == 0) profile_id = L4C_PROFILE_REQ_LOW;
    else profile_id = L4C_PROFILE_REQ_DEFAULT; /* wire 3 = default/720p */

    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id) || session_id == 0) {
        fprintf(stderr, "live_start: Session 0 rejected\n");
        return 2;
    }

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&hInR, &hInW, &sa, 0) || !CreatePipe(&hOutR, &hOutW, &sa, 0)) {
        fprintf(stderr, "live_start: CreatePipe failed\n");
        return 3;
    }
    SetHandleInformation(hInW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);

    hJob = CreateJobObjectW(NULL, NULL);
    memset(&jeli, 0, sizeof(jeli));
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));

    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    attr_list = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attr_size);
    InitializeProcThreadAttributeList(attr_list, 1, 0, &attr_size);
    inherit[0] = hInR;
    inherit[1] = hOutW;
    UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                              inherit, sizeof(inherit), NULL, NULL);

    memset(&si, 0, sizeof(si));
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = SW_HIDE;
    si.lpAttributeList = attr_list;

    _snwprintf_s(cmd, _countof(cmd), _TRUNCATE,
                 L"\"%S\" --pipe-in=%lu --pipe-out=%lu",
                 exe, (unsigned long)(ULONG_PTR)hInR, (unsigned long)(ULONG_PTR)hOutW);

    printf("live_start: profile=%s exe=%s\n", profile_arg, exe);
    printf("live_start: RTP 127.0.0.1:5004 / RTCP 5005 — start your transmission now\n");
    fflush(stdout);

    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE,
                        CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                        NULL, NULL, &si.StartupInfo, &pi)) {
        fprintf(stderr, "live_start: CreateProcess failed (%lu)\n", GetLastError());
        return 4;
    }
    AssignProcessToJobObject(hJob, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(hInR);
    CloseHandle(hOutW);

    deadline = l4c_now_monotonic_ms() + 3600000ull;
    last_renew = l4c_now_monotonic_ms();
    last_idr_req = 0;

    {
        l4c_message_t start;
        memset(&start, 0, sizeof(start));
        memset(start.body.start.lease_id, 0x11, 16);
        memset(start.body.start.stream_id, 0x22, 16);
        start.body.start.source_rect.left = 0;
        start.body.start.source_rect.top = 0;
        /* safety_gate: нулевой RECT отклоняется (INVALID_ARG); full-screen для live smoke. */
        start.body.start.source_rect.right = GetSystemMetrics(SM_CXSCREEN);
        start.body.start.source_rect.bottom = GetSystemMetrics(SM_CYSCREEN);
        start.body.start.geometry_gen = 1;
        start.body.start.profile_id = profile_id;
        start.body.start.rtp_port = 5004;
        start.body.start.rtcp_port = 5005;
        start.body.start.deadline_tick_ms = deadline;
        if (!send_msg(hInW, L4C_CMD_START, seq++, &start)) {
            fprintf(stderr, "live_start: CMD_START send failed\n");
            return 5;
        }
        printf("[CMD] START profile_id=%u deadline=%llu\n",
               profile_id, (unsigned long long)deadline);
        fflush(stdout);
    }

    l4c_ipc_parser_init(&parser);

    for (;;) {
        DWORD avail = 0, readn = 0;
        uint64_t now;
        DWORD w;

        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            printf("live_start: l4capture exited code=%lu\n", code);
            exit_code = (int)code;
            break;
        }
        if (!PeekNamedPipe(hOutR, NULL, 0, NULL, &avail, NULL)) break;
        if (avail > 0) {
            if (avail > sizeof(rbuf)) avail = sizeof(rbuf);
            if (!ReadFile(hOutR, rbuf, avail, &readn, NULL) || readn == 0) break;
            {
                size_t consumed = 0;
                while (consumed < readn) {
                    l4c_message_t msg;
                    bool ready = false;
                    size_t used = 0;
                    l4c_status_t st = l4c_ipc_feed(&parser, rbuf + consumed, readn - consumed, &used, &msg, &ready);
                    if (st != L4C_OK) {
                        printf("live_start: parser error\n");
                        break;
                    }
                    consumed += used;
                    if (ready) print_event(&msg);
                }
            }
        }

        now = l4c_now_monotonic_ms();
        if (now - last_renew >= 5000) {
            l4c_message_t renew;
            memset(&renew, 0, sizeof(renew));
            memset(renew.body.renew.lease_id, 0x11, 16);
            renew.body.renew.new_deadline_tick_ms = now + 3600000ull;
            send_msg(hInW, L4C_CMD_RENEW_LEASE, seq++, &renew);
            last_renew = now;
        }
        (void)last_idr_req;

        w = WaitForSingleObject(pi.hProcess, 5);
        if (w == WAIT_OBJECT_0) continue;
    }

    {
        l4c_message_t stop;
        memset(&stop, 0, sizeof(stop));
        memset(stop.body.stream_id, 0x22, 16);
        send_msg(hInW, L4C_CMD_STOP, seq++, &stop);
        if (WaitForSingleObject(pi.hProcess, 2000) == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 1);
        }
    }
    CloseHandle(hInW);
    CloseHandle(hOutR);
    CloseHandle(pi.hProcess);
    CloseHandle(hJob);
    DeleteProcThreadAttributeList(attr_list);
    HeapFree(GetProcessHeap(), 0, attr_list);
    printf("live_start: done\n");
    return exit_code;
}
