#ifndef L4D_KIOSK_FOCUS_H
#define L4D_KIOSK_FOCUS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    L4D_KIOSK_FOCUS_MODE_KIOSK     = 0,
    L4D_KIOSK_FOCUS_MODE_FALLBACK  = 1,
    L4D_KIOSK_FOCUS_MODE_GENERIC   = 2
} l4d_kiosk_focus_mode_t;

bool kiosk_focus_init(const wchar_t *kiosk_process);
void kiosk_focus_destroy(void);

bool kiosk_is_process_running(void);
bool kiosk_is_window_focused(void);
bool kiosk_focus_force(HWND hKiosk);
HWND kiosk_get_cached_hwnd(void);

l4d_kiosk_focus_mode_t kiosk_get_active_mode(void);
void kiosk_get_active_input_mode(char *out, size_t max_len);
void kiosk_get_foreground_process(char *out, size_t max_len);

#endif /* L4D_KIOSK_FOCUS_H */
