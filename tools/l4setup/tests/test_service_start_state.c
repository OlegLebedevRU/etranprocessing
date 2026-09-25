#include <windows.h>
#include <stdio.h>
#include "../src/service_start_state.h"

static int check(DWORD state, DWORD win32_code, DWORD service_code, ServiceStartDecision expected) {
    SERVICE_STATUS_PROCESS status = { 0 };
    status.dwCurrentState = state;
    status.dwWin32ExitCode = win32_code;
    status.dwServiceSpecificExitCode = service_code;
    if (service_start_decide(&status) != expected) {
        fprintf(stderr, "state=%lu win32=%lu specific=%lu: wrong decision\n",
                state, win32_code, service_code);
        return 1;
    }
    return 0;
}

int main(void) {
    int failures = 0;
    failures += check(SERVICE_RUNNING, 0, 0, SERVICE_START_SUCCEEDED);
    failures += check(SERVICE_START_PENDING, 0, 0, SERVICE_START_WAIT);
    failures += check(SERVICE_STOPPED, 0, 0, SERVICE_START_FAILED);
    failures += check(SERVICE_STOPPED, 5, 0, SERVICE_START_FAILED);
    failures += check(SERVICE_STOPPED, 0, 42, SERVICE_START_FAILED);
    failures += check(SERVICE_PAUSED, 0, 0, SERVICE_START_FAILED);
    printf("service start state: %d/6 passed\n", 6 - failures);
    return failures ? 1 : 0;
}
