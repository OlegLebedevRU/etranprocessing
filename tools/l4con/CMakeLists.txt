cmake_minimum_required(VERSION 3.20)
project(l4con C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.c
    src/config.c
    ../leo4proxy/src/policy_json.c
    ../l4pin/src/http_client.c
    src/mqtt_protocol.c
    src/command_runner.c
    src/mqtt_client.c
    src/event_ipc.c
    src/tool_inventory.c
    ../l4pin/src/cert_discovery.c
    src/service_mgr.c
)

set(HEADERS
    src/config.h
    src/mqtt_protocol.h
    src/command_runner.h
    src/mqtt_client.h
    src/event_ipc.h
    src/service_mgr.h
)

if(WIN32)
    list(APPEND SOURCES res/l4con.rc)
endif()

add_executable(l4con ${SOURCES} ${HEADERS})

target_include_directories(l4con PRIVATE src res ../l4pin/src)
set_property(TARGET l4con PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_definitions(l4con PRIVATE _WIN32_WINNT=0x0601)

if(MSVC)
    target_compile_options(l4con PRIVATE /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE)
    target_link_libraries(l4con PRIVATE ws2_32 winhttp advapi32 user32 shlwapi ole32 shell32 crypt32 ncrypt version)
else()
    target_compile_options(l4con PRIVATE -O2 -Wall -Wextra -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN)
    target_link_libraries(l4con PRIVATE -static ws2_32 winhttp advapi32 user32 shlwapi ole32 shell32 crypt32 ncrypt version)
endif()
