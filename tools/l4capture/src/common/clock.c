#include <windows.h>
#include "l4capture/clock.h"
uint64_t l4c_now_monotonic_ms(void) {
    return GetTickCount64();
}