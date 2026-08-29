cmake_minimum_required(VERSION 3.20)
project(l4pin C)

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
    src/url_finder.c
    src/http_client.c
    src/xml_utils.c
    src/cng_crypto.c
    src/cert_store.c
    res/app.rc
)

add_executable(l4pin ${SOURCES})

if(MSVC)
    target_link_libraries(l4pin
        ncrypt.lib
        crypt32.lib
        winhttp.lib
        advapi32.lib
        shell32.lib
        user32.lib
    )
else()
    target_link_libraries(l4pin
        -lncrypt
        -lcrypt32
        -lwinhttp
        -ladvapi32
        -lshell32
        -luser32
    )
endif()
