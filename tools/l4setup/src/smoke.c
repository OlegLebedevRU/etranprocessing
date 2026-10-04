#include "smoke.h"
#include "proxy_probe.h"
#include "services.h"
#include "log.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <wtsapi32.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "wtsapi32.lib")

static bool check_proxy_info(void) {
    return setup_proxy_probe(18443, false, 1200);
}

static void append_probe_output(char* output,size_t capacity,size_t* used,const char* chunk,size_t count) {
    if (count>=capacity) { chunk+=count-(capacity-1); count=capacity-1; *used=0; }
    if (*used+count>=capacity) {
        size_t discard=*used+count-(capacity-1);
        memmove(output,output+discard,*used-discard); *used-=discard;
    }
    memcpy(output+*used,chunk,count); *used+=count; output[*used]=0;
}

void smoke_probe_upstream(const wchar_t* dest_dir, SmokeProbesResult* result) {
    wchar_t executable[MAX_PATH],args[2048]=L"",command[2400];
    swprintf_s(executable,MAX_PATH,L"%ls\\leo4proxy\\leo4proxy.exe",dest_dir);
    /* Failure to launch diagnostics must never produce a healthy result. */
    bool local_warnings=result->has_warnings;
    result->has_warnings=true;
    strcpy_s(result->network,sizeof(result->network),"unknown");
    if (!result->critical_failed) result->calculated_exit_code=12;
    for (int c=0;c<4;c++) strcpy_s(result->upstream_tls[c],32,"probe_failed");
    if (GetFileAttributesW(executable)==INVALID_FILE_ATTRIBUTES) return;
    /* A stale watchdog file must not validate different endpoints or disabled RTP. */
    if(!services_read_proxy_arguments(dest_dir,args,2048))return;
    if (swprintf_s(command,2400,L"\"%ls\" --check-upstream %ls",executable,args)<0) return;
    SECURITY_ATTRIBUTES attributes={sizeof(attributes),NULL,TRUE}; HANDLE reader=NULL,writer=NULL;
    if (!CreatePipe(&reader,&writer,&attributes,0)) return;
    if(!SetHandleInformation(reader,HANDLE_FLAG_INHERIT,0)) {CloseHandle(reader);CloseHandle(writer);return;}
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_EXISTING,0,NULL);
    HANDLE error_output=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_EXISTING,0,NULL);
    if (input==INVALID_HANDLE_VALUE || error_output==INVALID_HANDLE_VALUE) {
        if (input!=INVALID_HANDLE_VALUE) CloseHandle(input);
        if (error_output!=INVALID_HANDLE_VALUE) CloseHandle(error_output);
        CloseHandle(reader); CloseHandle(writer); return;
    }
    STARTUPINFOW startup={sizeof(startup)}; PROCESS_INFORMATION process={0};
    startup.dwFlags=STARTF_USESTDHANDLES; startup.hStdOutput=writer; startup.hStdError=error_output; startup.hStdInput=input;
    bool started=CreateProcessW(executable,command,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,dest_dir,&startup,&process)!=0;
    CloseHandle(writer); CloseHandle(input); CloseHandle(error_output);
    if (!started) { CloseHandle(reader); return; }
    char output[8192]={0}; size_t used=0; ULONGLONG deadline=GetTickCount64()+9500; bool timed_out=false;
    for (;;) {
        if (GetTickCount64()>=deadline) { timed_out=true; TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,1000); break; }
        DWORD available=0;
        if (PeekNamedPipe(reader,NULL,0,NULL,&available,NULL) && available) {
            char chunk[1024]; DWORD got=0;
            if (ReadFile(reader,chunk,available<sizeof(chunk)?available:sizeof(chunk),&got,NULL)) {
                append_probe_output(output,sizeof(output),&used,chunk,got);
            }
            continue;
        }
        if (WaitForSingleObject(process.hProcess,25)==WAIT_OBJECT_0) {
            /* Exit may race the previous PeekNamedPipe: the child can flush its
               final verdict just before signaling. Drain those bytes first. */
            while (PeekNamedPipe(reader,NULL,0,NULL,&available,NULL) && available) {
                char chunk[1024]; DWORD got=0;
                if (!ReadFile(reader,chunk,available<sizeof(chunk)?available:sizeof(chunk),&got,NULL) || !got) break;
                append_probe_output(output,sizeof(output),&used,chunk,got);
            }
            break;
        }
        if (GetTickCount64()>=deadline) { timed_out=true; TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,1000); break; }
    }
    CloseHandle(reader); CloseHandle(process.hThread); CloseHandle(process.hProcess);
    for (int c=0;c<4;c++) strcpy_s(result->upstream_tls[c],32,timed_out?"timeout":"probe_failed");
    char* next=NULL;
    for (char* line=strtok_s(output,"\r\n",&next);line;line=strtok_s(NULL,"\r\n",&next)) {
        int channel=-1; char verdict[32]={0};
        if (sscanf_s(line,"{\"v\":1,\"channel\":%d,\"verdict\":\"%31[a-z_]",&channel,verdict,(unsigned int)sizeof(verdict))==2 && channel>=0 && channel<4) {
            const char* valid[]={"valid","skipped","policy_blocked","cert_invalid","probe_failed","timeout"};
            for (int n=0;n<6;n++) if (!strcmp(verdict,valid[n])) strcpy_s(result->upstream_tls[channel],32,verdict);
        }
    }
    if (strstr(output,"\"error\":\"no_certificate\""))
        for (int c=0;c<4;c++) strcpy_s(result->upstream_tls[c],32,"no_certificate");
    bool upstream_failed=false, connected=false;
    const char* names[]={"MQTT","HTTPS","Stream","RTP"};
    for (int c=0;c<4;c++) {
        log_info("Upstream %s TLS: %s",names[c],result->upstream_tls[c]);
        if (!strcmp(result->upstream_tls[c],"valid")) connected=true;
        if (!strcmp(result->upstream_tls[c],"cert_invalid") || !strcmp(result->upstream_tls[c],"probe_failed") || !strcmp(result->upstream_tls[c],"timeout") || !strcmp(result->upstream_tls[c],"no_certificate")) upstream_failed=true;
    }
    strcpy_s(result->network,sizeof(result->network),connected?"reachable":"unknown");
    result->has_warnings=upstream_failed || local_warnings;
    if (!result->critical_failed) result->calculated_exit_code=result->has_warnings?12:0;
}

static bool check_mosquitto_port(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    u_long non_blocking = 1;
    ioctlsocket(s, FIONBIO, &non_blocking);

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(1883);
    InetPtonA(AF_INET,"127.0.0.1",&sin.sin_addr);

    connect(s, (struct sockaddr*)&sin, sizeof(sin));

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(s, &writefds);

    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;

    bool connected = false;
    if (select(0, NULL, &writefds, NULL, &tv) > 0) {
        int so_error = 0;
        int len = sizeof(so_error);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&so_error, &len) == 0 && so_error == 0) {
            connected = true;
        }
    }

    closesocket(s);
    WSACleanup();
    return connected;
}

static bool check_l4desk_in_session(DWORD target_session_id) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    bool running = false;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"l4desk.exe") == 0) {
                DWORD proc_sess = 0;
                if (ProcessIdToSessionId(pe.th32ProcessID, &proc_sess)) {
                    if (proc_sess == target_session_id) {
                        running = true;
                        break;
                    }
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return running;
}

/* SCM RUNNING precedes supervisor's asynchronous identity/desktop startup. */
static bool wait_l4desk_in_session(DWORD session_id) {
    if (session_id == 0 || session_id == MAXDWORD) return false;
    ULONGLONG deadline = GetTickCount64() + 15000;
    for (;;) {
        if (WTSGetActiveConsoleSessionId() != session_id) return false;
        if (check_l4desk_in_session(session_id)) return true;
        if (GetTickCount64() >= deadline) return false;
        ULONGLONG now=GetTickCount64();if(now<deadline)Sleep((DWORD)(deadline-now<250?deadline-now:250));
    }
}

static void check_ffmpeg_capture(const wchar_t* dest_dir, int session_id, char* out_status, size_t out_size) {
    if (!out_status || out_size == 0) return;

    if (session_id == 0) {
        strncpy_s(out_status, out_size, "skipped", _TRUNCATE);
        return;
    }

    wchar_t ffmpeg_exe[MAX_PATH];
    swprintf_s(ffmpeg_exe, MAX_PATH, L"%ls\\ffmpeg\\ffmpeg.exe", dest_dir);
    if (GetFileAttributesW(ffmpeg_exe) == INVALID_FILE_ATTRIBUTES) {
        strncpy_s(out_status, out_size, "skipped", _TRUNCATE);
        return;
    }

    wchar_t cmd[1024];
    swprintf_s(cmd, 1024, L"\"%ls\" -f gdigrab -i desktop -frames:v 1 -f null -", ffmpeg_exe);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        strncpy_s(out_status, out_size, "skipped", _TRUNCATE);
        return;
    }

    DWORD wait_res = WaitForSingleObject(pi.hProcess, 10000);
    if (wait_res == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess,1000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        strncpy_s(out_status, out_size, "skipped", _TRUNCATE);
        return;
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exit_code == 0) {
        strncpy_s(out_status, out_size, "ok", _TRUNCATE);
    } else {
        strncpy_s(out_status, out_size, "skipped", _TRUNCATE);
    }
}

static bool check_desktop_locked(void) {
    HDESK hDesk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (hDesk != NULL) {
        CloseDesktop(hDesk);
        return false;
    }
    return true;
}

bool smoke_run_probes(
    const wchar_t* dest_dir,
    bool is_active_status,
    SmokeProbesResult* out_result
) {
    if (!out_result) return false;
    memset(out_result, 0, sizeof(SmokeProbesResult));

    log_info("Executing local smoke probes...");

    // 1. Probe proxy_info
    bool proxy_ok = check_proxy_info();
    strcpy_s(out_result->proxy_info, sizeof(out_result->proxy_info), proxy_ok ? "ok" : "fail");
    log_info("Smoke probe [proxy_info]: %s", out_result->proxy_info);

    // 2. Probe mosquitto_port
    bool mosq_ok = check_mosquitto_port();
    strcpy_s(out_result->mosquitto_port, sizeof(out_result->mosquitto_port), mosq_ok ? "ok" : "fail");
    log_info("Smoke probe [mosquitto_port]: %s", out_result->mosquitto_port);

    // 3. User session and l4desk
    DWORD session_id = WTSGetActiveConsoleSessionId();
    out_result->user_session_id = (int)session_id;

    if (is_active_status && session_id != 0 && session_id != 0xFFFFFFFF) {
        log_info("Waiting for l4desk in session %lu (up to 15s)...", session_id);
        out_result->l4desk_running = wait_l4desk_in_session(session_id);
    } else {
        out_result->l4desk_running = false;
    }
    log_info("Smoke probe [user_session_id]: %d, [l4desk_running]: %s",
             out_result->user_session_id, out_result->l4desk_running ? "true" : "false");

    // 4. FFmpeg test frame capture
    check_ffmpeg_capture(dest_dir, (int)session_id, out_result->ffmpeg_smoke_capture, sizeof(out_result->ffmpeg_smoke_capture));
    log_info("Smoke probe [ffmpeg_smoke_capture]: %s", out_result->ffmpeg_smoke_capture);

    // 5. Desktop locked check
    out_result->desktop_locked = check_desktop_locked();
    log_info("Smoke probe [desktop_locked]: %s", out_result->desktop_locked ? "true" : "false");

    // 6. Remote input determination
    if (out_result->user_session_id == 0 || out_result->user_session_id == (int)0xFFFFFFFF) {
        strcpy_s(out_result->remote_input, sizeof(out_result->remote_input), "session_unavailable");
    } else if (out_result->desktop_locked) {
        strcpy_s(out_result->remote_input, sizeof(out_result->remote_input), "desktop_locked");
    } else if (out_result->l4desk_running) {
        strcpy_s(out_result->remote_input, sizeof(out_result->remote_input), "available");
    } else {
        strcpy_s(out_result->remote_input, sizeof(out_result->remote_input), "disabled");
    }
    log_info("Smoke probe [remote_input]: %s", out_result->remote_input);

    // Network evidence comes from actual upstream TLS, including DNS-free IP recovery.
    strcpy_s(out_result->network, sizeof(out_result->network), "not_run");
    log_info("Smoke probe [network]: %s", out_result->network);

    // Evaluate
    if (!proxy_ok || !mosq_ok) {
        out_result->critical_failed = true;
        out_result->calculated_exit_code = 27; // Critical smoke probe failed
        log_err("Smoke critical probe failed (proxy_info: %s, mosquitto_port: %s).",
                out_result->proxy_info, out_result->mosquitto_port);
        return false;
    }

    if (is_active_status && session_id != 0 && session_id != MAXDWORD && !out_result->l4desk_running) {
        out_result->has_warnings = true;
        out_result->calculated_exit_code = 12; // Degraded / ready with warnings
        log_warn("Smoke completed with degraded status (code 12).");
    } else {
        out_result->calculated_exit_code = 0; // All smoke probes cleanly passed
        log_info("Smoke probes completed successfully (code 0).");
    }

    return true;
}
