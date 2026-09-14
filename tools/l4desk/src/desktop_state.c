#include "desktop_state.h"
#include <windows.h>
#include <wtsapi32.h>
#include <string.h>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "user32.lib")

static int s_test_override = -1;

void desktop_set_test_override(int override_status) {
    s_test_override = override_status;
}

DWORD desktop_get_current_session_id(void) {
    DWORD sessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId)) {
        return 0;
    }
    return sessionId;
}

DesktopAccessStatus desktop_check_input_access(void) {
    if (s_test_override >= 0) {
        return (DesktopAccessStatus)s_test_override;
    }

    // 1. Session 0 cannot receive interactive input injection
    DWORD sessionId = desktop_get_current_session_id();
    if (sessionId == 0) {
        return DESKTOP_ACCESS_SESSION_UNAVAILABLE;
    }

    // 2. Check active console session ID matches current session
    DWORD activeConsoleSession = WTSGetActiveConsoleSessionId();
    if (activeConsoleSession == 0xFFFFFFFF || activeConsoleSession != sessionId) {
        return DESKTOP_ACCESS_SESSION_UNAVAILABLE;
    }

    // 3. Open current active input desktop
    HDESK hDesk = OpenInputDesktop(0, FALSE, GENERIC_READ);
    if (!hDesk) {
        return DESKTOP_ACCESS_LOCKED;
    }

    // 4. Verify desktop name is "Default" (not "Winlogon", "Screen-saver", etc.)
    wchar_t nameBuf[256] = { 0 };
    DWORD needed = 0;
    BOOL ok = GetUserObjectInformationW(hDesk, UOI_NAME, nameBuf, sizeof(nameBuf), &needed);
    CloseDesktop(hDesk);

    if (!ok) {
        return DESKTOP_ACCESS_LOCKED;
    }

    if (_wcsicmp(nameBuf, L"Default") != 0) {
        return DESKTOP_ACCESS_LOCKED;
    }

    return DESKTOP_ACCESS_OK;
}

bool desktop_is_interactive_available(void) {
    return desktop_check_input_access() == DESKTOP_ACCESS_OK;
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
