cmake_minimum_required(VERSION 3.20)
project(l4desk C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.c
    src/config.c
    src/sn_discovery.c
    src/log.c
    src/json_min.c
    src/desktop_state.c
    src/input_inject.c
    src/dedup_cache.c
    src/ctl_protocol.c
    src/mqtt_protocol.c
    src/mqtt_client.c
)

set(HEADERS
    src/config.h
    src/sn_discovery.h
    src/log.h
    src/json_min.h
    src/desktop_state.h
    src/input_inject.h
    src/dedup_cache.h
    src/ctl_protocol.h
    src/mqtt_protocol.h
    src/mqtt_client.h
)

if(WIN32)
    list(APPEND SOURCES res/l4desk.rc)
endif()

add_executable(l4desk ${SOURCES} ${HEADERS})

target_include_directories(l4desk PRIVATE src res)

if(MSVC)
    target_compile_options(l4desk PRIVATE /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /DWIN32_LEAN_AND_MEAN)
    target_link_libraries(l4desk PRIVATE ws2_32 winhttp advapi32 user32 wtsapi32)
else()
    target_compile_options(l4desk PRIVATE -O2 -Wall -Wextra -DWIN32_LEAN_AND_MEAN)
    target_link_libraries(l4desk PRIVATE -static ws2_32 winhttp advapi32 user32 wtsapi32)
endif()
