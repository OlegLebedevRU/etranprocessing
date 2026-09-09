#include "input_inject.h"
#include <string.h>

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
    if (out_error) *out_error = 0;

    INPUT inps[3];
    memset(inps, 0, sizeof(inps));

    // 0: Move to (x, y)
    inps[0].type = INPUT_MOUSE;
    inps[0].mi.dx = (LONG)x;
    inps[0].mi.dy = (LONG)y;
    inps[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    // 1: Left Button Down
    inps[1].type = INPUT_MOUSE;
    inps[1].mi.dx = (LONG)x;
    inps[1].mi.dy = (LONG)y;
    inps[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    // 2: Left Button Up
    inps[2].type = INPUT_MOUSE;
    inps[2].mi.dx = (LONG)x;
    inps[2].mi.dy = (LONG)y;
    inps[2].mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    UINT sent = SendInput(3, inps, sizeof(INPUT));
    if (sent != 3) {
        if (out_error) *out_error = GetLastError();
        return false;
    }
    return true;
}
