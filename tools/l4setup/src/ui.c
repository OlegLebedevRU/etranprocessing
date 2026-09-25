#include "ui.h"
#include "engine.h"
#include "services.h"
#include "unpack.h"
#include "version.h"
#include "log.h"
#include "../res/resource.h"
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "comctl32.lib")

#define WM_SETUP_PHASE_UPDATE    (WM_APP + 1)
#define WM_SETUP_SERVICE_UPDATE  (WM_APP + 2)
#define WM_SETUP_NOTICE          (WM_APP + 3)
#define WM_SETUP_LOG             (WM_APP + 4)
#define WM_SETUP_FINISHED        (WM_APP + 5)
#define WM_SETUP_INSPECTION_DONE (WM_APP + 6)

static SetupContext g_ui_ctx;
static DWORD g_elapsed_sec = 0;
static bool g_setup_started = false;
static bool g_setup_finished = false;
static HANDLE g_hWorkerThread = NULL;

static HFONT g_hFontTitle = NULL;
static HFONT g_hFontBold = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontMono = NULL;

typedef struct {
    int phase;
    wchar_t phase_name[64];
    wchar_t status_text[256];
} MsgPhaseData;

typedef struct {
    int service_idx;
    ServiceLifecycleStatus status;
    wchar_t text[128];
} MsgServiceData;

typedef struct {
    HWND dialog;
    wchar_t dest[MAX_PATH];
    wchar_t payload_dir[MAX_PATH];
    bool payload_dir_specified;
    char installed_version[64];
    SetupOperationType operation;
    bool payload_present;
    bool config_present;
    bool incomplete;
    DWORD service_states[4];
} MsgInspectionData;

static bool ui_file_exists(const wchar_t* path) {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static DWORD WINAPI InspectionWorkerThread(LPVOID parameter) {
    MsgInspectionData* data = (MsgInspectionData*)parameter;
    unpack_read_installed_version(data->dest, data->installed_version,
                                  sizeof(data->installed_version));
    if (data->installed_version[0]) {
        int cmp = version_compare(data->installed_version, L4SETUP_VERSION_STRING);
        if (cmp > 0) data->operation = OP_UPGRADE;
        else if (cmp < 0) data->operation = OP_UPGRADE;
        else data->operation = unpack_is_idempotent(data->dest, L4SETUP_VERSION_STRING) ?
            OP_VERIFY : OP_REPAIR;
    } else {
        data->operation = OP_INSTALL;
    }
    data->incomplete = unpack_has_incomplete_marker(data->dest, NULL, 0);
    wchar_t config_path[MAX_PATH];
    swprintf_s(config_path, MAX_PATH, L"%ls\\mosquitto\\mosquitto.conf", data->dest);
    data->config_present = ui_file_exists(config_path);
    if (data->payload_dir_specified) {
        wchar_t archive_path[MAX_PATH];
        swprintf_s(archive_path, MAX_PATH, L"%ls\\tools.zip", data->payload_dir);
        data->payload_present = ui_file_exists(archive_path);
    }
    if (!data->payload_present) {
        data->payload_present = FindResourceW(NULL, L"PAYLOAD_X86", RT_RCDATA) != NULL ||
                                FindResourceW(NULL, L"PAYLOAD_X64", RT_RCDATA) != NULL;
    }
    if (!data->payload_present) {
        wchar_t module_dir[MAX_PATH];
        DWORD length = GetModuleFileNameW(NULL, module_dir, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            wchar_t* slash = wcsrchr(module_dir, L'\\');
            if (slash) {
                *slash = L'\0';
                wchar_t archive_path[MAX_PATH];
                swprintf_s(archive_path, MAX_PATH, L"%ls\\tools.zip", module_dir);
                data->payload_present = ui_file_exists(archive_path);
                if (!data->payload_present) {
                    swprintf_s(archive_path, MAX_PATH, L"%ls\\..\\dist\\tools.zip", module_dir);
                    data->payload_present = ui_file_exists(archive_path);
                }
            }
        }
    }
    data->service_states[0] = services_query_status(SVC_NAME_LEO4PROXY);
    data->service_states[1] = services_query_status(SVC_NAME_MOSQUITTO);
    data->service_states[2] = services_query_status(SVC_NAME_L4CON);
    data->service_states[3] = services_query_status(SVC_NAME_L4SUPERV);
    if (!PostMessageW(data->dialog, WM_SETUP_INSPECTION_DONE, 0, (LPARAM)data)) free(data);
    return 0;
}

static void update_progress_ui(HWND hDlg, int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    HWND hProg = GetDlgItem(hDlg, IDC_PROGRESS_BAR);
    if (hProg) {
        SendMessageW(hProg, PBM_SETPOS, (WPARAM)percent, 0);
    }
    wchar_t buf[32];
    swprintf_s(buf, sizeof(buf) / sizeof(wchar_t), L"%d%%", percent);
    SetDlgItemTextW(hDlg, IDC_STATIC_PERCENT, buf);
}

static void ui_on_phase_change(SetupPhase phase, const char* phase_name, const char* status_text, void* user_data) {
    HWND hDlg = (HWND)user_data;
    if (!hDlg) return;

    MsgPhaseData* msg = (MsgPhaseData*)malloc(sizeof(MsgPhaseData));
    if (!msg) return;

    msg->phase = (int)phase;
    MultiByteToWideChar(CP_UTF8, 0, phase_name, -1, msg->phase_name, 64);
    MultiByteToWideChar(CP_UTF8, 0, status_text, -1, msg->status_text, 256);

    PostMessageW(hDlg, WM_SETUP_PHASE_UPDATE, 0, (LPARAM)msg);
}

static void ui_on_service_status(
    int service_idx,
    const wchar_t* svc_name,
    ServiceLifecycleStatus status,
    DWORD elapsed_sec,
    const char* notice,
    void* user_data
) {
    HWND hDlg = (HWND)user_data;
    if (!hDlg) return;

    MsgServiceData* msg = (MsgServiceData*)malloc(sizeof(MsgServiceData));
    if (!msg) return;

    msg->service_idx = service_idx;
    msg->status = status;

    const wchar_t* st_badge = L"[  ...  ] Pending";
    switch (status) {
        case SVC_STATUS_STARTING: st_badge = L"[ START ] Starting"; break;
        case SVC_STATUS_RUNNING:  st_badge = L"[  OK   ] Running"; break;
        case SVC_STATUS_STOPPING: st_badge = L"[ STOP  ] Stopping"; break;
        case SVC_STATUS_STOPPED:  st_badge = L"[ STOP  ] Stopped"; break;
        case SVC_STATUS_CHECKING: st_badge = L"[ CHECK ] Checking"; break;
        case SVC_STATUS_READY:    st_badge = L"[ READY ] Ready"; break;
        case SVC_STATUS_FAILED:   st_badge = L"[ FAIL  ] FAILED"; break;
        default: break;
    }

    if (notice && notice[0]) {
        swprintf_s(msg->text, 128, L"%-10ls  %ls (%hs)", svc_name, st_badge, notice);
    } else if (status == SVC_STATUS_STARTING || status == SVC_STATUS_STOPPING) {
        swprintf_s(msg->text, 128, L"%-10ls  %ls (%lus)", svc_name, st_badge, elapsed_sec);
    } else {
        swprintf_s(msg->text, 128, L"%-10ls  %ls", svc_name, st_badge);
    }

    PostMessageW(hDlg, WM_SETUP_SERVICE_UPDATE, 0, (LPARAM)msg);
}

static void ui_on_notice(const char* notice_text, void* user_data) {
    HWND hDlg = (HWND)user_data;
    if (!hDlg || !notice_text) return;

    size_t len = strlen(notice_text) + 1;
    wchar_t* w_notice = (wchar_t*)malloc(len * sizeof(wchar_t));
    if (!w_notice) return;

    MultiByteToWideChar(CP_UTF8, 0, notice_text, -1, w_notice, (int)len);
    PostMessageW(hDlg, WM_SETUP_NOTICE, 0, (LPARAM)w_notice);
}

static void ui_on_log_line(const char* line, void* user_data) {
    HWND hDlg = (HWND)user_data;
    if (!hDlg || !line) return;

    size_t len = strlen(line) + 1;
    wchar_t* w_line = (wchar_t*)malloc(len * sizeof(wchar_t));
    if (!w_line) return;

    MultiByteToWideChar(CP_UTF8, 0, line, -1, w_line, (int)len);
    PostMessageW(hDlg, WM_SETUP_LOG, 0, (LPARAM)w_line);
}

static void ui_on_pipeline_finish(int exit_code, const char* final_status, void* user_data) {
    HWND hDlg = (HWND)user_data;
    if (!hDlg) return;

    PostMessageW(hDlg, WM_SETUP_FINISHED, (WPARAM)exit_code, 0);
}

static DWORD WINAPI SetupWorkerThread(LPVOID lpParam) {
    HWND hDlg = (HWND)lpParam;
    g_ui_ctx.user_data = (void*)hDlg;

    if (engine_phase_check(&g_ui_ctx)) engine_run_pipeline(&g_ui_ctx);
    PostMessageW(hDlg, WM_SETUP_FINISHED, (WPARAM)g_ui_ctx.final_exit_code, 0);
    return 0;
}

static void append_text_to_edit(HWND hEdit, const wchar_t* text) {
    if (!hEdit || !text) return;
    int len = GetWindowTextLengthW(hEdit);
    if (len > 300000) {
        SendMessageW(hEdit, EM_SETSEL, 0, 50000);
        SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)L"[... log truncated ...]\r\n");
        len = GetWindowTextLengthW(hEdit);
    }
    SendMessageW(hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
}

static INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            g_ui_ctx.user_data = (void*)hDlg;

            // Initialize log stream callback so details are captured live
            log_set_callback(ui_on_log_line, (void*)hDlg);

            // Set window icon if available
            HICON hIcon = LoadIconW(g_ui_ctx.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
            if (hIcon) {
                SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
                SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            }

            // Create modern Segoe UI and Consolas fonts
            g_hFontTitle = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontBold = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontNormal = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontMono = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

            if (g_hFontTitle) {
                SendDlgItemMessageW(hDlg, IDC_STATIC_HEADER, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);
            }
            if (g_hFontBold) {
                SendDlgItemMessageW(hDlg, IDC_STATIC_PHASE, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
                SendDlgItemMessageW(hDlg, IDC_STATIC_PERCENT, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
                SendDlgItemMessageW(hDlg, IDC_STATIC_SERVICES_LBL, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
            }
            if (g_hFontNormal) {
                SendDlgItemMessageW(hDlg, IDC_STATIC_VER_INFO, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendDlgItemMessageW(hDlg, IDC_STATIC_PATH_INFO, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendDlgItemMessageW(hDlg, IDC_STATIC_NOTICE, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendDlgItemMessageW(hDlg, IDC_STATIC_STATUS, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendDlgItemMessageW(hDlg, IDC_STATIC_ELAPSED, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendDlgItemMessageW(hDlg, IDC_SVC_LEO4PROXY, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendDlgItemMessageW(hDlg, IDC_SVC_MOSQUITTO, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendDlgItemMessageW(hDlg, IDC_SVC_L4CON, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendDlgItemMessageW(hDlg, IDC_SVC_L4SUPERV, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendDlgItemMessageW(hDlg, IDC_BTN_ACTION, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendDlgItemMessageW(hDlg, IDC_BTN_RETRY, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendDlgItemMessageW(hDlg, IDC_BTN_DETAILS, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
                SendDlgItemMessageW(hDlg, IDCANCEL, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
            }
            if (g_hFontMono) {
                SendDlgItemMessageW(hDlg, IDC_EDIT_DETAILS, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
            }

            // Setup Details Edit limit
            SendDlgItemMessageW(hDlg, IDC_EDIT_DETAILS, EM_SETLIMITTEXT, 0, 0);

            // Initialize progress bar
            update_progress_ui(hDlg, 0);

            // Center dialog
            RECT rc, rcOwner;
            HWND hwndOwner = GetDesktopWindow();
            GetWindowRect(hwndOwner, &rcOwner);
            GetWindowRect(hDlg, &rc);
            SetWindowPos(hDlg, HWND_TOP,
                         (rcOwner.right - rcOwner.left - (rc.right - rc.left)) / 2,
                         (rcOwner.bottom - rcOwner.top - (rc.bottom - rc.top)) / 2,
                         0, 0, SWP_NOSIZE);

            // Inspect only; all system-changing checks start after the action button.
            SetDlgItemTextW(hDlg, IDC_STATIC_PHASE, L"Phase: Inspect");
            SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, L"Checking installation, payload and services...");
            EnableWindow(GetDlgItem(hDlg, IDC_BTN_ACTION), FALSE);
            wchar_t path_buf[MAX_PATH + 32];
            swprintf_s(path_buf, sizeof(path_buf) / sizeof(path_buf[0]),
                       L"Destination: %ls", g_ui_ctx.opts->dest);
            SetDlgItemTextW(hDlg, IDC_STATIC_PATH_INFO, path_buf);
            MsgInspectionData* data = (MsgInspectionData*)calloc(1, sizeof(MsgInspectionData));
            if (!data) {
                SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, L"Cannot allocate inspection state.");
                return TRUE;
            }
            data->dialog = hDlg;
            wcscpy_s(data->dest, MAX_PATH, g_ui_ctx.opts->dest);
            data->payload_dir_specified = g_ui_ctx.opts->payload_dir_specified;
            if (data->payload_dir_specified)
                wcscpy_s(data->payload_dir, MAX_PATH, g_ui_ctx.opts->payload_dir);
            HANDLE inspection = CreateThread(NULL, 0, InspectionWorkerThread, data, 0, NULL);
            if (!inspection) {
                free(data);
                SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, L"Cannot start installation inspection.");
            } else {
                CloseHandle(inspection);
            }

            return TRUE;
        }

        case WM_SETUP_INSPECTION_DONE: {
            MsgInspectionData* data = (MsgInspectionData*)lParam;
            if (!data) return TRUE;
            strncpy_s(g_ui_ctx.installed_version, sizeof(g_ui_ctx.installed_version),
                      data->installed_version, _TRUNCATE);
            strncpy_s(g_ui_ctx.target_version, sizeof(g_ui_ctx.target_version),
                      L4SETUP_VERSION_STRING, _TRUNCATE);
            g_ui_ctx.op_type = data->operation;
            if (g_ui_ctx.opts->repair && data->installed_version[0] &&
                version_compare(data->installed_version, L4SETUP_VERSION_STRING) == 0)
                g_ui_ctx.op_type = OP_REPAIR;
            wchar_t ver_buf[256];
            swprintf_s(ver_buf, 256, L"Target: %hs | Operation: %hs | Installed: %hs",
                       L4SETUP_VERSION_STRING, engine_op_type_to_str(g_ui_ctx.op_type),
                       data->installed_version[0] ? data->installed_version : "None");
            SetDlgItemTextW(hDlg, IDC_STATIC_VER_INFO, ver_buf);
            const wchar_t* names[] = { L"Leo4Proxy", L"mosquitto", L"L4Con", L"L4Superv" };
            const int controls[] = { IDC_SVC_LEO4PROXY, IDC_SVC_MOSQUITTO,
                                     IDC_SVC_L4CON, IDC_SVC_L4SUPERV };
            for (int i = 0; i < 4; i++) {
                const wchar_t* label = L"Unknown or not registered";
                if (data->service_states[i] == SERVICE_RUNNING) label = L"Running";
                else if (data->service_states[i] == SERVICE_STOPPED) label = L"Stopped";
                else if (data->service_states[i] == SERVICE_START_PENDING) label = L"Starting";
                wchar_t row[128];
                swprintf_s(row, 128, L"%ls  %ls", names[i], label);
                SetDlgItemTextW(hDlg, controls[i], row);
            }
            bool downgrade = data->installed_version[0] &&
                version_compare(data->installed_version, L4SETUP_VERSION_STRING) > 0;
            if (downgrade) {
                g_ui_ctx.final_exit_code = 29;
                SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, L"Installed version is newer; downgrade is blocked.");
            } else if (!data->payload_present && g_ui_ctx.op_type != OP_VERIFY) {
                SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, L"No installation payload found; provide tools.zip before continuing.");
            } else {
                SetDlgItemTextW(hDlg, IDC_BTN_ACTION,
                    g_ui_ctx.op_type == OP_UPGRADE ? L"Upgrade" :
                    g_ui_ctx.op_type == OP_REPAIR ? L"Repair" :
                    g_ui_ctx.op_type == OP_VERIFY ? L"Verify" : L"Install");
                SetDlgItemTextW(hDlg, IDC_STATIC_STATUS,
                    data->incomplete ? L"Interrupted installation detected; recovery will run after you continue." :
                    data->config_present ? L"Inspection complete; Mosquitto config found." :
                    L"Inspection complete; Mosquitto config is missing.");
                EnableWindow(GetDlgItem(hDlg, IDC_BTN_ACTION), TRUE);
            }
            free(data);
            return TRUE;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            HWND hCtrl = (HWND)lParam;
            int ctrlId = GetDlgCtrlID(hCtrl);
            if (ctrlId == IDC_STATIC_HEADER) {
                SetTextColor(hdc, RGB(0, 51, 102));
                SetBkMode(hdc, TRANSPARENT);
                return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
            }
            if (ctrlId == IDC_STATIC_PHASE || ctrlId == IDC_STATIC_PERCENT) {
                SetTextColor(hdc, RGB(16, 76, 140));
                SetBkMode(hdc, TRANSPARENT);
                return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
            }
            if (ctrlId == IDC_SVC_LEO4PROXY || ctrlId == IDC_SVC_MOSQUITTO ||
                ctrlId == IDC_SVC_L4CON || ctrlId == IDC_SVC_L4SUPERV) {
                wchar_t label[128] = { 0 };
                GetWindowTextW(hCtrl, label, 128);
                if (wcsstr(label, L"Running")) SetTextColor(hdc, RGB(0, 100, 42));
                else if (wcsstr(label, L"FAILED") || wcsstr(label, L"Stopped"))
                    SetTextColor(hdc, RGB(154, 31, 31));
                else SetTextColor(hdc, RGB(16, 76, 140));
            }
            SetBkMode(hdc, TRANSPARENT);
            return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
        }

        case WM_TIMER: {
            if (g_setup_started && !g_setup_finished) {
                g_elapsed_sec++;
                wchar_t elapsed_buf[64];
                swprintf_s(elapsed_buf, 64, L"Elapsed: %lus", g_elapsed_sec);
                SetDlgItemTextW(hDlg, IDC_STATIC_ELAPSED, elapsed_buf);
            }
            return TRUE;
        }

        case WM_SETUP_PHASE_UPDATE: {
            MsgPhaseData* pData = (MsgPhaseData*)lParam;
            if (pData) {
                wchar_t ph_buf[128];
                swprintf_s(ph_buf, 128, L"Phase: %ls", pData->phase_name);
                SetDlgItemTextW(hDlg, IDC_STATIC_PHASE, ph_buf);
                SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, pData->status_text);

                int pct = 0;
                switch (pData->phase) {
                    case SETUP_PHASE_CHECK:    pct = 5;  break;
                    case SETUP_PHASE_PREPARE:  pct = 15; break;
                    case SETUP_PHASE_STOP:     pct = 30; break;
                    case SETUP_PHASE_UPDATE:   pct = 55; break;
                    case SETUP_PHASE_START:    pct = 70; break;
                    case SETUP_PHASE_VERIFY:   pct = 90; break;
                    case SETUP_PHASE_FINISH:   pct = 100; break;
                    default: break;
                }
                update_progress_ui(hDlg, pct);
                free(pData);
            }
            return TRUE;
        }

        case WM_SETUP_SERVICE_UPDATE: {
            MsgServiceData* sData = (MsgServiceData*)lParam;
            if (sData) {
                int ctrl_id = IDC_SVC_LEO4PROXY;
                if (sData->service_idx == 1) ctrl_id = IDC_SVC_MOSQUITTO;
                else if (sData->service_idx == 2) ctrl_id = IDC_SVC_L4CON;
                else if (sData->service_idx == 3) ctrl_id = IDC_SVC_L4SUPERV;

                SetDlgItemTextW(hDlg, ctrl_id, sData->text);

                if (sData->status == SVC_STATUS_RUNNING) {
                    int svc_pct = 70 + (sData->service_idx + 1) * 4;
                    update_progress_ui(hDlg, svc_pct);
                }
                free(sData);
            }
            return TRUE;
        }

        case WM_SETUP_NOTICE: {
            wchar_t* pNotice = (wchar_t*)lParam;
            if (pNotice) {
                SetDlgItemTextW(hDlg, IDC_STATIC_NOTICE, pNotice);
                free(pNotice);
            }
            return TRUE;
        }

        case WM_SETUP_LOG: {
            wchar_t* pLog = (wchar_t*)lParam;
            if (pLog) {
                append_text_to_edit(GetDlgItem(hDlg, IDC_EDIT_DETAILS), pLog);
                free(pLog);
            }
            return TRUE;
        }

        case WM_SETUP_FINISHED: {
            g_setup_finished = true;
            KillTimer(hDlg, 1);

            SetDlgItemTextW(hDlg, IDC_BTN_ACTION, L"Finish");
            EnableWindow(GetDlgItem(hDlg, IDC_BTN_ACTION), TRUE);
            SetDlgItemTextW(hDlg, IDCANCEL, L"Close");
            EnableWindow(GetDlgItem(hDlg, IDCANCEL), TRUE);

            if (g_ui_ctx.final_exit_code != 0 && g_ui_ctx.final_exit_code != 10 &&
                g_ui_ctx.final_exit_code != 11 && g_ui_ctx.final_exit_code != 12) {
                EnableWindow(GetDlgItem(hDlg, IDC_BTN_RETRY), TRUE);
            }

            if (g_ui_ctx.final_exit_code == 0) {
                update_progress_ui(hDlg, 100);
            }

            wchar_t final_msg[256];
            swprintf_s(final_msg, 256, L"Finished with status: %hs (exit code %d)",
                       g_ui_ctx.summary.status, g_ui_ctx.final_exit_code);
            SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, final_msg);
            SetDlgItemTextW(hDlg, IDC_STATIC_NOTICE, L"");

            return TRUE;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == IDC_BTN_ACTION) {
                if (!g_setup_started) {
                    // Start worker thread
                    g_setup_started = true;
                    EnableWindow(GetDlgItem(hDlg, IDC_BTN_ACTION), FALSE);
                    SetDlgItemTextW(hDlg, IDC_BTN_ACTION, L"Running...");
                    SetTimer(hDlg, 1, 1000, NULL);

                    g_hWorkerThread = CreateThread(NULL, 0, SetupWorkerThread, hDlg, 0, NULL);
                } else if (g_setup_finished) {
                    // Close dialog and return exit code
                    EndDialog(hDlg, g_ui_ctx.final_exit_code);
                }
                return TRUE;
            } else if (wmId == IDC_BTN_RETRY) {
                EnableWindow(GetDlgItem(hDlg, IDC_BTN_RETRY), FALSE);
                g_setup_finished = false;
                g_ui_ctx.retry_requested = true;
                g_hWorkerThread = CreateThread(NULL, 0, SetupWorkerThread, hDlg, 0, NULL);
                return TRUE;
            } else if (wmId == IDC_BTN_DETAILS) {
                HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_DETAILS);
                if (hEdit) {
                    BOOL is_visible = IsWindowVisible(hEdit);
                    ShowWindow(hEdit, is_visible ? SW_HIDE : SW_SHOW);
                    SetDlgItemTextW(hDlg, IDC_BTN_DETAILS, is_visible ? L"Show Details" : L"Hide Details");
                }
                return TRUE;
            } else if (wmId == IDCANCEL) {
                if (g_setup_started && !g_setup_finished) {
                    bool thread_alive = (g_hWorkerThread && WaitForSingleObject(g_hWorkerThread, 0) == WAIT_TIMEOUT);
                    if (!thread_alive) {
                        g_setup_finished = true;
                        EndDialog(hDlg, g_ui_ctx.final_exit_code);
                    } else if (g_ui_ctx.cancel_requested) {
                        EndDialog(hDlg, 31);
                    } else {
                        g_ui_ctx.cancel_requested = true;
                        SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, L"Finishing current operation safely (press Cancel/Close again to force exit)...");
                    }
                } else {
                    EndDialog(hDlg, g_setup_finished ? g_ui_ctx.final_exit_code : 31);
                }
                return TRUE;
            }
            break;
        }

        case WM_CLOSE: {
            if (g_setup_started && !g_setup_finished) {
                bool thread_alive = (g_hWorkerThread && WaitForSingleObject(g_hWorkerThread, 0) == WAIT_TIMEOUT);
                if (!thread_alive) {
                    g_setup_finished = true;
                    EndDialog(hDlg, g_ui_ctx.final_exit_code);
                } else if (g_ui_ctx.cancel_requested) {
                    EndDialog(hDlg, 31);
                } else {
                    g_ui_ctx.cancel_requested = true;
                    SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, L"Finishing current operation safely (press Close again to force exit)...");
                }
                return TRUE;
            } else {
                EndDialog(hDlg, g_setup_finished ? g_ui_ctx.final_exit_code : 31);
                return TRUE;
            }
        }

        case WM_DESTROY: {
            log_set_callback(NULL, NULL);
            if (g_hFontTitle)  { DeleteObject(g_hFontTitle);  g_hFontTitle = NULL; }
            if (g_hFontBold)   { DeleteObject(g_hFontBold);   g_hFontBold = NULL; }
            if (g_hFontNormal) { DeleteObject(g_hFontNormal); g_hFontNormal = NULL; }
            if (g_hFontMono)   { DeleteObject(g_hFontMono);   g_hFontMono = NULL; }
            return TRUE;
        }
    }

    return FALSE;
}

int ui_run_interactive_setup(HINSTANCE hInstance, CliOptions* opts) {
    if (!opts) return 1;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    memset(&g_ui_ctx, 0, sizeof(SetupContext));
    g_ui_ctx.opts = opts;
    g_ui_ctx.hInstance = hInstance;
    g_ui_ctx.is_interactive = true;

    g_ui_ctx.on_phase_change = ui_on_phase_change;
    g_ui_ctx.on_service_status = ui_on_service_status;
    g_ui_ctx.on_notice = ui_on_notice;
    g_ui_ctx.on_log_line = ui_on_log_line;
    g_ui_ctx.on_pipeline_finish = ui_on_pipeline_finish;

    INT_PTR res = DialogBoxParamW(
        hInstance,
        MAKEINTRESOURCEW(IDD_MAIN_DIALOG),
        NULL,
        MainDlgProc,
        0
    );

    if (g_hWorkerThread) {
        WaitForSingleObject(g_hWorkerThread, 1500);
        CloseHandle(g_hWorkerThread);
        g_hWorkerThread = NULL;
    }

    if (g_ui_ctx.hMutex) {
        CloseHandle(g_ui_ctx.hMutex);
        g_ui_ctx.hMutex = NULL;
    }

    return (int)res;
}
