cmake_minimum_required(VERSION 3.20)
project(terminal-cert-installer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /MT)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE)
else()
    add_compile_options(-Wall -Wextra)
    add_definitions(-DUNICODE -D_UNICODE)
endif()

include_directories(src)

set(SOURCES
    src/main.c
    src/http_client.c
    src/xml_utils.c
    src/cng_crypto.c
    src/cert_store.c
)

add_executable(terminal-cert-installer ${SOURCES})

if(MSVC)
    target_link_libraries(terminal-cert-installer
        ncrypt.lib
        crypt32.lib
        winhttp.lib
        advapi32.lib
    )
else()
    target_link_libraries(terminal-cert-installer
        -lncrypt
        -lcrypt32
        -lwinhttp
        -ladvapi32
    )
endif()
