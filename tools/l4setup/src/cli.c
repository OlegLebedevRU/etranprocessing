#include "cli.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cli_init_defaults(CliOptions* opts) {
    if (!opts) return;
    memset(opts, 0, sizeof(CliOptions));
    wcscpy_s(opts->dest, MAX_PATH, L"C:\\l4tools");
    opts->interactive = true;
    opts->silent = false;
}

static bool is_valid_6digit_pin(const wchar_t* pin) {
    if (!pin || wcslen(pin) != 6) return false;
    for (int i = 0; i < 6; i++) {
        if (pin[i] < L'0' || pin[i] > L'9') return false;
    }
    return true;
}

void cli_clean_pin(CliOptions* opts) {
    if (opts) {
        SecureZeroMemory(opts->pin, sizeof(opts->pin));
        opts->pin_specified = false;
    }
}

bool cli_parse(int argc, wchar_t* argv[], CliOptions* opts, char* err_buf, size_t err_buf_size) {
    if (!opts) return false;
    cli_init_defaults(opts);

    bool explicit_interactive = false;
    bool explicit_silent = false;

    for (int i = 1; i < argc; i++) {
        const wchar_t* arg = argv[i];

        if (_wcsicmp(arg, L"--version") == 0 || _wcsicmp(arg, L"-version") == 0 || 
            _wcsicmp(arg, L"/version") == 0 || _wcsicmp(arg, L"-v") == 0) {
            opts->show_version = true;
            return true;
        } else if (_wcsicmp(arg, L"--help") == 0 || _wcsicmp(arg, L"-help") == 0 || 
                   _wcsicmp(arg, L"/help") == 0 || _wcsicmp(arg, L"-h") == 0 || 
                   _wcsicmp(arg, L"/?") == 0) {
            opts->show_help = true;
            return true;
        } else if (_wcsicmp(arg, L"--force-reissue") == 0 || _wcsicmp(arg, L"-force-reissue") == 0 ||
                   _wcsicmp(arg, L"/force-reissue") == 0) {
            opts->force_reissue = true;
        } else if (_wcsicmp(arg, L"--no-pin") == 0 || _wcsicmp(arg, L"-no-pin") == 0 ||
                   _wcsicmp(arg, L"/no-pin") == 0) {
            opts->no_pin = true;
        } else if (_wcsicmp(arg, L"--silent") == 0 || _wcsicmp(arg, L"-silent") == 0 ||
                   _wcsicmp(arg, L"/silent") == 0 || _wcsicmp(arg, L"/S") == 0 || 
                   _wcsicmp(arg, L"/s") == 0 || _wcsicmp(arg, L"-s") == 0 ||
                   _wcsicmp(arg, L"--unattended") == 0 || _wcsicmp(arg, L"-unattended") == 0 ||
                   _wcsicmp(arg, L"/unattended") == 0) {
            explicit_silent = true;
        } else if (_wcsicmp(arg, L"--interactive") == 0 || _wcsicmp(arg, L"-interactive") == 0 ||
                   _wcsicmp(arg, L"/interactive") == 0 || _wcsicmp(arg, L"-i") == 0 ||
                   _wcsicmp(arg, L"/i") == 0) {
            explicit_interactive = true;
        } else if (_wcsicmp(arg, L"--repair") == 0 || _wcsicmp(arg, L"-repair") == 0 ||
                   _wcsicmp(arg, L"/repair") == 0) {
            opts->repair = true;
        } else if (_wcsicmp(arg, L"--smoke-only") == 0 || _wcsicmp(arg, L"-smoke-only") == 0 ||
                   _wcsicmp(arg, L"/smoke-only") == 0) {
            opts->smoke_only = true;
        } else if (_wcsicmp(arg, L"--pin") == 0 || _wcsicmp(arg, L"-pin") == 0 ||
                   _wcsicmp(arg, L"-p") == 0 || _wcsicmp(arg, L"/pin") == 0 || 
                   _wcsicmp(arg, L"/p") == 0) {
            if (i + 1 < argc) {
                wcsncpy_s(opts->pin, 64, argv[++i], _TRUNCATE);
                opts->pin_specified = true;
            } else {
                if (err_buf && err_buf_size > 0) snprintf(err_buf, err_buf_size, "Option --pin requires an argument");
                return false;
            }
        } else if (_wcsnicmp(arg, L"--pin=", 6) == 0) {
            wcsncpy_s(opts->pin, 64, arg + 6, _TRUNCATE);
            opts->pin_specified = true;
        } else if (_wcsnicmp(arg, L"-pin=", 5) == 0) {
            wcsncpy_s(opts->pin, 64, arg + 5, _TRUNCATE);
            opts->pin_specified = true;
        } else if (_wcsicmp(arg, L"--dest") == 0 || _wcsicmp(arg, L"-dest") == 0 ||
                   _wcsicmp(arg, L"-d") == 0 || _wcsicmp(arg, L"/dest") == 0 ||
                   _wcsicmp(arg, L"/d") == 0) {
            if (i + 1 < argc) {
                wcsncpy_s(opts->dest, MAX_PATH, argv[++i], _TRUNCATE);
                opts->dest_specified = true;
            } else {
                if (err_buf && err_buf_size > 0) snprintf(err_buf, err_buf_size, "Option --dest requires an argument");
                return false;
            }
        } else if (_wcsnicmp(arg, L"--dest=", 7) == 0) {
            wcsncpy_s(opts->dest, MAX_PATH, arg + 7, _TRUNCATE);
            opts->dest_specified = true;
        } else if (_wcsnicmp(arg, L"-dest=", 6) == 0) {
            wcsncpy_s(opts->dest, MAX_PATH, arg + 6, _TRUNCATE);
            opts->dest_specified = true;
        } else if (_wcsicmp(arg, L"--payload-dir") == 0) {
            if (i + 1 < argc) {
                wcsncpy_s(opts->payload_dir, MAX_PATH, argv[++i], _TRUNCATE);
                opts->payload_dir_specified = true;
            } else {
                if (err_buf && err_buf_size > 0) snprintf(err_buf, err_buf_size, "Option --payload-dir requires an argument");
                return false;
            }
        } else if (_wcsnicmp(arg, L"--payload-dir=", 14) == 0) {
            wcsncpy_s(opts->payload_dir, MAX_PATH, arg + 14, _TRUNCATE);
            opts->payload_dir_specified = true;
        } else {
            if (err_buf && err_buf_size > 0) {
                char a_arg[256] = { 0 };
                WideCharToMultiByte(CP_UTF8, 0, arg, -1, a_arg, sizeof(a_arg), NULL, NULL);
                snprintf(err_buf, err_buf_size, "Unknown argument: %s", a_arg);
            }
            return false;
        }
    }

    // Validate conflict between --interactive and --silent
    if (explicit_interactive && explicit_silent) {
        if (err_buf && err_buf_size > 0) {
            snprintf(err_buf, err_buf_size, "Conflict: --interactive and --silent cannot be specified together");
        }
        return false;
    }

    if (explicit_silent) {
        opts->silent = true;
        opts->interactive = false;
    } else {
        opts->interactive = true;
        opts->silent = false;
    }

    // Validate PIN if specified
    if (opts->pin_specified) {
        if (!is_valid_6digit_pin(opts->pin)) {
            if (err_buf && err_buf_size > 0) {
                snprintf(err_buf, err_buf_size, "PIN must be exactly 6 digits (0-9)");
            }
            return false;
        }
    }

    // In silent mode without --pin, behaves as --no-pin
    if (opts->silent && !opts->pin_specified) {
        opts->no_pin = true;
    }

    return true;
}

void cli_print_version(void) {
    wprintf(L"l4setup version %ls\n", L4SETUP_VERSION_WSTRING);
    fflush(stdout);
}

void cli_print_usage(const wchar_t* prog_name) {
    wprintf(L"Leo4 Zero-Touch Setup (l4setup) v%ls\n\n", L4SETUP_VERSION_WSTRING);
    wprintf(L"Usage: %ls [options]\n\n", prog_name ? prog_name : L"l4setup.exe");
    wprintf(L"Options:\n");
    wprintf(L"  --pin, -p <PIN>    6-digit terminal certificate PIN code\n");
    wprintf(L"  --force-reissue    Allow certificate reissuance even if existing cert is valid\n");
    wprintf(L"  --no-pin           Do not ask for PIN; if certificate absent, enter standby\n");
    wprintf(L"  --interactive, -i  Interactive GUI mode (default if run without arguments)\n");
    wprintf(L"  --silent, -s, /S   Silent/unattended execution (no GUI windows or dialog prompts)\n");
    wprintf(L"  --unattended       Alias for --silent\n");
    wprintf(L"  --dest, -d <DIR>   Target installation directory (default: C:\\l4tools)\n");
    wprintf(L"  --repair           Force reinstallation of binaries and services even if version matches\n");
    wprintf(L"  --smoke-only       Execute only Phase 5 smoke checks on existing installation\n");
    wprintf(L"  --version          Print version information and exit\n");
    wprintf(L"  --help, -h, /?     Show this help message and exit\n\n");
    wprintf(L"Exit Codes:\n");
    wprintf(L"   0  Ready (active + all 4 services healthy + verified)\n");
    wprintf(L"  10  Ready for PIN (standby / activation_required, PIN not provided)\n");
    wprintf(L"  11  Ready for Online (PIN saved to pending_pin.json, CA unreachable)\n");
    wprintf(L"  12  Ready with warnings / Degraded (local services ok, network/upstream down)\n");
    wprintf(L"  20  Access denied / Administrator privileges required\n");
    wprintf(L"  21  Unsupported OS version (< Windows 7 SP1 / 6.1)\n");
    wprintf(L"  22  Drainage failed (service stop failure, active stream, port occupied)\n");
    wprintf(L"  23  Payload extraction failed\n");
    wprintf(L"  24  Service registration failed\n");
    wprintf(L"  25  Certificate issuance failed (PIN rejected by CA)\n");
    wprintf(L"  26  Activation timeout (_leo4/info status not ready within 15s)\n");
    wprintf(L"  27  Critical smoke probe failure\n");
    wprintf(L"  28  Setup busy (another l4setup instance is running)\n");
    wprintf(L"  29  Downgrade blocked (installed version is newer than package)\n");
    wprintf(L"  30  Preflight failed (insufficient disk space, etc.)\n");
    wprintf(L"  31  Setup cancelled by user\n");
    fflush(stdout);
}
