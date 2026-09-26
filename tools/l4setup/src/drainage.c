#include "drainage.h"
#include "services.h"
#include "log.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

static bool path_starts_with_dir(const wchar_t* path, const wchar_t* dir) {
    if (!path || !dir) return false;
    size_t dir_len = wcslen(dir);
    if (dir_len == 0) return false;

    if (_wcsnicmp(path, dir, dir_len) != 0) return false;

    // Next char must be backslash, forward slash or end of string
    wchar_t c = path[dir_len];
    return (c == L'\\' || c == L'/' || c == L'\0');
}

static bool check_active_video_stream(const wchar_t* dest_dir) {
    // 1. Check if ffmpeg_state.json exists and indicates streaming
    wchar_t state_path[MAX_PATH];
    swprintf_s(state_path, MAX_PATH, L"%s\\ffmpeg\\ffmpeg_state.json", dest_dir);
    DWORD attr = GetFileAttributesW(state_path);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        FILE* fp = NULL;
        if (_wfopen_s(&fp, state_path, L"r") == 0 && fp) {
            char buf[512] = { 0 };
            fread(buf, 1, sizeof(buf) - 1, fp);
            fclose(fp);
            if (strstr(buf, "\"streaming\": true") || strstr(buf, "\"active\": true") ||
                strstr(buf, "\"status\": \"streaming\"") || strstr(buf, "\"status\": \"active\"")) {
                return true;
            }
        }
    }

    // 2. Check if ffmpeg.exe from dest_dir is running
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"ffmpeg.exe") == 0 ||
                    _wcsicmp(pe.szExeFile, L"l4capture.exe") == 0) {
                    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        wchar_t img_path[MAX_PATH] = { 0 };
                        DWORD size = MAX_PATH;
                        if (QueryFullProcessImageNameW(hProc, 0, img_path, &size)) {
                            if (path_starts_with_dir(img_path, dest_dir)) {
                                CloseHandle(hProc);
                                CloseHandle(hSnap);
                                return true;
                            }
                        }
                        CloseHandle(hProc);
                    }
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    return false;
}

static void kill_orphaned_processes(const wchar_t* dest_dir, DrainageResult* out_result) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"ffmpeg.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"l4capture.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"l4desk.exe") == 0) {
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    wchar_t img_path[MAX_PATH] = { 0 };
                    DWORD size = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProc, 0, img_path, &size)) {
                        if (path_starts_with_dir(img_path, dest_dir)) {
                            log_info("Terminating orphaned process %ls (PID: %lu) in %ls", pe.szExeFile, pe.th32ProcessID, dest_dir);
                            TerminateProcess(hProc, 1);
                            if (out_result && out_result->processes_killed_count < DRAINAGE_MAX_ITEMS) {
                                char a_info[128];
                                snprintf(a_info, sizeof(a_info), "%S:%lu", pe.szExeFile, pe.th32ProcessID);
                                strncpy_s(out_result->processes_killed[out_result->processes_killed_count++],
                                          sizeof(out_result->processes_killed[0]), a_info, _TRUNCATE);
                            }
                        } else {
                            log_info("Preserving non-l4tools process %ls (PID: %lu) at %ls", pe.szExeFile, pe.th32ProcessID, img_path);
                        }
                    }
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
}

static bool free_loopback_ports(const wchar_t* dest_dir, DrainageResult* out_result) {
    const int target_ports[] = { 1883, 18443, 18883 };
    int num_ports = (int)(sizeof(target_ports)/sizeof(target_ports[0]));

    DWORD table_size = 0;
    GetExtendedTcpTable(NULL, &table_size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (table_size == 0) return true;

    PMIB_TCPTABLE_OWNER_PID pTable = (PMIB_TCPTABLE_OWNER_PID)malloc(table_size);
    if (!pTable) return false;

    if (GetExtendedTcpTable(pTable, &table_size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) {
        free(pTable);
        return false;
    }

    DWORD my_pid = GetCurrentProcessId();
    bool all_freed = true;

    for (DWORD i = 0; i < pTable->dwNumEntries; i++) {
        int port = (int)ntohs((u_short)pTable->table[i].dwLocalPort);
        bool is_target = false;
        for (int p = 0; p < num_ports; p++) {
            if (target_ports[p] == port) {
                is_target = true;
                break;
            }
        }
        if (!is_target) continue;

        DWORD pid = pTable->table[i].dwOwningPid;
        if (pid == 0 || pid == my_pid) continue;

        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
        if (!hProc) {
            log_err("Cannot open process holding port %d (PID %lu). Access denied.", port, pid);
            all_freed = false;
            continue;
        }

        wchar_t img_path[MAX_PATH] = { 0 };
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProc, 0, img_path, &size)) {
            if (path_starts_with_dir(img_path, dest_dir)) {
                log_info("Port %d is held by %ls (PID %lu) from dest_dir; terminating to free port...", port, img_path, pid);
                TerminateProcess(hProc, 1);
                if (out_result && out_result->ports_freed_count < DRAINAGE_MAX_ITEMS) {
                    bool already_recorded = false;
                    for (int k = 0; k < out_result->ports_freed_count; k++) {
                        if (out_result->ports_freed[k] == port) {
                            already_recorded = true;
                            break;
                        }
                    }
                    if (!already_recorded) {
                        out_result->ports_freed[out_result->ports_freed_count++] = port;
                    }
                }
            } else {
                log_err("Port %d is held by FOREIGN process %ls (PID %lu). Cannot terminate foreign process!", port, img_path, pid);
                all_freed = false;
            }
        } else {
            log_err("Cannot query image name for process holding port %d (PID %lu)", port, pid);
            all_freed = false;
        }
        CloseHandle(hProc);
    }

    free(pTable);
    return all_freed;
}

static void drainage_service_cb(
    const wchar_t* svc_name,
    ServiceLifecycleStatus status,
    DWORD elapsed_sec,
    const char* notice,
    void* user_data
) {
    UNREFERENCED_PARAMETER(elapsed_sec);
    UNREFERENCED_PARAMETER(notice);
    DrainageResult* out_result = (DrainageResult*)user_data;
    if (out_result && status == SVC_STATUS_STOPPED && out_result->services_stopped_count < DRAINAGE_MAX_ITEMS) {
        char a_name[64] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, svc_name, -1, a_name, sizeof(a_name), NULL, NULL);
        bool already = false;
        for (int i = 0; i < out_result->services_stopped_count; i++) {
            if (strcmp(out_result->services_stopped[i], a_name) == 0) {
                already = true;
                break;
            }
        }
        if (!already) {
            strncpy_s(out_result->services_stopped[out_result->services_stopped_count++],
                      sizeof(out_result->services_stopped[0]), a_name, _TRUNCATE);
        }
    }
}

bool drainage_execute(const wchar_t* dest_dir, const char* sn, bool silent, DrainageResult* out_result) {
    if (!dest_dir) return false;
    if (out_result) memset(out_result, 0, sizeof(DrainageResult));

    log_info("Starting drainage phase on %ls...", dest_dir);

    // 1. Check for an active video stream before interrupting services.
    if (check_active_video_stream(dest_dir)) {
        if (out_result) out_result->active_stream_detected = true;

        if (silent) {
            log_err("Active video stream detected in %ls during silent mode. Drainage aborted.", dest_dir);
            return false;
        }

        int resp = MessageBoxW(
            NULL,
            L"An active video stream was detected.\nStopping services will interrupt the active stream.\nDo you want to proceed?",
            L"Leo4 Setup - Active Stream Detected",
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2
        );
        if (resp != IDYES) {
            log_warn("User refused to interrupt active video stream. Drainage aborted.");
            return false;
        }
        log_info("User confirmed interruption of active video stream.");
    }

    // 2. Stop services in reverse dependency order: L4Superv -> L4Con -> mosquitto -> Leo4Proxy
    // services_stop_all_in_order signals Global\L4Desk_Stop_<SN> when stopping L4Superv
    if (!services_stop_all_in_order(sn, drainage_service_cb, out_result)) {
        log_warn("One or more services did not stop cleanly within 120s timeout.");
    }

    // 3. Clean up any orphaned child processes that belong to dest_dir (only after graceful stop wait)
    kill_orphaned_processes(dest_dir, out_result);

    // 4. Free loopback ports 1883, 18443, 18883
    if (!free_loopback_ports(dest_dir, out_result)) {
        log_err("Failed to free all required ports (occupied by foreign processes). Drainage failed.");
        return false;
    }

    // Give OS a moment to reclaim sockets
    Sleep(500);

    log_info("Drainage phase completed successfully.");
    return true;
}
