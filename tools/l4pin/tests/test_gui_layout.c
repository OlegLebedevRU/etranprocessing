/* Exercise the actual native dialog on a simulated 800x600 desktop with
 * a 40px taskbar. No certificate store or enrollment is accessed. */
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>

static BOOL WINAPI fixture_monitor(HMONITOR monitor, LPMONITORINFO info) {
    (void)monitor;
    info->rcMonitor = (RECT){0, 0, 800, 600};
    info->rcWork = (RECT){0, 0, 800, 560};
    info->dwFlags = MONITORINFOF_PRIMARY;
    return TRUE;
}
static HCERTSTORE WINAPI fixture_store(LPCSTR provider, DWORD encoding,
    HCRYPTPROV_LEGACY key, DWORD flags, const void* parameters) {
    (void)provider; (void)encoding; (void)key; (void)flags; (void)parameters;
    return NULL;
}
#define GetMonitorInfoW fixture_monitor
#define CertOpenStore fixture_store
#include "../src/gui.c"
#undef GetMonitorInfoW
#undef CertOpenStore

int l4pin_run_cli(int argc, char* argv[]) {
    (void)argc; (void)argv;
    return 31; /* Enrollment is impossible in this fixture. */
}

int main(void) {
    int failures = 0;
    wchar_t number[8];
    if (!terminal_number(L"a4b0000773c82116d210826", number) || wcscmp(number, L"0000773")) failures++;
    if (terminal_number(L"DD437F549BA7FB5383C9700D69A96C7F", number)) failures++;
    if (terminal_number(L"a4b000x773c82116d210826", number)) failures++;
    HWND dialog = CreateDialogParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_PIN_GUI), NULL, gui_proc, 0);
    if (!dialog) { printf("CreateDialog failed: %lu\n", GetLastError()); return 1; }
    RECT window, client;
    GetWindowRect(dialog, &window);
    GetClientRect(dialog, &client);
    if (window.left < 0 || window.top < 0 || window.right > 800 || window.bottom > 560) failures++;
    for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        RECT rect;
        GetWindowRect(child, &rect);
        MapWindowPoints(NULL, dialog, (POINT*)&rect, 2);
        if (rect.left < 0 || rect.top < 0 || rect.right > client.right || rect.bottom > client.bottom ||
            rect.right <= rect.left || rect.bottom <= rect.top) {
            printf("Control %d outside client area\n", GetDlgCtrlID(child)); failures++;
        }
    }
    LOGFONTW pin;
    GetObjectW(pin_font, sizeof(pin), &pin);
    RECT edit;
    GetWindowRect(GetDlgItem(dialog, IDC_PIN_EDIT), &edit);
    if (-pin.lfHeight < 24 || -pin.lfHeight > edit.bottom - edit.top - 4) failures++;
    printf("GUI 800x600: window %ldx%ld, PIN font %ldpx; failures=%d\n",
        window.right-window.left, window.bottom-window.top, -pin.lfHeight, failures);
    DestroyWindow(dialog);
    return failures ? 1 : 0;
}
