#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    const char* mode = getenv("FAKE_FFMPEG_MODE");
    if (!mode) mode = "exit_on_q";

    if (strcmp(mode, "crash") == 0) {
        fprintf(stderr, "fake_ffmpeg: crashing immediately\n");
        return 1;
    }

    if (strcmp(mode, "hang") == 0) {
        fprintf(stderr, "fake_ffmpeg: hanging (ignoring stdin)...\n");
        while (1) {
            Sleep(1000);
        }
        return 0;
    }

    if (strcmp(mode, "stall") == 0) {
        fprintf(stdout, "frame=1\nout_time_ms=1000\nfps=25\n");
        fflush(stdout);
        fprintf(stderr, "fake_ffmpeg: stalling after initial frame...\n");
        while (1) {
            Sleep(1000);
        }
        return 0;
    }

    /* Default: exit_on_q */
    fprintf(stdout, "frame=1\nout_time_ms=1000\nfps=25\n");
    fflush(stdout);

    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    char buf[128];
    DWORD bytesRead = 0;

    int frame = 1;
    while (1) {
        /* Check if stdin has input */
        DWORD avail = 0;
        if (PeekNamedPipe(hStdin, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            if (ReadFile(hStdin, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
                buf[bytesRead] = '\0';
                if (strchr(buf, 'q')) {
                    fprintf(stderr, "fake_ffmpeg: received 'q', exiting cleanly with code 0\n");
                    return 0;
                }
            }
        }

        fprintf(stdout, "frame=%d\nout_time_ms=%d\nfps=25\n", ++frame, frame * 40);
        fflush(stdout);
        Sleep(100);
    }

    return 0;
}
