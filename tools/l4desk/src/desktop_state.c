#include "desktop_state.h"
#include <windows.h>
#include <wtsapi32.h>
#include <string.h>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "user32.lib")

DWORD desktop_get_current_session_id(void) {
    DWORD sessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId)) {
        return 0;
    }
    return sessionId;
}

bool desktop_is_interactive_available(void) {
    // 1. Session 0 cannot receive interactive input injection
    DWORD sessionId = desktop_get_current_session_id();
    if (sessionId == 0) {
        return false;
    }

    // 2. Open current active input desktop
    HDESK hDesk = OpenInputDesktop(0, FALSE, GENERIC_READ);
    if (!hDesk) {
        return false;
    }

    // 3. Verify desktop name is "Default" (not "Winlogon", "Screen-saver", etc.)
    char nameBuf[256] = { 0 };
    DWORD needed = 0;
    BOOL ok = GetUserObjectInformationA(hDesk, UOI_NAME, nameBuf, sizeof(nameBuf) - 1, &needed);
    CloseDesktop(hDesk);

    if (!ok) {
        return false;
    }

    if (_stricmp(nameBuf, "Default") != 0) {
        return false;
    }

    return true;
}

void desktop_get_screen_metrics(ScreenMetrics* out) {
    if (!out) return;
    memset(out, 0, sizeof(ScreenMetrics));

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

    out->virtual_x = vx;
    out->virtual_y = vy;
    out->virtual_width = vw > 0 ? vw : 1920;
    out->virtual_height = vh > 0 ? vh : 1080;
}
