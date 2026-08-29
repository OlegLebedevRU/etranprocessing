cmake_minimum_required(VERSION 3.20)
project(l4superv C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(COMMON_SOURCES
    src/config.c
    src/hardware_fingerprint.c
    src/state_mgr.c
    src/service_mgr.c
    src/mosquitto_conf.c
)

set(SUPERV_SOURCES
    src/supervisor_main.c
    src/proxy_client.c
    src/orchestrator.c
    ${COMMON_SOURCES}
)

set(INSTALL_SOURCES
    src/installer_main.c
    src/miniz.c
    src/zip_extractor.c
    ${COMMON_SOURCES}
)

if(WIN32)
    list(APPEND SUPERV_SOURCES res/l4superv.rc)
    list(APPEND INSTALL_SOURCES res/l4install.rc)
endif()

# 1. Target: l4superv
add_executable(l4superv ${SUPERV_SOURCES})
target_include_directories(l4superv PRIVATE src res)

# 2. Target: l4install
add_executable(l4install ${INSTALL_SOURCES})
target_include_directories(l4install PRIVATE src res)

if(MSVC)
    target_compile_options(l4superv PRIVATE /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE)
    target_link_libraries(l4superv PRIVATE advapi32 crypt32 winhttp ws2_32 user32 shlwapi)

    target_compile_options(l4install PRIVATE /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE)
    target_link_libraries(l4install PRIVATE advapi32 crypt32 winhttp ws2_32 user32 shlwapi shell32)
else()
    target_compile_options(l4superv PRIVATE -O2 -Wall -Wextra -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN)
    target_link_libraries(l4superv PRIVATE -static -ladvapi32 -lcrypt32 -lwinhttp -lws2_32 -luser32 -lshlwapi)

    target_compile_options(l4install PRIVATE -O2 -Wall -Wextra -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN)
    target_link_libraries(l4install PRIVATE -static -ladvapi32 -lcrypt32 -lwinhttp -lws2_32 -luser32 -lshlwapi -lshell32)
endif()
