#include "drainage.h"
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

static bool check_active_ffmpeg_stream(const wchar_t* dest_dir) {
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
                if (_wcsicmp(pe.szExeFile, L"ffmpeg.exe") == 0) {
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

static bool stop_service(const wchar_t* svc_name, DrainageResult* out_result) {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) return false;

    SC_HANDLE hSvc = OpenServiceW(hSCM, svc_name, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        return true; // Service not installed - nothing to stop
    }

    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
        if (ssp.dwCurrentState == SERVICE_STOPPED) {
            CloseServiceHandle(hSvc);
            CloseServiceHandle(hSCM);
            return true;
        }
    }

    log_info("Stopping service %ls (PID: %lu)...", svc_name, ssp.dwProcessId);
    SERVICE_STATUS ss;
    ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);

    // Wait up to 5 seconds
    bool stopped = false;
    for (int i = 0; i < 20; i++) {
        Sleep(250);
        if (QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
            if (ssp.dwCurrentState == SERVICE_STOPPED) {
                stopped = true;
                break;
            }
        }
    }

    if (!stopped && ssp.dwProcessId > 0) {
        log_warn("Service %ls did not stop within 5s; terminating PID %lu...", svc_name, ssp.dwProcessId);
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, ssp.dwProcessId);
        if (hProc) {
            TerminateProcess(hProc, 1);
            CloseHandle(hProc);
            stopped = true;
        }
    }

    if (stopped && out_result && out_result->services_stopped_count < DRAINAGE_MAX_ITEMS) {
        char a_name[64] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, svc_name, -1, a_name, sizeof(a_name), NULL, NULL);
        strncpy_s(out_result->services_stopped[out_result->services_stopped_count++],
                  sizeof(out_result->services_stopped[0]), a_name, _TRUNCATE);
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return stopped;
}

static void kill_orphaned_processes(const wchar_t* dest_dir, DrainageResult* out_result) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"ffmpeg.exe") == 0 || _wcsicmp(pe.szExeFile, L"l4desk.exe") == 0) {
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

bool drainage_execute(const wchar_t* dest_dir, bool silent, DrainageResult* out_result) {
    if (!dest_dir) return false;
    if (out_result) memset(out_result, 0, sizeof(DrainageResult));

    log_info("Starting drainage phase on %ls...", dest_dir);

    // 1. Check for active ffmpeg stream
    if (check_active_ffmpeg_stream(dest_dir)) {
        if (out_result) out_result->active_stream_detected = true;

        if (silent) {
            log_err("Active ffmpeg stream detected in %ls during silent mode. Drainage aborted.", dest_dir);
            return false;
        }

        int resp = MessageBoxW(
            NULL,
            L"Обнаружен активный видеострим ffmpeg.\nПрервать стрим и продолжить установку?",
            L"Leo4 Setup - Предупреждение",
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2
        );
        if (resp != IDYES) {
            log_warn("User refused to interrupt active ffmpeg stream. Drainage aborted.");
            return false;
        }
        log_info("User confirmed interruption of active ffmpeg stream.");
    }

    // 2. Stop services in reverse dependency order: L4Superv -> L4Con -> mosquitto -> Leo4Proxy
    const wchar_t* services[] = { L"L4Superv", L"L4Con", L"mosquitto", L"Leo4Proxy" };
    for (int i = 0; i < 4; i++) {
        stop_service(services[i], out_result);
    }

    // 3. Kill orphaned ffmpeg and l4desk
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
