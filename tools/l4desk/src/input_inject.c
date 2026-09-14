#include "input_inject.h"
#include "desktop_state.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static bool s_key_is_down[256] = { false };
static bool s_mouse_button_down[2] = { false, false }; /* 0: left, 1: right */
static bool s_pending_cleanup = false;

typedef struct {
    bool allow_f12;
    bool allow_alt_f4;
    bool allow_win_d;
    wchar_t kiosk_process[MAX_PATH];
} ShortcutPolicy;

static ShortcutPolicy s_policy = { false, false, false, { 0 } };

void input_set_shortcut_policy(bool allow_f12, bool allow_alt_f4, bool allow_win_d, const wchar_t* kiosk_process) {
    s_policy.allow_f12 = allow_f12;
    s_policy.allow_alt_f4 = allow_alt_f4;
    s_policy.allow_win_d = allow_win_d;
    if (kiosk_process) {
        wcsncpy_s(s_policy.kiosk_process, MAX_PATH, kiosk_process, _TRUNCATE);
    } else {
        s_policy.kiosk_process[0] = L'\0';
    }
}

bool input_get_shortcut_policy(bool* out_f12, bool* out_alt_f4, bool* out_win_d) {
    if (out_f12) *out_f12 = s_policy.allow_f12;
    if (out_alt_f4) *out_alt_f4 = s_policy.allow_alt_f4;
    if (out_win_d) *out_win_d = s_policy.allow_win_d;
    return true;
}

static void input_check_pending_cleanup(void) {
    if (s_pending_cleanup && desktop_check_input_access() == DESKTOP_ACCESS_OK) {
        input_release_all();
        s_pending_cleanup = false;
    }
}

void input_map_coordinates_custom(double nx, double ny,
                                 int rect_x, int rect_y, int rect_w, int rect_h,
                                 int virt_x, int virt_y, int virt_w, int virt_h,
                                 int* out_norm_x, int* out_norm_y) {
    /* 1. Scale detection: if value > 1.0, scale from 65535.0, otherwise treat as 0.0..1.0 */
    if (nx > 1.0) {
        nx /= 65535.0;
    }
    if (ny > 1.0) {
        ny /= 65535.0;
    }

    if (nx < 0.0) nx = 0.0;
    if (nx > 1.0) nx = 1.0;
    if (ny < 0.0) ny = 0.0;
    if (ny > 1.0) ny = 1.0;

    /* 2. Map normalized frame coords to monitor virtual coords taking into account monitor offset */
    int target_x = rect_x + (int)(nx * (double)rect_w);
    int target_y = rect_y + (int)(ny * (double)rect_h);

    /* 3. Normalize for SendInput relative to entire virtual screen */
    double div_w = (virt_w > 1) ? (double)(virt_w - 1) : 1.0;
    double div_h = (virt_h > 1) ? (double)(virt_h - 1) : 1.0;

    int norm_x = (int)(((double)(target_x - virt_x) * 65535.0) / div_w);
    int norm_y = (int)(((double)(target_y - virt_y) * 65535.0) / div_h);

    if (norm_x < 0) norm_x = 0;
    if (norm_x > 65535) norm_x = 65535;
    if (norm_y < 0) norm_y = 0;
    if (norm_y > 65535) norm_y = 65535;

    if (out_norm_x) *out_norm_x = norm_x;
    if (out_norm_y) *out_norm_y = norm_y;
}

void input_map_coordinates(double nx, double ny,
                           int rect_x, int rect_y, int rect_w, int rect_h,
                           int* out_norm_x, int* out_norm_y) {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) {
        vx = 0;
        vy = 0;
        vw = GetSystemMetrics(SM_CXSCREEN);
        vh = GetSystemMetrics(SM_CYSCREEN);
    }
    input_map_coordinates_custom(nx, ny, rect_x, rect_y, rect_w, rect_h,
                                 vx, vy, vw, vh, out_norm_x, out_norm_y);
}

bool input_inject_move_norm(double nx, double ny,
                            int rect_x, int rect_y, int rect_w, int rect_h,
                            DWORD* out_error) {
    int norm_x = 0, norm_y = 0;
    input_map_coordinates(nx, ny, rect_x, rect_y, rect_w, rect_h, &norm_x, &norm_y);
    return input_inject_move(norm_x, norm_y, out_error);
}

static bool input_inject_click_abs(int norm_x, int norm_y, const char* button, DWORD* out_error) {
    if (out_error) *out_error = 0;

    DWORD down_flag = MOUSEEVENTF_LEFTDOWN;
    DWORD up_flag = MOUSEEVENTF_LEFTUP;
    int btn_idx = 0;

    if (!button || button[0] == '\0' || _stricmp(button, "left") == 0) {
        down_flag = MOUSEEVENTF_LEFTDOWN;
        up_flag = MOUSEEVENTF_LEFTUP;
        btn_idx = 0;
    } else if (_stricmp(button, "right") == 0) {
        down_flag = MOUSEEVENTF_RIGHTDOWN;
        up_flag = MOUSEEVENTF_RIGHTUP;
        btn_idx = 1;
    } else {
        /* Disallow middle or unknown buttons per public input contract */
        if (out_error) *out_error = ERROR_INVALID_PARAMETER;
        return false;
    }

    if (desktop_check_input_access() != DESKTOP_ACCESS_OK) {
        if (out_error) *out_error = ERROR_BUSY;
        return false;
    }
    input_check_pending_cleanup();

    /* Stage 1: Send MOVE + DOWN in single atomic batch */
    INPUT inps_stage1[2];
    memset(inps_stage1, 0, sizeof(inps_stage1));

    inps_stage1[0].type = INPUT_MOUSE;
    inps_stage1[0].mi.dx = (LONG)norm_x;
    inps_stage1[0].mi.dy = (LONG)norm_y;
    inps_stage1[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    inps_stage1[1].type = INPUT_MOUSE;
    inps_stage1[1].mi.dx = (LONG)norm_x;
    inps_stage1[1].mi.dy = (LONG)norm_y;
    inps_stage1[1].mi.dwFlags = down_flag | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    UINT sent1 = SendInput(2, inps_stage1, sizeof(INPUT));
    if (sent1 != 2) {
        if (out_error) *out_error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
        return false;
    }
    s_mouse_button_down[btn_idx] = true;

    /* Stage 2: 40 ms delay between DOWN and UP */
    Sleep(40);

    /* Verify desktop hasn't changed to secure desktop / Winlogon between DOWN and UP */
    if (desktop_check_input_access() != DESKTOP_ACCESS_OK) {
        s_pending_cleanup = true;
        if (out_error) *out_error = ERROR_BUSY;
        return false;
    }

    /* Stage 3: Send UP */
    INPUT inp_up;
    memset(&inp_up, 0, sizeof(inp_up));
    inp_up.type = INPUT_MOUSE;
    inp_up.mi.dx = (LONG)norm_x;
    inp_up.mi.dy = (LONG)norm_y;
    inp_up.mi.dwFlags = up_flag | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    UINT sent2 = SendInput(1, &inp_up, sizeof(INPUT));
    if (sent2 != 1) {
        s_pending_cleanup = true;
        if (out_error) *out_error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
        return false;
    }
    s_mouse_button_down[btn_idx] = false;
    return true;
}

bool input_inject_click_norm(double nx, double ny, const char* button,
                             int rect_x, int rect_y, int rect_w, int rect_h,
                             DWORD* out_error) {
    int norm_x = 0, norm_y = 0;
    input_map_coordinates(nx, ny, rect_x, rect_y, rect_w, rect_h, &norm_x, &norm_y);
    return input_inject_click_abs(norm_x, norm_y, button, out_error);
}

bool input_inject_move(int x, int y, DWORD* out_error) {
    if (out_error) *out_error = 0;

    if (desktop_check_input_access() != DESKTOP_ACCESS_OK) {
        if (out_error) *out_error = ERROR_BUSY;
        return false;
    }
    input_check_pending_cleanup();

    INPUT inp;
    memset(&inp, 0, sizeof(INPUT));
    inp.type = INPUT_MOUSE;
    inp.mi.dx = (LONG)x;
    inp.mi.dy = (LONG)y;
    inp.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    UINT sent = SendInput(1, &inp, sizeof(INPUT));
    if (sent != 1) {
        if (out_error) *out_error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
        return false;
    }
    return true;
}

bool input_inject_click(int x, int y, DWORD* out_error) {
    return input_inject_click_abs(x, y, "left", out_error);
}

bool input_is_vk_allowed(int vk) {
    /* Forbidden keys */
    if (vk == VK_LWIN || vk == VK_RWIN || vk == VK_APPS) return false;

    /* F12 is allowed only in verified application profile */
    if (vk == VK_F12) {
        return s_policy.allow_f12;
    }

    /* Prevent synthesizing Alt+F4 via ordinary key_event */
    if (vk == VK_F4) {
        SHORT alt_st = GetAsyncKeyState(VK_MENU);
        if ((alt_st & 0x8000) || s_key_is_down[VK_MENU]) {
            return false;
        }
    }

    if (vk >= 'A' && vk <= 'Z') return true;
    if (vk >= '0' && vk <= '9') return true;
    if (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE) return true;
    if (vk >= VK_F1 && vk <= VK_F11) return true;
    if (vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_TAB || vk == VK_BACK || vk == VK_SPACE) return true;
    if (vk >= VK_PRIOR && vk <= VK_DOWN) return true; /* Prior, Next, End, Home, Left, Up, Right, Down */
    if (vk == VK_INSERT || vk == VK_DELETE) return true;
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_CAPITAL) return true;
    if (vk >= VK_OEM_1 && vk <= VK_OEM_3) return true;
    if (vk >= VK_OEM_4 && vk <= VK_OEM_7) return true;

    return false;
}

bool input_inject_key(const char* kind, int vk, const char* text, DWORD* out_error) {
    if (out_error) *out_error = 0;

    if (desktop_check_input_access() != DESKTOP_ACCESS_OK) {
        if (out_error) *out_error = ERROR_BUSY;
        return false;
    }
    input_check_pending_cleanup();

    /* Check for Unicode text insertion first */
    if (text && text[0] != '\0') {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
        if (wlen > 1) {
            wchar_t* wbuf = (wchar_t*)malloc(wlen * sizeof(wchar_t));
            if (!wbuf) {
                if (out_error) *out_error = ERROR_OUTOFMEMORY;
                return false;
            }
            MultiByteToWideChar(CP_UTF8, 0, text, -1, wbuf, wlen);

            for (int i = 0; i < wlen - 1; i++) {
                INPUT inps[2];
                memset(inps, 0, sizeof(inps));
                inps[0].type = INPUT_KEYBOARD;
                inps[0].ki.wScan = wbuf[i];
                inps[0].ki.dwFlags = KEYEVENTF_UNICODE;

                inps[1].type = INPUT_KEYBOARD;
                inps[1].ki.wScan = wbuf[i];
                inps[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

                UINT sent = SendInput(2, inps, sizeof(INPUT));
                if (sent != 2) {
                    if (out_error) *out_error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
                    free(wbuf);
                    return false;
                }
            }
            free(wbuf);
            return true;
        }
    }

    if (vk <= 0) {
        if (out_error) *out_error = ERROR_INVALID_PARAMETER;
        return false;
    }

    if (!input_is_vk_allowed(vk)) {
        if (out_error) *out_error = ERROR_ACCESS_DENIED;
        return false;
    }

    /* Prevent Ctrl+Alt+Del injection */
    if (vk == VK_DELETE) {
        SHORT ctrl_st = GetAsyncKeyState(VK_CONTROL);
        SHORT alt_st = GetAsyncKeyState(VK_MENU);
        if (((ctrl_st & 0x8000) || s_key_is_down[VK_CONTROL]) &&
            ((alt_st & 0x8000) || s_key_is_down[VK_MENU])) {
            if (out_error) *out_error = ERROR_ACCESS_DENIED;
            return false;
        }
    }

    bool is_up = (kind && _stricmp(kind, "up") == 0);
    bool is_press = (!kind || kind[0] == '\0' || _stricmp(kind, "press") == 0);

    WORD scan = (WORD)MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);

    DWORD ext_flag = 0;
    if ((vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_DELETE || vk == VK_INSERT ||
        vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_DIVIDE) {
        ext_flag = KEYEVENTF_EXTENDEDKEY;
    }

    if (is_press) {
        INPUT inp_down;
        memset(&inp_down, 0, sizeof(inp_down));
        inp_down.type = INPUT_KEYBOARD;
        inp_down.ki.wVk = (WORD)vk;
        inp_down.ki.wScan = scan;
        inp_down.ki.dwFlags = ext_flag;

        UINT sent1 = SendInput(1, &inp_down, sizeof(INPUT));
        if (sent1 != 1) {
            if (out_error) *out_error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
            return false;
        }
        if (vk >= 0 && vk < 256) s_key_is_down[vk] = true;

        Sleep(40);

        if (desktop_check_input_access() != DESKTOP_ACCESS_OK) {
            s_pending_cleanup = true;
            if (out_error) *out_error = ERROR_BUSY;
            return false;
        }

        INPUT inp_up;
        memset(&inp_up, 0, sizeof(inp_up));
        inp_up.type = INPUT_KEYBOARD;
        inp_up.ki.wVk = (WORD)vk;
        inp_up.ki.wScan = scan;
        inp_up.ki.dwFlags = ext_flag | KEYEVENTF_KEYUP;

        UINT sent2 = SendInput(1, &inp_up, sizeof(INPUT));
        if (sent2 != 1) {
            s_pending_cleanup = true;
            if (out_error) *out_error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
            return false;
        }
        if (vk >= 0 && vk < 256) s_key_is_down[vk] = false;
    } else {
        INPUT inp;
        memset(&inp, 0, sizeof(INPUT));
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = (WORD)vk;
        inp.ki.wScan = scan;
        inp.ki.dwFlags = ext_flag | (is_up ? KEYEVENTF_KEYUP : 0);

        UINT sent = SendInput(1, &inp, sizeof(INPUT));
        if (sent != 1) {
            if (out_error) *out_error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
            return false;
        }
        if (vk >= 0 && vk < 256) {
            s_key_is_down[vk] = !is_up;
        }
    }

    return true;
}

void input_release_all(void) {
    if (desktop_check_input_access() != DESKTOP_ACCESS_OK) {
        s_pending_cleanup = true;
        return;
    }

    /* 1. Release own tracked mouse buttons */
    if (s_mouse_button_down[0]) {
        INPUT inp;
        memset(&inp, 0, sizeof(INPUT));
        inp.type = INPUT_MOUSE;
        inp.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &inp, sizeof(INPUT));
        s_mouse_button_down[0] = false;
    }
    if (s_mouse_button_down[1]) {
        INPUT inp;
        memset(&inp, 0, sizeof(INPUT));
        inp.type = INPUT_MOUSE;
        inp.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        SendInput(1, &inp, sizeof(INPUT));
        s_mouse_button_down[1] = false;
    }

    /* 2. Release own tracked active keyboard virtual keys */
    for (int vk = 1; vk < 256; vk++) {
        if (s_key_is_down[vk]) {
            WORD scan = (WORD)MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);

            DWORD ext_flag = 0;
            if ((vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_DELETE || vk == VK_INSERT ||
                vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_DIVIDE || vk == VK_LWIN || vk == VK_RWIN) {
                ext_flag = KEYEVENTF_EXTENDEDKEY;
            }

            INPUT inp;
            memset(&inp, 0, sizeof(inp));
            inp.type = INPUT_KEYBOARD;
            inp.ki.wVk = (WORD)vk;
            inp.ki.wScan = scan;
            inp.ki.dwFlags = ext_flag | KEYEVENTF_KEYUP;

            SendInput(1, &inp, sizeof(INPUT));
            s_key_is_down[vk] = false;
        }
    }
    s_pending_cleanup = false;
}

bool input_verify_alt_f4_target(HWND* out_target) {
    if (out_target) *out_target = NULL;

    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }

    HWND shellHwnd = GetShellWindow();
    if (shellHwnd && hwnd == shellHwnd) {
        return false;
    }

    wchar_t clsName[256] = { 0 };
    if (GetClassNameW(hwnd, clsName, sizeof(clsName) / sizeof(wchar_t)) > 0) {
        if (_wcsicmp(clsName, L"Progman") == 0 ||
            _wcsicmp(clsName, L"WorkerW") == 0 ||
            _wcsicmp(clsName, L"Shell_TrayWnd") == 0 ||
            _wcsicmp(clsName, L"Shell_SecondaryTrayWnd") == 0) {
            return false;
        }
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) {
        return false;
    }

    /* Check process name if accessible */
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        wchar_t imgPath[MAX_PATH] = { 0 };
        DWORD imgLen = MAX_PATH;
        if (QueryFullProcessImageNameW(hProc, 0, imgPath, &imgLen)) {
            const wchar_t* fileName = wcsrchr(imgPath, L'\\');
            fileName = fileName ? (fileName + 1) : imgPath;
            if (_wcsicmp(fileName, L"explorer.exe") == 0) {
                CloseHandle(hProc);
                return false;
            }
            if (s_policy.kiosk_process[0] != L'\0' && _wcsicmp(fileName, s_policy.kiosk_process) == 0) {
                CloseHandle(hProc);
                return false;
            }
        }
        CloseHandle(hProc);
    }

    if (out_target) *out_target = hwnd;
    return true;
}

bool input_inject_shortcut_f12(DWORD* out_error) {
    if (out_error) *out_error = 0;

    if (!s_policy.allow_f12) {
        if (out_error) *out_error = ERROR_ACCESS_DENIED;
        return false;
    }
    if (desktop_check_input_access() != DESKTOP_ACCESS_OK) {
        if (out_error) *out_error = ERROR_BUSY;
        return false;
    }
    input_check_pending_cleanup();

    WORD scan = (WORD)MapVirtualKeyW(VK_F12, MAPVK_VK_TO_VSC);

    INPUT inp_down;
    memset(&inp_down, 0, sizeof(inp_down));
    inp_down.type = INPUT_KEYBOARD;
    inp_down.ki.wVk = VK_F12;
    inp_down.ki.wScan = scan;

    if (SendInput(1, &inp_down, sizeof(INPUT)) != 1) {
        if (out_error) *out_error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
        return false;
    }
    s_key_is_down[VK_F12] = true;

    Sleep(40);

    if (desktop_check_input_access() != DESKTOP_ACCESS_OK) {
        s_pending_cleanup = true;
        if (out_error) *out_error = ERROR_BUSY;
        return false;
    }

    INPUT inp_up;
    memset(&inp_up, 0, sizeof(inp_up));
    inp_up.type = INPUT_KEYBOARD;
    inp_up.ki.wVk = VK_F12;
    inp_up.ki.wScan = scan;
    inp_up.ki.dwFlags = KEYEVENTF_KEYUP;

    if (SendInput(1, &inp_up, sizeof(INPUT)) != 1) {
        s_pending_cleanup = true;
        if (out_error) *out_error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
        return false;
    }
    s_key_is_down[VK_F12] = false;
    return true;
}

bool input_inject_shortcut_alt_f4(DWORD* out_error) {
    if (out_error) *out_error = 0;

    if (!s_policy.allow_alt_f4) {
        if (out_error) *out_error = ERROR_ACCESS_DENIED;
        return false;
    }
    if (desktop_check_input_access() != DESKTOP_ACCESS_OK) {
        if (out_error) *out_error = ERROR_BUSY;
        return false;
    }
    input_check_pending_cleanup();

    HWND target = NULL;
    if (!input_verify_alt_f4_target(&target)) {
        if (out_error) *out_error = ERROR_INVALID_TARGET_HANDLE;
        return false;
    }

    /* Re-verify foreground window right before injection */
    HWND curFg = GetForegroundWindow();
    if (curFg != target) {
        if (out_error) *out_error = ERROR_INVALID_TARGET_HANDLE;
        return false;
    }

    WORD scan_alt = (WORD)MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC);
    WORD scan_f4 = (WORD)MapVirtualKeyW(VK_F4, MAPVK_VK_TO_VSC);

    INPUT inps_down[2];
    memset(inps_down, 0, sizeof(inps_down));
    inps_down[0].type = INPUT_KEYBOARD;
    inps_down[0].ki.wVk = VK_MENU;
    inps_down[0].ki.wScan = scan_alt;

    inps_down[1].type = INPUT_KEYBOARD;
    inps_down[1].ki.wVk = VK_F4;
    inps_down[1].ki.wScan = scan_f4;

    if (SendInput(2, inps_down, sizeof(INPUT)) != 2) {
        if (out_error) *out_error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
        input_release_all();
        return false;
    }
    s_key_is_down[VK_MENU] = true;
    s_key_is_down[VK_F4] = true;

    Sleep(40);

    if (desktop_check_input_access() != DESKTOP_ACCESS_OK) {
        s_pending_cleanup = true;
        if (out_error) *out_error = ERROR_BUSY;
        return false;
    }

    INPUT inps_up[2];
    memset(inps_up, 0, sizeof(inps_up));
    inps_up[0].type = INPUT_KEYBOARD;
    inps_up[0].ki.wVk = VK_F4;
    inps_up[0].ki.wScan = scan_f4;
    inps_up[0].ki.dwFlags = KEYEVENTF_KEYUP;

    inps_up[1].type = INPUT_KEYBOARD;
    inps_up[1].ki.wVk = VK_MENU;
    inps_up[1].ki.wScan = scan_alt;
    inps_up[1].ki.dwFlags = KEYEVENTF_KEYUP;

    if (SendInput(2, inps_up, sizeof(INPUT)) != 2) {
        s_pending_cleanup = true;
        if (out_error) *out_error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
        return false;
    }
    s_key_is_down[VK_F4] = false;
    s_key_is_down[VK_MENU] = false;
    return true;
}

bool input_inject_shortcut_win_d(DWORD* out_error) {
    if (out_error) *out_error = 0;

    if (!s_policy.allow_win_d) {
        if (out_error) *out_error = ERROR_ACCESS_DENIED;
        return false;
    }
    if (desktop_check_input_access() != DESKTOP_ACCESS_OK) {
        if (out_error) *out_error = ERROR_BUSY;
        return false;
    }
    input_check_pending_cleanup();

    WORD scan_win = (WORD)MapVirtualKeyW(VK_LWIN, MAPVK_VK_TO_VSC);
    WORD scan_d = (WORD)MapVirtualKeyW('D', MAPVK_VK_TO_VSC);

    INPUT inps_down[2];
    memset(inps_down, 0, sizeof(inps_down));
    inps_down[0].type = INPUT_KEYBOARD;
    inps_down[0].ki.wVk = VK_LWIN;
    inps_down[0].ki.wScan = scan_win;
    inps_down[0].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;

    inps_down[1].type = INPUT_KEYBOARD;
    inps_down[1].ki.wVk = 'D';
    inps_down[1].ki.wScan = scan_d;

    if (SendInput(2, inps_down, sizeof(INPUT)) != 2) {
        if (out_error) *out_error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
        input_release_all();
        return false;
    }
    s_key_is_down[VK_LWIN] = true;
    s_key_is_down['D'] = true;

    Sleep(40);

    if (desktop_check_input_access() != DESKTOP_ACCESS_OK) {
        s_pending_cleanup = true;
        if (out_error) *out_error = ERROR_BUSY;
        return false;
    }

    INPUT inps_up[2];
    memset(inps_up, 0, sizeof(inps_up));
    inps_up[0].type = INPUT_KEYBOARD;
    inps_up[0].ki.wVk = 'D';
    inps_up[0].ki.wScan = scan_d;
    inps_up[0].ki.dwFlags = KEYEVENTF_KEYUP;

    inps_up[1].type = INPUT_KEYBOARD;
    inps_up[1].ki.wVk = VK_LWIN;
    inps_up[1].ki.wScan = scan_win;
    inps_up[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;

    if (SendInput(2, inps_up, sizeof(INPUT)) != 2) {
        s_pending_cleanup = true;
        if (out_error) *out_error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
        return false;
    }
    s_key_is_down['D'] = false;
    s_key_is_down[VK_LWIN] = false;
    return true;
}
