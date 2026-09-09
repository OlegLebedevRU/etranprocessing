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

bool desktop_is_interactive_available(void);
void desktop_get_screen_metrics(ScreenMetrics* out);
DWORD desktop_get_current_session_id(void);

#endif /* L4DESK_DESKTOP_STATE_H */
