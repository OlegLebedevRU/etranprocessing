cmake_minimum_required(VERSION 3.20)
project(l4sql C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.c
    src/xml_parser.c
    src/db_discovery.c
    src/sql_validator.c
    src/db_odbc.c
    src/output_formatter.c
)

set(HEADERS
    src/xml_parser.h
    src/db_discovery.h
    src/sql_validator.h
    src/db_odbc.h
    src/output_formatter.h
)

if(WIN32)
    list(APPEND SOURCES res/l4sql.rc)
endif()

add_executable(l4sql ${SOURCES} ${HEADERS})

target_include_directories(l4sql PRIVATE src res)

if(MSVC)
    target_compile_options(l4sql PRIVATE /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE)
    target_link_libraries(l4sql PRIVATE odbc32 advapi32 user32 shlwapi)
else()
    target_compile_options(l4sql PRIVATE -O2 -Wall -Wextra -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN)
    target_link_libraries(l4sql PRIVATE -static -lodbc32 -ladvapi32 -luser32 -lshlwapi)
endif()
