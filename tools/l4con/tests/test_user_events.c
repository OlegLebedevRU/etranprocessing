#define _WIN32_WINNT 0x0601
#include "../src/event_ipc.h"
#include "../src/command_runner.h"
#include "../src/mqtt_protocol.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <shellapi.h>

static int failures;
static volatile LONG published;
static HANDLE seen;
static UserEvent last_event;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static bool fake_publish(const UserEvent* event, void* context) {
    (void)context;
    last_event = *event;
    InterlockedIncrement(&published);
    SetEvent(seen);
    return true;
}

static int cli_call(void) {
    wchar_t* args[] = { L"test", L"--send-event", L"-event-payload={\"result\":\"ok\"}" };
    return event_cli(3, args);
}

static void test_args(void) {
    UserEvent event;
    wchar_t* defaults[] = { L"test", L"--send-event" };
    CHECK(event_parse_args(2, defaults, &event));
    CHECK(event.code == 999 && event.exit_code == 0 && !event.has_payload);
    wchar_t* args[] = { L"test", L"--send-event", L"-event-code=900",
        L"-event-exit-code=-2147483648", L"-event-payload={\"result\":\"ok\"}" };
    CHECK(event_parse_args(5, args, &event));
    CHECK(event_validate(&event) && event.code == 900 && event.exit_code == INT32_MIN);
    CHECK(strcmp(event.payload, "{\"result\":\"ok\"}") == 0);
    args[3] = L"-event-exit-code=2147483647";
    CHECK(event_parse_args(5, args, &event) && event.exit_code == INT32_MAX);
    const wchar_t* invalid[] = { L"2147483648", L"-2147483649", L"1.0", L"", L" 1", L"abc", L"9999999999999999999999999" };
    wchar_t exit_arg[128];
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        swprintf_s(exit_arg, 128, L"-event-exit-code=%ls", invalid[i]);
        args[3] = exit_arg;
        CHECK(event_parse_args(5, args, &event) && event.exit_code == -1);
    }
    args[2] = L"-event-code=899";
    CHECK(!event_parse_args(5, args, &event));
    args[2] = L"-event-code=1000";
    CHECK(!event_parse_args(5, args, &event));
    args[2] = L"-event-code=999";
    wchar_t payload[1100];
    wcscpy_s(payload, 1100, L"-event-payload=");
    for (int i = 0; i < 1024; ++i) payload[15 + i] = L'x';
    payload[15 + 1024] = 0;
    args[4] = payload;
    args[3] = L"-event-exit-code=42";
    CHECK(event_parse_args(5, args, &event) && event.payload_len == 1024 && event.exit_code == 42);
    payload[15 + 1024] = L'x'; payload[15 + 1025] = 0;
    CHECK(event_parse_args(5, args, &event) && !event.has_payload && event.exit_code == -2);
    for (int i = 0; i < 512; ++i) payload[15 + i] = L'я';
    payload[15 + 512] = 0;
    CHECK(event_parse_args(5, args, &event) && event.payload_len == 1024);
    payload[15 + 512] = L'я'; payload[15 + 513] = 0;
    CHECK(event_parse_args(5, args, &event) && !event.has_payload && event.exit_code == -2);
    args[4] = L"-event-payload=\"{\\\"x\\\":1}\"";
    CHECK(event_parse_args(5, args, &event));
    CHECK(strcmp(event.payload, "\"{\\\"x\\\":1}\"") == 0);
    args[4] = L"-event-payload=";
    CHECK(event_parse_args(5, args, &event) && event.has_payload && event.payload_len == 0);
    event.payload_len = 1025;
    CHECK(!event_validate(&event));
    wchar_t* uuid_args[] = { L"test", L"--send-event",
        L"-event-correlation-id=12345678-1234-1234-1234-123456789abc" };
    CHECK(event_parse_args(3, uuid_args, &event) && event_validate(&event));
    CHECK(strcmp(event.correlation_id, "12345678-1234-1234-1234-123456789abc") == 0);
    char json[7000];
    CHECK(event_json(&event, "test", 1, "2026-10-01T00:00:00Z", "internal", json, sizeof(json)) > 0);
    CHECK(strstr(json, "\"448\":\"12345678-1234-1234-1234-123456789abc\"") != NULL);
    CHECK(strstr(json, "\"correlationData\":\"internal\"") != NULL);
    uuid_args[2] = L"-event-correlation-id=bad";
    CHECK(!event_parse_args(3, uuid_args, &event));
    uuid_args[2] = L"-event-correlation-id=";
    CHECK(!event_parse_args(3, uuid_args, &event));
    args[4] = L"-event-payload={\"result\":\"ok\"}";
    CHECK(event_parse_args(5, args, &event));
    CHECK(event_json(&event, "test", 1, "utc", "internal", json, sizeof(json)) > 0);
    CHECK(strstr(json, "\"446\":\"{\\\"result\\\":\\\"ok\\\"}\"") != NULL);
    CHECK(strstr(json, "\"447\":42") != NULL && !strstr(json, "\"448\""));
    for (int i = 0; i < 1024; ++i) event.payload[i] = 1;
    event.payload[1024] = 0; event.payload_len = 1024;
    CHECK(event_json(&event, "test", 1, "utc", "internal", json, sizeof(json)) > 6144);
    CHECK(event_parse_args(5, args, &event));
    event.payload[0] = -1; event.payload_len = 1; event.payload[1] = 0;
    CHECK(!event_validate(&event));
    ULONGLONG last = 0; bool used = false;
    CHECK(event_rate_take(0, &last, &used));
    CHECK(!event_rate_take(999, &last, &used));
    CHECK(event_rate_take(1000, &last, &used));
    CHECK(!event_rate_take(1001, &last, &used));
}

static PROCESS_INFORMATION spawn(const wchar_t* mode, HANDLE job) {
    wchar_t executable[MAX_PATH], command[MAX_PATH + 80];
    GetModuleFileNameW(NULL, executable, MAX_PATH);
    swprintf_s(command, MAX_PATH + 80, L"\"%ls\" %ls", executable, mode);
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process = { 0 };
    CHECK(CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                         NULL, NULL, &startup, &process));
    if (process.hProcess) {
        if (job) CHECK(AssignProcessToJobObject(job, process.hProcess));
        CHECK(ResumeThread(process.hThread) != (DWORD)-1);
    }
    return process;
}

static DWORD join(PROCESS_INFORMATION process) {
    DWORD code = 255;
    CHECK(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0);
    GetExitCodeProcess(process.hProcess, &code);
    if (code == STILL_ACTIVE) TerminateProcess(process.hProcess, 255);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return code;
}

static void test_ipc(void) {
    HANDLE job = CreateJobObjectW(NULL, NULL);
    volatile bool cancelled = false;
    event_job_register(job, &cancelled, GetTickCount64() + 20000);
    CHECK(cli_call() == EVENT_DENIED); /* Same user, outside authorized job. */
    CHECK(join(spawn(L"--sequence", job)) == 0);
    CHECK(published == 2 && last_event.code == 999 && last_event.exit_code == 0);
    CHECK(join(spawn(L"--send-once", NULL)) == EVENT_DENIED);
    cancelled = true;
    CHECK(join(spawn(L"--send-once", job)) == EVENT_DENIED);
    cancelled = false;
    event_job_register(job, &cancelled, GetTickCount64() - 1);
    CHECK(join(spawn(L"--send-once", job)) == EVENT_DENIED);
    event_job_register(job, &cancelled, GetTickCount64() + 20000);
    event_job_revoke(job);
    CHECK(join(spawn(L"--send-once", job)) == EVENT_DENIED);
    CHECK(published == 2);
    CloseHandle(job);
}

static DWORD WINAPI run_context(void* context) {
    int code; uint64_t duration;
    return (DWORD)command_runner_execute(context, NULL, NULL, &code, &duration);
}

static void test_runner(void) {
    CommandContext context;
    char executable[MAX_PATH];
    GetModuleFileNameA(NULL, executable, MAX_PATH);
    command_runner_init_context(&context);
    context.enable_blacklist = false;
    snprintf(context.command_line, sizeof(context.command_line), "\"%s\" --send-once", executable);
    int code; uint64_t duration;
    Sleep(1100);
    CHECK(command_runner_execute(&context, NULL, NULL, &code, &duration) == 0);
    CHECK(published == 3 && context.hJob == NULL);
    CHECK(cli_call() == EVENT_DENIED);
    context.shell = SHELL_POWERSHELL;
    snprintf(context.command_line, sizeof(context.command_line),
        "& '%s' --send-event ('-event-payload=' + ('x' * 1025)) -event-exit-code=7", executable);
    Sleep(1100);
    CHECK(command_runner_execute(&context, NULL, NULL, &code, &duration) == 0);
    CHECK(published == 4 && !last_event.has_payload && last_event.exit_code == -2);
    snprintf(context.command_line, sizeof(context.command_line),
        "& '%s' --send-event '-event-payload=\\\"{\\\"x\\\":1}\\\"'", executable);
    Sleep(1100);
    CHECK(command_runner_execute(&context, NULL, NULL, &code, &duration) == 0);
    CHECK(published == 5 && last_event.has_payload);
    CHECK(strcmp(last_event.payload, "\"{\"x\":1}\"") == 0);
    context.shell = SHELL_CMD;
    /* Root exits first; its delayed descendant retains authorization until TTL/end. */
    snprintf(context.command_line, sizeof(context.command_line), "start \"\" /b \"%s\" --delayed", executable);
    Sleep(1100);
    CHECK(command_runner_execute(&context, NULL, NULL, &code, &duration) == 0);
    CHECK(published == 6 && duration >= 1100);
    snprintf(context.command_line, sizeof(context.command_line), "\"%s\" --delayed", executable);
    context.ttl_sec = 1;
    CHECK(command_runner_execute(&context, NULL, NULL, &code, &duration) == 124);
    CHECK(context.hJob == NULL && published == 6);
    context.ttl_sec = 10;
    context.cancel_requested = false;
    ResetEvent(seen);
    snprintf(context.command_line, sizeof(context.command_line), "\"%s\" --hold", executable);
    HANDLE thread = CreateThread(NULL, 0, run_context, &context, 0, NULL);
    CHECK(WaitForSingleObject(seen, 5000) == WAIT_OBJECT_0);
    command_runner_request_cancel(&context);
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    DWORD result = 0; GetExitCodeThread(thread, &result);
    CHECK(result == 130 && context.hJob == NULL);
    CloseHandle(thread);
    CHECK(cli_call() == EVENT_DENIED);
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--send-event") == 0) {
        int count;
        wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        int result = event_cli(count, arguments);
        LocalFree(arguments);
        return result;
    }
    if (argc == 2 && strcmp(argv[1], "--send-once") == 0) return cli_call();
    if (argc == 2 && strcmp(argv[1], "--delayed") == 0) { Sleep(1200); return cli_call(); }
    if (argc == 2 && strcmp(argv[1], "--hold") == 0) { int code = cli_call(); Sleep(5000); return code; }
    if (argc == 2 && strcmp(argv[1], "--sequence") == 0) {
        int first = cli_call(), second = cli_call();
        Sleep(100); /* Let the pipe receipt finish; still within the one-second limit. */
        int fourth = cli_call();
        Sleep(1100);
        int third = cli_call();
        return first == 0 && (second == EVENT_RATE_LIMIT || second == EVENT_UNAVAILABLE) &&
               fourth == EVENT_RATE_LIMIT && third == 0 ? 0 : 99;
    }
    test_args();
    HANDLE stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    seen = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(event_ipc_start(stop, fake_publish, NULL));
    test_ipc();
    test_runner();
    SetEvent(stop);
    event_ipc_stop();
    CloseHandle(stop); CloseHandle(seen);
    CHECK(cli_call() == EVENT_UNAVAILABLE);
    if (!failures) puts("User event parser, IPC authorization, rate, job lifecycle tests passed");
    return failures ? 1 : 0;
}
