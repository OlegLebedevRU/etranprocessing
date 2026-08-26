cmake_minimum_required(VERSION 3.16)
project(leo4-simple-svc-mqtt C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.c
    src/config.c
    src/mqtt_client.c
    src/service_mgr.c
    res/leo4_svc_mqtt.rc
)

set(HEADERS
    src/config.h
    src/mqtt_client.h
    src/service_mgr.h
    res/resource.h
)

add_executable(leo4-simple-svc-mqtt ${SOURCES} ${HEADERS})

target_include_directories(leo4-simple-svc-mqtt PRIVATE src res)

if(MSVC)
    set_property(TARGET leo4-simple-svc-mqtt PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(leo4-simple-svc-mqtt PRIVATE /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE)
endif()

target_link_libraries(leo4-simple-svc-mqtt PRIVATE
    ws2_32
    winhttp
    advapi32
    user32
)

set_target_properties(leo4-simple-svc-mqtt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)
