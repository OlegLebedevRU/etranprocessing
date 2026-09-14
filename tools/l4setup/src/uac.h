#pragma once
#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Check whether the current process token is elevated (Administrator / High IL).
 */
bool uac_is_elevated(void);

#ifdef __cplusplus
}
#endif
