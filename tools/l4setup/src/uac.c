#include "uac.h"

bool uac_is_elevated(void) {
    bool is_elevated = false;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD cbSize = sizeof(elevation);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
            is_elevated = (elevation.TokenIsElevated != 0);
        }
        CloseHandle(hToken);
    }
    return is_elevated;
}
