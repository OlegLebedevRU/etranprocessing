#include "ui.h"
#include "engine.h"
#include "log.h"
#include "../res/resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WM_SETUP_PHASE_UPDATE    (WM_APP + 1)
#define WM_SETUP_SERVICE_UPDATE  (WM_APP + 2)
#define WM_SETUP_NOTICE          (WM_APP + 3)
#define WM_SETUP_LOG             (WM_APP + 4)
#define WM_SETUP_FINISHED        (WM_APP + 5)

static SetupContext g_ui_ctx;
static DWORD g_elapsed_sec = 0;
static bool g_setup_started = false;
static bool g_setup_finished = false;
static HANDLE g_hWorkerThread = NULL;

typedef struct {
    int phase;
    wchar_t phase_name[64];
    wchar_t status_text[256];
} MsgPhaseData;

typedef struct {
    int service_idx;
    wchar_t text[128];
} MsgServiceData;

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

    const wchar_t* st_str = L"Pending";
    switch (status) {
        case SVC_STATUS_STARTING: st_str = L"Starting"; break;
        case SVC_STATUS_RUNNING:  st_str = L"Running"; break;
        case SVC_STATUS_STOPPING: st_str = L"Stopping"; break;
        case SVC_STATUS_STOPPED:  st_str = L"Stopped"; break;
        case SVC_STATUS_CHECKING: st_str = L"Checking"; break;
        case SVC_STATUS_READY:    st_str = L"Ready"; break;
        case SVC_STATUS_FAILED:   st_str = L"Failed"; break;
        default: break;
    }

    if (notice && notice[0]) {
        swprintf_s(msg->text, 128, L"%-11ls %ls (%hs)", svc_name, st_str, notice);
    } else if (status == SVC_STATUS_STARTING || status == SVC_STATUS_STOPPING) {
        swprintf_s(msg->text, 128, L"%-11ls %ls (%lus)", svc_name, st_str, elapsed_sec);
    } else {
        swprintf_s(msg->text, 128, L"%-11ls %ls", svc_name, st_str);
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

    engine_run_pipeline(&g_ui_ctx);
    PostMessageW(hDlg, WM_SETUP_FINISHED, (WPARAM)g_ui_ctx.final_exit_code, 0);
    return 0;
}

static void append_text_to_edit(HWND hEdit, const wchar_t* text) {
    if (!hEdit || !text) return;
    int len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
}

static INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            g_ui_ctx.user_data = (void*)hDlg;

            // Set window icon if available
            HICON hIcon = LoadIconW(g_ui_ctx.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
            if (hIcon) {
                SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
                SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            }

            // Center dialog
            RECT rc, rcOwner;
            HWND hwndOwner = GetDesktopWindow();
            GetWindowRect(hwndOwner, &rcOwner);
            GetWindowRect(hDlg, &rc);
            SetWindowPos(hDlg, HWND_TOP,
                         (rcOwner.right - rcOwner.left - (rc.right - rc.left)) / 2,
                         (rcOwner.bottom - rcOwner.top - (rc.bottom - rc.top)) / 2,
                         0, 0, SWP_NOSIZE);

            // Phase 1: Check
            bool check_ok = engine_phase_check(&g_ui_ctx);

            wchar_t ver_buf[256];
            swprintf_s(ver_buf, 256, L"Target Version: %hs | Operation: %hs (Installed: %hs)",
                       g_ui_ctx.target_version,
                       engine_op_type_to_str(g_ui_ctx.op_type),
                       g_ui_ctx.installed_version[0] ? g_ui_ctx.installed_version : "None");
            SetDlgItemTextW(hDlg, IDC_STATIC_VER_INFO, ver_buf);

            wchar_t path_buf[MAX_PATH + 32];
            swprintf_s(path_buf, sizeof(path_buf)/sizeof(wchar_t), L"Destination: %ls", g_ui_ctx.opts->dest);
            SetDlgItemTextW(hDlg, IDC_STATIC_PATH_INFO, path_buf);

            // Set button text to match operation
            const wchar_t* btn_text = L"Install";
            if (g_ui_ctx.op_type == OP_UPGRADE) btn_text = L"Upgrade";
            else if (g_ui_ctx.op_type == OP_REPAIR) btn_text = L"Repair";
            else if (g_ui_ctx.op_type == OP_VERIFY) btn_text = L"Verify";
            SetDlgItemTextW(hDlg, IDC_BTN_ACTION, btn_text);

            if (!check_ok) {
                wchar_t err_msg[256];
                swprintf_s(err_msg, 256, L"Check Failed: %hs (code %d)",
                           g_ui_ctx.summary.error_reason[0] ? g_ui_ctx.summary.error_reason : "prerequisites not met",
                           g_ui_ctx.final_exit_code);
                SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, err_msg);
                EnableWindow(GetDlgItem(hDlg, IDC_BTN_ACTION), FALSE);
            }

            return TRUE;
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
    }

    return FALSE;
}

int ui_run_interactive_setup(HINSTANCE hInstance, CliOptions* opts) {
    if (!opts) return 1;

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
