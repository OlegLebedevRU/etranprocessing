#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "l4capture/clock.h"
#include "l4capture/safety_gate.h"
#include "l4capture/ipc_pipe.h"

typedef struct {
    HANDLE pipe_in;
    HANDLE pipe_out;
    bool valid;
} l4c_args_t;

static l4c_args_t parse_args(int argc, char *argv[]) {
    l4c_args_t args;
    int i;
    memset(&args, 0, sizeof(args));
    for (i = 1; i < argc; ++i) {
        const char *p = argv[i];
        if (strncmp(p, "--pipe-in=", 10) == 0) {
            if (l4c_parse_handle(p + 10, &args.pipe_in) != L4C_OK) return args;
        } else if (strncmp(p, "--pipe-out=", 11) == 0) {
            if (l4c_parse_handle(p + 11, &args.pipe_out) != L4C_OK) return args;
        } else {
            return args;
        }
    }
    if (args.pipe_in && args.pipe_out) args.valid = true;
    return args;
}

static int run(l4c_args_t *args) {
    l4c_safety_gate_t gate;
    l4c_ipc_pipe_t pipe;
    l4c_session_probe_t session;
    l4c_status_t status;
    int exit_code = 0;

    if (l4c_check_job() != L4C_OK) {
        fprintf(stderr, "l4capture: not in JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE\n");
        return (int)L4C_ERR_FATAL;
    }
    if (l4c_session_open(&session) != L4C_OK) {
        fprintf(stderr, "l4capture: session unavailable (Session 0 or locked)\n");
        return (int)L4C_ERR_SESSION_UNAVAILABLE;
    }
    status = l4c_safety_init(&gate);
    if (status != L4C_OK) { l4c_session_close(&session); return (int)status; }

    status = l4c_pipe_open(&pipe, args->pipe_in, args->pipe_out, &gate);
    if (status != L4C_OK) {
        fprintf(stderr, "l4capture: pipe open failed (%d)\n", (int)status);
        l4c_safety_destroy(&gate); l4c_session_close(&session);
        return (int)status;
    }
    args->pipe_in = NULL; args->pipe_out = NULL;

    /* Main loop: poll IPC, check safety, pace at ~10ms tick. */
    while (WaitForSingleObject(gate.stop_event, 0) == WAIT_TIMEOUT) {
        uint64_t now = l4c_now_monotonic_ms();
        if (!l4c_session_available(&session)) {
            l4c_safety_stop(&gate, L4C_ERR_SESSION_UNAVAILABLE);
            break;
        }
        status = l4c_pipe_poll(&pipe, now);
        if (status != L4C_OK && status != L4C_ERR_PIPE_BROKEN) break;
        if (WaitForSingleObject(gate.stop_event, 0) != WAIT_TIMEOUT) break;
        status = l4c_safety_check(&gate, now, l4c_session_available(&session));
        if (status != L4C_OK) break;
        if (l4c_pipe_stalled(&pipe, now)) {
            l4c_safety_stop(&gate, L4C_ERR_OVERFLOW);
            break;
        }
        /* Stub: no capture/encoder yet. Sleep to avoid busy spin. */
        Sleep(10);
    }

    l4c_pipe_close(&pipe);
    exit_code = (int)l4c_safety_reason(&gate);
    l4c_safety_destroy(&gate);
    l4c_session_close(&session);
    return exit_code;
}

int main(int argc, char *argv[]) {
    l4c_args_t args = parse_args(argc, argv);
    if (!args.valid) {
        fprintf(stderr, "Usage: l4capture.exe --pipe-in=<HANDLE> --pipe-out=<HANDLE>\n");
        return (int)L4C_ERR_INVALID_ARG;
    }
    return run(&args);
}
