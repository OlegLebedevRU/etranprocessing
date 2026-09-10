#include "input_inject.h"
#include <string.h>
#include <stdlib.h>

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

bool input_inject_click_norm(double nx, double ny, const char* button,
                             int rect_x, int rect_y, int rect_w, int rect_h,
                             DWORD* out_error) {
    if (out_error) *out_error = 0;

    int norm_x = 0, norm_y = 0;
    input_map_coordinates(nx, ny, rect_x, rect_y, rect_w, rect_h, &norm_x, &norm_y);

    DWORD down_flag = MOUSEEVENTF_LEFTDOWN;
    DWORD up_flag = MOUSEEVENTF_LEFTUP;
    if (button) {
        if (_stricmp(button, "right") == 0) {
            down_flag = MOUSEEVENTF_RIGHTDOWN;
            up_flag = MOUSEEVENTF_RIGHTUP;
        } else if (_stricmp(button, "middle") == 0) {
            down_flag = MOUSEEVENTF_MIDDLEDOWN;
            up_flag = MOUSEEVENTF_MIDDLEUP;
        }
    }

    INPUT inps[3];
    memset(inps, 0, sizeof(inps));

    // 0: Move
    inps[0].type = INPUT_MOUSE;
    inps[0].mi.dx = (LONG)norm_x;
    inps[0].mi.dy = (LONG)norm_y;
    inps[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    // 1: Down
    inps[1].type = INPUT_MOUSE;
    inps[1].mi.dx = (LONG)norm_x;
    inps[1].mi.dy = (LONG)norm_y;
    inps[1].mi.dwFlags = down_flag | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    // 2: Up
    inps[2].type = INPUT_MOUSE;
    inps[2].mi.dx = (LONG)norm_x;
    inps[2].mi.dy = (LONG)norm_y;
    inps[2].mi.dwFlags = up_flag | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    UINT sent = SendInput(3, inps, sizeof(INPUT));
    if (sent != 3) {
        if (out_error) *out_error = GetLastError();
        return false;
    }
    return true;
}

bool input_inject_move(int x, int y, DWORD* out_error) {
    if (out_error) *out_error = 0;

    INPUT inp;
    memset(&inp, 0, sizeof(INPUT));
    inp.type = INPUT_MOUSE;
    inp.mi.dx = (LONG)x;
    inp.mi.dy = (LONG)y;
    inp.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    UINT sent = SendInput(1, &inp, sizeof(INPUT));
    if (sent != 1) {
        if (out_error) *out_error = GetLastError();
        return false;
    }
    return true;
}

bool input_inject_click(int x, int y, DWORD* out_error) {
    return input_inject_click_norm(0.0, 0.0, "left", x, y, 0, 0, out_error);
}

bool input_is_vk_allowed(int vk) {
    /* Forbidden keys */
    if (vk == VK_LWIN || vk == VK_RWIN || vk == VK_APPS) return false;

    if (vk >= 'A' && vk <= 'Z') return true;
    if (vk >= '0' && vk <= '9') return true;
    if (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE) return true;
    if (vk >= VK_F1 && vk <= VK_F12) return true;
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

                SendInput(2, inps, sizeof(INPUT));
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
        if ((ctrl_st & 0x8000) && (alt_st & 0x8000)) {
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
        INPUT inps[2];
        memset(inps, 0, sizeof(inps));

        inps[0].type = INPUT_KEYBOARD;
        inps[0].ki.wVk = (WORD)vk;
        inps[0].ki.wScan = scan;
        inps[0].ki.dwFlags = ext_flag;

        inps[1].type = INPUT_KEYBOARD;
        inps[1].ki.wVk = (WORD)vk;
        inps[1].ki.wScan = scan;
        inps[1].ki.dwFlags = ext_flag | KEYEVENTF_KEYUP;

        UINT sent = SendInput(2, inps, sizeof(INPUT));
        if (sent != 2) {
            if (out_error) *out_error = GetLastError();
            return false;
        }
    } else {
        INPUT inp;
        memset(&inp, 0, sizeof(INPUT));
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = (WORD)vk;
        inp.ki.wScan = scan;
        inp.ki.dwFlags = ext_flag | (is_up ? KEYEVENTF_KEYUP : 0);

        UINT sent = SendInput(1, &inp, sizeof(INPUT));
        if (sent != 1) {
            if (out_error) *out_error = GetLastError();
            return false;
        }
    }

    return true;
}
