#ifndef L4DESK_DESKTOP_STATE_H
#define L4DESK_DESKTOP_STATE_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdbool.h>

typedef struct {
    int virtual_x;
    int virtual_y;
    int virtual_width;
    int virtual_height;
} ScreenMetrics;

typedef enum {
    DESKTOP_ACCESS_OK = 0,
    DESKTOP_ACCESS_SESSION_UNAVAILABLE = 1,
    DESKTOP_ACCESS_LOCKED = 2,
    DESKTOP_ACCESS_ERROR = 3
} DesktopAccessStatus;

DesktopAccessStatus desktop_check_input_access(void);
bool desktop_is_interactive_available(void);
void desktop_get_screen_metrics(ScreenMetrics* out);
DWORD desktop_get_current_session_id(void);
void desktop_set_test_override(int override_status);

#endif /* L4DESK_DESKTOP_STATE_H */
