cmake_minimum_required(VERSION 3.16)
project(leo4proxy C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.c
    src/cert_store.c
    src/schannel_tls.c
    src/mqtt_proxy.c
    src/http_proxy.c
    src/reverse_proxy.c
    src/discovery.c
    src/firewall.c
    src/service_mgr.c
    src/tray_icon.c
    res/leo4proxy.rc
)

set(HEADERS
    src/config.h
    src/cert_store.h
    src/schannel_tls.h
    src/mqtt_proxy.h
    src/http_proxy.h
    src/reverse_proxy.h
    src/discovery.h
    src/firewall.h
    src/service_mgr.h
    src/tray_icon.h
    res/resource.h
)

add_executable(leo4proxy ${SOURCES} ${HEADERS})

target_include_directories(leo4proxy PRIVATE src)

if(MSVC)
    set_property(TARGET leo4proxy PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(leo4proxy PRIVATE /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE)
endif()

target_link_libraries(leo4proxy PRIVATE
    ws2_32
    crypt32
    ncrypt
    secur32
    advapi32
    shell32
    user32
    gdi32
    iphlpapi
)

set_target_properties(leo4proxy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)
